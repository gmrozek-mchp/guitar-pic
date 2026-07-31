# T1S inter-node link — 10BASE-T1S + PoDL

Engineering detail for the marvin↔node link over **10BASE-T1S single-pair Ethernet**
(with planned "dumb" Power-over-Data-Lines). System-level summary and rationale live
in [`../SPEC.md`](../SPEC.md) §2/§6; this document holds the detail.

Status: **built and working between marvin (PLCA coordinator, node 0) and the
[`guitar`](../firmware/guitar/SPEC.md) actuator node (follower, node 2)** as of
2026-06-17. A LAN8651 MAC-PHY at each end over SPI + the OPEN Alliance TC6 driver
([`third_party/oa-tc6-lib`](../third_party/oa-tc6-lib)); a custom L2 header
(ethertype `0x88B5`) carries the existing 1-byte command bitmask; a presence
heartbeat (`0x88B6`, §7.2) plus link / `nodes` diagnostics run on both ends.

The [`fretboard`](../firmware/fretboard/SPEC.md) firmware to join as node 1 is
**written** (2026-06-17, `t1s_detector.{c,h}`), **T1S-only** (the old UART path +
build flags removed). It is a **sense+actuate** node: it streams its 17-byte data
frame to the coordinator (`0x88B5`, logging) **and** sends its on-device model's
inferred 1-byte command **directly to the guitar node** (`0x88B5`, dst `02:..:03`) —
peer-to-peer actuation — plus a `0x88B6` heartbeat. Its MCC config is done (SERCOM0
SPI + CS/RST/IRQ_N, mirror of guitar G0); remaining is the build-wiring + on-hardware
bring-up. marvin's RX side is already in place (node-table id 4 + `fretboard_link.c`
T1S frame handler); marvin keeps a `MARVIN_FRETBOARD_TRANSPORT={UART,T1S}` build flag
(§8). **PoDL is design-direction only** (not built) — the link is separately powered
during bring-up. Addressing in §7.1; the scope widened from a 2-node UART swap to
this multi-node PLCA bus on 2026-06-16.

> **Command-plane note:** with the fretboard driving the guitar directly, two nodes
> can address the guitar (marvin and the fretboard). There is no active-source
> arbitration yet — the guitar applies whoever transmitted last, so only one should
> be armed at a time. marvin-side active-detector/active-guitar selection is the
> follow-up (it currently picks `node_for_type(GUITAR)`).

---

## 1. Why T1S here

- **PoDL** — one twisted pair carries both data *and* the fretboard's power, so
  the cable to the guitar is a single 2-wire link instead of a data pair plus a
  separate power feed. This is the primary motivation.
- **Robustness / reach** — differential single-pair Ethernet tolerates a long,
  electrically noisy run to a moving instrument better than a TTL UART pair.
- **Multidrop headroom** — 10BASE-T1S/PLCA puts up to 8 nodes on one pair, so a
  second sensor node or fretboard can share the same pair *and* the same power.
- **Demonstration** — showcasing 10BASE-T1S + PoDL doing real work inside a
  larger Microchip system (SAM9X7 + LAN8651 + PIC32CM) is an explicit goal, not
  just an incidental benefit. Bandwidth is *not* a motivation — the existing
  500 kbaud UART already carries the ~4 KB/s payload with ~12× headroom.

## 2. The part: LAN8651B1

A **MAC-PHY**: integrates the full IEEE 802.3 Clause 4 MAC *and* the 10BASE-T1S
PHY on one chip, and talks to the host MCU over **SPI** using the OPEN Alliance
TC6 ("10BASE-T1x MAC-PHY Serial Interface", v1.1) protocol. It exists precisely
to give a small MCU with no built-in Ethernet MAC a single-pair Ethernet link
over ~5 pins.

Done in hardware by the LAN8651 (the MCU never touches these):

- Line coding, PLCA / CSMA-CD media access, preamble/SFD.
- **FCS (CRC32) generation on TX and checking on RX.**
- On-wire framing and minimum-frame padding.

SPI: LAN8651 is an SPI client, SCLK up to 25 MHz. Pins: `SDI`, `SDO`, `CS_N`,
`SCLK`, `IRQ_N` (active-low interrupt), plus a reset.

**SPI mode — Mode 0 (CPOL = 0, CPHA = 0), MSB first.** Mandated by the OA TC6
v1.1 spec the part conforms to, and confirmed in the LAN8650/1 datasheet
(DS60001734 §5.1 *SPI Format*, §9 timing): "on the rising edge of SCLK the data
is captured, while on the falling edge … the data will change … always
transferred most significant bit/byte first," and the `tcss` ("CS_N low to SCLK
rising") timing means **SCLK idles low** → CPOL = 0, sample-on-leading-edge →
CPHA = 0. Host SPI-master config: 8-bit words, MSB first, CPOL = 0 / CPHA = 0
(Harmony: *Clock Polarity = Idle Low*, *Clock Phase = Valid Leading Edge*),
`CS_N` active-low idling high. The same mode applies to the fretboard SERCOM.

Bring-up watch items: if the device-ID read returns `0x00`/`0xFF`, flip the
Harmony phase setting first (the dropdown wording varies) — the target is Mode 0.
And a TC6 chunk transaction (header + 64-byte data) needs `CS_N` held low for the
*whole* transfer; if the FLEXCOM SPI PLib toggles hardware CS per byte, drive
`CS_N` as a plain GPIO instead.

## 3. What the host MCU must implement

No TCP/IP, ARP, DHCP, or any IP stack. Three layers, none large:

1. **SPI transport** — one SERCOM in SPI-master mode + GPIO for `CS_N`,
   `IRQ_N` (external interrupt), and reset.
2. **OA TC6 chunk protocol** — the only real "driver." Unlike a UART, the SPI
   link is *not* byte-transparent: it exchanges fixed-size **chunks** (default
   64-byte data payload + a 4-byte header on TX / 4-byte footer on RX), with
   credit-based flow control (TX credits and RX-chunks-available are reported in
   each footer), plus a separate control-transaction format for register
   read/writes during init. Microchip publishes an open-source reference driver
   ([`MicrochipTech/oa-tc6-lib`](https://github.com/MicrochipTech/oa-tc6-lib),
   §References) small enough to run on an Arduino — the candidate base for both
   ends here.
3. **Minimal L2 framing** — the MAC-PHY only transmits *valid Ethernet frames*,
   so the host prepends a fixed 14-byte Ethernet header (dest MAC, src MAC,
   ethertype) to the payload. For this link the MACs are hardcoded and a custom
   ethertype carries the existing frame formats verbatim:
   - detector (fretboard, id 4) → marvin: the 17-byte data-stream frame (see fretboard SPEC).
   - command source → guitar (id 3): the 1-byte button bitmask — sent by marvin
     (timing pipeline) and/or directly by the active detector (fretboard's model,
     peer-to-peer).

   17 B + 14 B header = 31 B → a single chunk. The 1-byte command likewise. The
   existing UART framing (`0x03 … 0xFC`, `sample_seq`, etc.) can ride unchanged
   inside the Ethernet payload, so higher layers don't need to know the
   transport changed.

## 4. Resource budget — fretboard (PIC32CM6408PL10048, 64 KB flash / 8 KB SRAM)

Estimates; static allocation only (no malloc), per project rule.

| Resource | Estimate | Notes |
|---|---|---|
| Flash | ~6–10 KB | OA TC6 lib (~4–8 KB) + SPI glue + L2 framing. Reclaim the UART data path if the UART link is fully retired. |
| SRAM | ~1–2 KB | A couple of chunk buffers + one TX/RX frame buffer. |
| SERCOM | 1 (SPI master) | A free SERCOM alongside SERCOM1 (still UART for now). |
| GPIO | 2–3 | `CS_N`, `IRQ_N` (ext-int), reset. |
| CPU | negligible | One chunk per 240 Hz tick, interrupt-driven off `IRQ_N`; the TC0 ISR stays lean. |

SPI clock: the PIC32CM SERCOM SPI master maxes at GCLK/2 ≈ **12 MHz** on the 24 MHz
part (`BAUD=0`; not the chip's 25 MHz ceiling). Irrelevant for *throughput* (the wire
is 10 Mbps) but it **dominates command latency** — see §4.1. The PIC32CM followers
(fretboard + guitar) are brought up at `BAUD=11` ≈ 1 MHz (conservative); planned bump
toward the 12 MHz ceiling. This ≤12 MHz / 1 MHz figure is **follower-side only**:
marvin, the PLCA coordinator, drives its FLEXCOM SPI at **15 MHz** (`t1s_link.c`
`T1S_SPI_HZ`).

Everything fits comfortably inside 64 KB / 8 KB.

### 4.1 Command latency (fretboard → guitar)

End-to-end for the fretboard's inferred command to drive the guitar GPIO, by stage
(worst case):

| Stage | Worst case | Notes |
|---|---|---|
| Command-ready quantization | ~4.2 ms | model output gated to the fretboard's 240 Hz tick — kept deliberately (the model is timing-fit to this grid; see fretboard journal 2026-06-17). |
| Fretboard SPI TX to MAC-PHY | ~0.5–1.1 ms | 1 TC6 chunk = 68 B; **0.55 ms @ 1 MHz**, ~45 µs @ 12 MHz. +1 chunk if a data frame is queued ahead. |
| PLCA media access | ~0.2–0.5 ms | wait for the node's transmit opportunity (8-slot cycle, 3 active); +1 cycle if behind the data frame. **Not** the bottleneck. |
| Wire (10 Mbps) | ~0.06 ms | 60-byte min frame. |
| Guitar SPI RX + apply | ~0.6 ms | 1 chunk read; **0.55 ms @ 1 MHz**, ~45 µs @ 12 MHz. GPIO assert instant. |
| **Total** | **~6–7 ms** (≈2–3 ms typical) | |

The two off-wire terms dominate: the 240 Hz tick (firmware, intentionally kept) and
the **1 MHz host SPI** (~0.55 ms/chunk each end). Raising both nodes' SPI to 12 MHz
removes ~1 ms; PLCA media access is already sub-ms. The command is edge-triggered
(sent on change, not waiting for the 50 ms refresh), so a new note isn't delayed by
the refresh. Analytical estimate — validate by scoping fretboard `SetCommand` →
guitar GPIO apply.

## 5. marvin side

marvin runs **FreeRTOS + Harmony (no Linux)**, so it does *not* get a free
netdev — it needs its own TC6 driver. The TC6 service runs as a task woken by
the LAN8651 `IRQ_N` (a PIO external interrupt → `xSemaphoreGiveFromISR`, the
same pattern as `fretboard_link.c`'s RX path). The plan **adapts
[`MicrochipTech/oa-tc6-lib`](https://github.com/MicrochipTech/oa-tc6-lib)** (its
hardware-agnostic core + thin Harmony FLEXCOM-SPI glue), rather than writing a
TC6 from scratch — proven chunk/credit logic, and a **single portable `oa_tc6`
layer shared between marvin (Cortex-A5) and fretboard (Cortex-M0+)** is itself a
tidy demonstration that the OPEN Alliance SPI driver scales across the family.
marvin resources are not a concern. Hand-authored T1S code lives in
`firmware/marvin/default/src/net/t1s/`, kept out of the MCC-generated `config/`
tree and added via `user.cmake` (same discipline as `perf_log`/`console`).

## 6. PoDL — "dumb" (no negotiation)

PoDL is **not in the LAN8651** — the MAC-PHY only moves data. PoDL is external
coupling circuitry around the pair, and it is **transparent to both MCUs** (zero
firmware footprint, the §4 budget is unaffected):

- **PSE side (marvin):** a fixed-voltage source + coupling inductors that inject
  DC onto the pair.
- **PD side (fretboard):** coupling inductors to extract DC + a regulator down to
  the PIC32CM rail.
- **Dumb / unmanaged:** no SCCP classification or negotiation handshake. Fixed
  supply voltage, fixed point-to-point pair, decided at design time. Simplest
  PoDL form; appropriate because the topology is fixed and both ends are ours.

Microchip has PoDL reference designs that pair with the LAN865x EVBs, which also
helps the demonstration story.

## 7. Topology — PLCA multidrop, marvin as coordinator

**Decided (2026-06-16): PLCA from the start, multi-node.** The system is expected
to grow several nodes on one pair — multiple guitars and phototransistor
fretboard-detector nodes coexisting — so PLCA multidrop is a requirement, not just
headroom. marvin is the **PLCA coordinator (ID 0)**; every other node is a
follower. The single marvin↔fretboard link is brought up first, but the
addressing and demux are designed for N from the start so a new node joins with a
table entry, not a transport rewrite.

### 7.1 Addressing (static — no discovery, keeps the M0+ node dumb)

Followers are typed by **node class** (top-level [`SPEC.md`](../SPEC.md) §2): **detector**
nodes are RX sources (each maps to a marvin `detector_id`); **guitar** (actuator) nodes are
command TX targets. marvin selects the active node of each class.

**Target id assignment** (canonical — the whole bus renumbers to this):

| Node | Class | PLCA ID | MAC (locally administered) | Notes |
|---|---|---|---|---|
| marvin | coordinator | 0 | `02:00:00:00:00:00` | beacons the PLCA cycle; selects active detector + guitar |
| fauxmote(s) | controller | 1–2 | `02:00:00:00:00:0k` | Wiimote emulator; receives mf_proto slices on `0x88B7`, sends `STATUS` uplink. One PLCA node per fauxmote, at most two (id build-configurable, default 1) |
| guitar | guitar (actuator) | 3 | `02:00:00:00:00:03` | receives the 1-byte command bitmask (from marvin or a detector) |
| fretboard | detector | 4 | `02:00:00:00:00:04` | photo-ADC stream → `detector_id`; also commands the guitar directly |
| beatbox | (future) | 5 | `02:00:00:00:00:05` | reserved — beat-signal source for lemmy / lightshow |
| lemmy | animation | 6 | `02:00:00:00:00:06` | animated guitar puppet, 2 R/C servos (neck nod + jaw); **bring-up** (base MCC scaffolded, T1S follower next) |
| lightshow | lightshow | 7 | `02:00:00:00:00:07` | LED lighting node — drives LEDs / lamps in time to the music; **bring-up** (bootstrapped from lemmy's T1S follower, LED output next) |

> **Transition status (2026-07-28).** **fauxmote** (id 1, range 1–2), the **guitar**
> (id 3), and the **fretboard** (id 4) are all on the target scheme in source. The guitar
> id 3 is set at all three points that address it (guitar firmware, marvin's coordinator
> node table, and the fretboard's peer-to-peer guitar target); the fretboard id 4 is set
> at its two points (its firmware `T1S_NODE_ID` + marvin's coordinator table entry). Each
> node was last *flashed/verified* at its old id (guitar 2, fretboard 1), so re-flash each
> node **together with marvin** for the new ids to take effect on the wire (marvin filters
> by src MAC). Until the fretboard is re-flashed, a *running* fretboard is still physically
> at id 1, so hold off co-busing a default-id-1 fauxmote with a not-yet-reflashed fretboard.

- One **custom ethertype** `0x88B5` (IEEE local/experimental range; no
  registration needed for a private bus) carries the fretboard/guitar payloads verbatim.
- A **second data ethertype** `0x88B7` carries the fauxmote (controller) channel — the
  mf_proto message layer ([`docs/marvin-fauxmote-link.md`](marvin-fauxmote-link.md) §4–§5)
  as `[TYPE][payload…]` in the frame body. Kept distinct from `0x88B5` so the coordinator
  demultiplexes controller traffic apart from the detector/guitar bitmask.
- A **beat-frame ethertype** `0x88B8` carries beatbox's (id 5) compact beat
  frame to the lighting/animation nodes. Unlike the point-to-point traffic above this is a
  **one-to-many broadcast** (see the addressing note below): beatbox sends one frame to
  `FF:FF:FF:FF:FF:FF`, and every interested node (lightshow, lemmy) accepts
  it and dispatches on the ethertype.
- A **node-control ethertype** `0x88B9` carries typed commands **unicast** to a follower
  to tune its local behavior. Payload is `[opcode, arg]`; the opcode namespace is **per-node,
  disambiguated by destination MAC** (like `0x88B5`'s data grammar differing per node), so one
  ethertype serves every node's control channel and a new node reuses it rather than minting a
  fresh ethertype — the typed-control *grammar* is what earns `0x88B9` over `0x88B5`'s fixed
  positional payloads, not the node. marvin stages commands per-opcode and flushes one frame per
  service pass; the follower applies each on RX. Defined namespaces:
    - **lemmy** (`02:..:06`) — beat-nod tuning: `0x01` nod enable (arg 0|1), `0x02` nod trim
      (arg int8), `0x03` oscillator (arg 0|1); the same tunables as lemmy's local `nod` CLI,
      driven from marvin's `lemmy nod|trim|osc`. (Manual servo positioning stays on `0x88B5`;
      `nod off` frees the neck so that path takes effect.)
    - **lightshow** (`02:..:07`) — LED output: `0x01` output enable (arg 0|1), driven from
      marvin's `lightshow on|off` → `BeatShow_SetEnabled`. Disabling blanks the strands.
  Opcode space in each namespace is left open for future control (scripted gestures / jaw for
  lemmy; scenes / brightness for lightshow).
- A static **node table** on marvin maps `{PLCA ID, MAC, node_type}` → the bus
  `detector_id` (and the actuator target for TX). The single fretboard keeps
  `detector_id = 1`, matching today's `adc_fretboard` bus slot. No discovery /
  enumeration protocol — the table is compiled in.
- The 17-byte fretboard→marvin frame and the 1-byte marvin→fretboard bitmask ride
  inside the Ethernet payload unchanged; marvin demuxes incoming frames by src MAC
  and addresses outgoing ones to a specific node.
- **Unicast is the default; broadcast is for one-to-many producer streams.** The
  detector/guitar/controller traffic is all point-to-point (unicast, src-MAC demux). A
  producer whose stream is consumed by several nodes at once — beatbox's beat frame being the
  first — sends **once to broadcast** (`FF:FF:FF:FF:FF:FF`) rather than N unicast copies; the
  shared bus delivers the single frame to every node and the follower MAC filter already
  accepts broadcast (it is not promiscuous — self-MAC + broadcast only), so consumers need no
  filter change and select by ethertype. Broadcast uses one PLCA transmit opportunity, same as
  a unicast frame. Adding a consumer is then a change on *its* side, not another TX on the
  producer's.

### 7.2 Presence heartbeat

PLCA has **no node discovery**, so the coordinator can't enumerate who's on the
bus. A lightweight application heartbeat fills that gap: each follower
periodically (≈500 ms) sends a small frame to the coordinator
(`02:00:00:00:00:00`) under a **separate ethertype `0x88B6`** so marvin routes it
apart from data/command traffic (`0x88B5`). marvin stamps a per-node "last seen"
on receipt and reports it via the `nodes` console command (present = a heartbeat
within ~2 s).

Payload (8 bytes): `version(1)`, `node_type(1)` (1=detector, 2=guitar, 3=controller,
4=animation, 5=lightshow, 6=beat source), `node_id(1)`, `flags(1)` (bit0 = follower synced),
`seq(u32 LE)`. marvin derives the node from the **src MAC** via its static node table, not by
decoding the `node_type` byte — so the payload type is informational (seq enables drop detection).
marvin's `nodes` display recognizes `lemmy` (id 6), `lightshow` (id 7), and `beatbox` (id 5) as
table entries; decoding the payload `node_type` byte remains an unneeded marvin-side follow-up.

## 8. Transport coexistence

The marvin `FretboardLink_{Initialize,Send,IsConnected}` API is preserved; the
actuator command now targets the guitar node (`T1SLink_SendToGuitar`). UART and
T1S transports live behind a `MARVIN_FRETBOARD_TRANSPORT={UART,T1S}` **build flag**.
The `t1s` branch builds **T1S by default**; UART stays one (commented) line away in
`user.cmake` as a fallback during the fretboard's own transition.

The **fauxmote (controller) link** is behind the same kind of seam:
`MARVIN_FAUXMOTE_TRANSPORT={UART,T1S}` in `net/fauxmote/fauxmote_link.h`, **default T1S**.
On T1S the fauxmote channel is not a second interface — marvin has a single LAN8651, so
`fauxmote_link` rides the shared `net/t1s` MAC-PHY as a *controller channel*: TX stages
through `T1SLink_SendToController` (framed `[dst=ctrl][src=coord][0x88B7][TYPE][payload]`
and flushed by the T1S service task from a small static FIFO, interleaved with the
`0x88B5` guitar command), and the `STATUS` uplink arrives via a registered
`T1SLink_ControllerHandler`. `T1SLink_Initialize` is idempotent so the fretboard-T1S and
fauxmote-T1S paths can both call it. `MARVIN_FAUXMOTE_TRANSPORT=0` selects the dedicated
FLEXCOM5 UART point-to-point path instead (§ [`marvin-fauxmote-link.md`](marvin-fauxmote-link.md)).

## 9. Decisions & open items

**Built and working (2026-06-17):** marvin coordinator (id 0) ↔ guitar follower
over T1S — command TX, presence heartbeat, and link/`nodes` diagnostics on both
ends. First brought up at guitar id 2; renumbered to the target **id 3** in source
(2026-07-28, §7.1) — re-flash guitar + marvin for it to take effect. The earlier
prerequisites (a FLEXCOM/SERCOM in SPI-master mode via MCC; the `oa-tc6-lib`
submodule) are resolved.

Open / future:

- **Fretboard node (id 4) — firmware written 2026-06-17** (`t1s_detector.{c,h}`,
  T1S-only). Sense+actuate: streams the 17-byte data frame to marvin (RX/detector path
  + node-table slot already in place) **and** sends its model's inferred command
  directly to the guitar (id 3). MCC config done. First brought up at id 1; renumbered to
  the target **id 4** in source (2026-07-28, §7.1) — re-flash fretboard + marvin for it to
  take effect. **Remaining:** build-wiring + on-hardware bring-up (banner
  `LAN8651 up … PLCA follower id=4/8`; confirm the
  `FRETBOARD_RAW` rate holds ≈240 Hz — data + command + heartbeat now share the node's
  PLCA TX).
- **Active-source arbitration.** Both marvin and the fretboard can address the guitar
  (`02:..:03`); the guitar applies whoever transmitted last. Need marvin-side
  active-detector/active-guitar selection so exactly one source drives at a time
  (today: single guitar → `node_for_type(GUITAR)`; the fretboard's SW0 is an interim
  manual arm).
- **PoDL** supply voltage + PD-side regulator topology (BOM, not firmware) — not
  built; the link is separately powered for now.
- Magnetics-free coupling component selection on the Sensor-LCD5 PCB.

## References

- **OA TC6 driver (candidate base, both ends):**
  [`MicrochipTech/oa-tc6-lib`](https://github.com/MicrochipTech/oa-tc6-lib) —
  Microchip's open-source OPEN Alliance TC6 (10BASE-T1x MAC-PHY Serial
  Interface) library: SPI chunk protocol + control transactions, used as the
  shared portable layer (§3, §5).
- LAN8650/1 (LAN8651B1) datasheet — 10BASE-T1S MAC-PHY Ethernet Controller with
  SPI, on [Microchip online docs](https://onlinedocs.microchip.com/) /
  the LAN8651 product page.
- OPEN Alliance *10BASE-T1x MAC-PHY Serial Interface* specification (TC6), v1.1.
