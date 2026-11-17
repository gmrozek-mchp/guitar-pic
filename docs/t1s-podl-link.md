# T1S inter-node link — 10BASE-T1S + PoDL

Engineering detail for the marvin↔node link over **10BASE-T1S single-pair Ethernet**
(with planned "dumb" Power-over-Data-Lines). System-level summary and rationale live
in [`../SPEC.md`](../SPEC.md) §2/§6; this document holds the detail.

Status: **built and working.** marvin (PLCA coordinator, id 0) runs the bus; every PIC32CM /
dsPIC / ESP32 follower uses a LAN8651 MAC-PHY over SPI + the OPEN Alliance TC6 driver
([`third_party/oa-tc6-lib`](../third_party/oa-tc6-lib)). Followers on the bus: fauxmote (id 1,
the game input), guitar (id 3), fretboard (id 4), beatbox (id 5), lemmy (id 6), lightshow (id 7)
— see §7.1. Each sends a presence heartbeat (`0x88B6`, §7.2); marvin's `nodes` console command
and bus-stats screen report them. **PoDL is design-direction only** (not built) — nodes are
powered separately; a PoDL sink board is in progress under [`hardware/t1s-sink/`](../hardware/t1s-sink/).

> **Command-plane note:** two senders can address fauxmote and the guitar — marvin's CV pipeline
> and the fretboard (peer-to-peer). Neither node filters by source, so marvin's
> `active <cv|fretboard>` plus its per-song gameplay window decide which one drives (§7.1,
> `0x88B9` fretboard arm). There is one guitar node, so marvin picks it with
> `node_for_type(GUITAR)`; active-guitar selection is not built.

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
  just an incidental benefit. Bandwidth is *not* a motivation — the original
  500 kbaud UART link carried the ~4 KB/s payload with ~12× headroom.

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
   §References) small enough to run on an Arduino — used on every node here.
3. **Minimal L2 framing** — the MAC-PHY only transmits *valid Ethernet frames*,
   so the host prepends a fixed 14-byte Ethernet header (dest MAC, src MAC,
   ethertype) to the payload. For this link the MACs are hardcoded and a custom
   ethertype carries the existing frame formats verbatim:
   - detector (fretboard, id 4) → marvin: the 18-byte data-stream frame (see fretboard SPEC).
   - command source → guitar (id 3): the 1-byte button bitmask — sent by marvin
     (timing pipeline) and/or directly by the active detector (fretboard's model,
     peer-to-peer).

   18 B + 14 B header = 32 B → a single chunk. The 1-byte command likewise. The
   frame keeps its `0x03 … 0xFC` delimiters and `sample_seq` inside the Ethernet payload.

## 4. Resource budget — fretboard (PIC32CM6408PL10048, 64 KB flash / 8 KB SRAM)

Estimates; static allocation only (no malloc), per project rule.

| Resource | Estimate | Notes |
|---|---|---|
| Flash | ~6–10 KB | OA TC6 lib (~4–8 KB) + SPI glue + L2 framing. |
| SRAM | ~1–2 KB | A couple of chunk buffers + one TX/RX frame buffer. |
| SERCOM | 1 (SPI master) | SERCOM0; SERCOM1 stays the debug-console UART. |
| GPIO | 2–3 | `CS_N`, `IRQ_N` (ext-int), reset. |
| CPU | negligible | One chunk per 240 Hz tick, interrupt-driven off `IRQ_N`; the TC0 ISR stays lean. |

SPI clock: the PIC32CM SERCOM SPI master maxes at GCLK/2 = **12 MHz** on the 24 MHz
part (`BAUD=0`; not the LAN8651's 25 MHz ceiling). All PIC32CM followers (fretboard,
guitar, lemmy, lightshow) run SERCOM0 at `BAUD=0` = 12 MHz. marvin drives its FLEXCOM
SPI at **15 MHz** (`t1s_link.c` `T1S_SPI_HZ`).

Everything fits comfortably inside 64 KB / 8 KB.

### 4.1 Command latency (fretboard → guitar)

End-to-end for the fretboard's inferred command to drive the guitar GPIO, by stage
(worst case):

| Stage | Worst case | Notes |
|---|---|---|
| Command-ready quantization | ~4.2 ms | model output gated to the fretboard's 240 Hz tick — kept deliberately (the model is timing-fit to this grid; see fretboard journal 2026-06-17). |
| Fretboard SPI TX to MAC-PHY | ~45–90 µs | 1 TC6 chunk = 68 B ≈ 45 µs @ 12 MHz. +1 chunk if a data frame is queued ahead. |
| PLCA media access | ~0.2–0.5 ms | wait for the node's transmit opportunity (8-slot cycle); +1 cycle if behind the data frame. |
| Wire (10 Mbps) | ~0.06 ms | 60-byte min frame. |
| Guitar SPI RX + apply | ~50 µs | 1 chunk read @ 12 MHz. GPIO assert instant. |
| **Total** | **~5 ms** worst case | |

The 240 Hz tick quantization dominates; the transport adds well under 1 ms. The same path
applies to the fretboard's `0x88B7` GUITAR message to fauxmote, which then adds its own
Bluetooth report latency. The command is edge-triggered
(sent on change, not waiting for the 50 ms refresh), so a new note isn't delayed by
the refresh. Analytical estimate — validate by scoping fretboard `SetCommand` →
guitar GPIO apply.

## 5. marvin side

marvin runs **FreeRTOS + Harmony (no Linux)**, so it does *not* get a free
netdev — it needs its own TC6 driver. The TC6 service runs as a task woken by
the LAN8651 `IRQ_N` (a PIO external interrupt → `xSemaphoreGiveFromISR`, the
same pattern as `fretboard_link.c`'s RX path). It **adapts
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
follower. A new node joins with a table entry, not a transport rewrite.

### 7.1 Addressing (static — no discovery, keeps the M0+ node dumb)

Followers are typed by **node class** (top-level [`SPEC.md`](../SPEC.md) §2): **detector**
nodes are RX sources (each maps to a marvin `detector_id`); **guitar** (actuator) nodes are
command TX targets. marvin selects the active node of each class.

**Id assignment** (canonical; matches `T1S_NODE_ID` in each node and marvin's `s_nodes[]`;
`node_type` is the heartbeat byte, §7.2):

| Node | Class | PLCA ID | MAC (locally administered) | `node_type` | Notes |
|---|---|---|---|---|---|
| marvin | coordinator | 0 | `02:00:00:00:00:00` | — | beacons the PLCA cycle; selects the active detector |
| fauxmote | controller | 1 | `02:00:00:00:00:01` | 3 | Wiimote emulator; mf_proto on `0x88B7`, `STATUS` uplink. Single controller node; id is build-configurable (`CONFIG_FAUXMOTE_T1S_NODE_ID`, default 1). Row compiled into marvin only when `MARVIN_FAUXMOTE_TRANSPORT` = T1S |
| guitar | guitar (actuator) | 3 | `02:00:00:00:00:03` | 2 | receives the 1-byte bitmask (from marvin or the fretboard) |
| fretboard | detector | 4 | `02:00:00:00:00:04` | 1 | photo-ADC stream → `detector_id`; drives fauxmote + guitar directly when armed |
| beatbox | beat source | 5 | `02:00:00:00:00:05` | 6 | dsPIC33AK; broadcasts the `0x88B8` beat frame |
| lemmy | animation | 6 | `02:00:00:00:00:06` | 4 | puppet, 2 R/C servos (neck nod + jaw) |
| lightshow | lighting | 7 | `02:00:00:00:00:07` | 5 | WS2812 strands driven from the beat frame |

Id 2 is unused. PLCA `NODE_COUNT` = 8 on every node.

- One **custom ethertype** `0x88B5` (IEEE local/experimental range; no
  registration needed for a private bus) carries the fretboard/guitar payloads verbatim.
- A **second data ethertype** `0x88B7` carries the fauxmote (controller) channel — the
  mf_proto message layer ([`docs/marvin-fauxmote-link.md`](marvin-fauxmote-link.md) §4–§5)
  as `[TYPE][payload…]` in the frame body (no SOF/LEN/CRC — the MAC-PHY FCS covers integrity).
  Kept distinct from `0x88B5` so the coordinator demultiplexes controller traffic apart from the
  detector/guitar bitmask. **Two senders** address fauxmote (id 1) on this channel: marvin's CV
  pipeline (`net/fauxmote/fauxmote_link.c`) and the **fretboard detector** (`02:..:04`), which
  drives a `MF_MSG_GUITAR` message peer-to-peer directly to fauxmote whenever it is armed — this is
  the game-critical path (fauxmote is what plays the Wii). fauxmote does not filter by source MAC;
  `active <cv|fretboard>` on marvin ensures exactly one sender is live (see the `0x88B9` fretboard
  arm bullet). The fretboard pins whammy to `MF_WHAMMY_REST` / aux 0 today (both reserved for a
  future whammy/IMU source via a generic `(type, payload, len)` sender).
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
    - **guitar** (`02:..:03`) — button output: `0x01` output enable (arg 0|1), driven from
      marvin's `guitar on|off` → `T1SFollower_SetOutputEnabled`. Disabling releases every button
      GPIO and holds them released. Gating at the node rather than by muting marvin's `0x88B5`
      command is what makes it authoritative: inside a song with the fretboard selected, the
      **fretboard drives the guitar peer-to-peer and marvin is silent**, so a marvin-side gate
      would not stop actuation in exactly the window that matters.
    - **lemmy** (`02:..:06`) — beat-nod tuning: `0x01` nod mode (arg 0 = off, 1 = nod every beat,
      2 = **auto**: occasional bursts on a tempo the engine is confident of), `0x02` nod trim
      (arg int8), `0x03` oscillator (arg 0|1), `0x05` auto confidence floor (arg 0-100); the same
      tunables as lemmy's local `nod` CLI, driven from marvin's `lemmy nod|trim|osc`. A gameplay
      window commands mode 2; mode 1 is the bench override. Plus `0x04` **servo output enable** (arg 0|1) →
      `Servo_SetEnabled`, driven from marvin's `lemmy output on|off`. The two are different knobs
      and deliberately so: `nod enable` only detaches the neck from the beat engine (freeing it
      for a manual `0x88B5` position), while `output enable` gates the single hardware write in
      `servo.c` and therefore stops **every** source — the nod, marvin's positions, and
      beatbox's. Disabling parks both servos at neutral first.
    - **lightshow** (`02:..:07`) — LED output: `0x01` output enable (arg 0|1), driven from
      marvin's `lightshow on|off` → `BeatShow_SetEnabled`. Disabling blanks the strands.
    - **fretboard** (`02:..:04`) — `0x01` arm (arg 0|1) + `0x02` stream (arg 0|1) + `0x03` model
      (arg 0..4) + `0x04` teacher (arg = marvin's 7-bit CV command, latched into the data frame's
      `commanded_mask`; sent automatically), driven from marvin's `fretboard arm|disarm` / `fretboard stream on|off` /
      `fretboard model <difficulty>`. Arm tracks marvin's **per-song gameplay window** —
      marvin asserts it only while a song is actually playing *and* the fretboard is the selected
      detector, disasserts it when the song ends or `active cv` is chosen; **it gates both of the
      fretboard's outputs** — the `0x88B7` GUITAR message to fauxmote (game-critical) and the `0x88B5`
      bitmask to the guitar node (indicator). `active <cv|fretboard>` *selects* the detector;
      `GameTiming_SetEnabled` (marvin's in-song choke point) is what arms/disarms the fretboard here
      **and** gates marvin's own CV output off/on (`Detector_FretboardDriving()` — the shared predicate
      `in-song && active==fretboard` — drives `FretboardLink`'s guitar-node output + `fx_tx_task`), so
      exactly one detector drives fauxmote and only inside a song (outside a song marvin owns the
      controller for menu nav / manual control). The arm state is shared with the node's local `arm` CLI command
      (last writer wins, no lockout — arming locally while `active==cv` can double-drive fauxmote, a
      documented manual-override hazard). (Disarming sends one final all-released frame on each output
      then goes silent, so the fretboard never contends for the guitar or fauxmote with marvin; marvin
      emits one release frame to fauxmote on the cv→fretboard handoff so no note sticks.) Stream gates the `0x88B5` data feed to marvin and **boots disabled**
      — marvin turns it on when it wants the logging / edge-ai capture. Model selects the on-device
      inference model — one per Guitar Hero difficulty (`easy`/`medium`/`hard`/`expert`, indices
      0..3) plus a reserved `auto` slot (index 4); shared with the node's local `model` CLI command.
      Only `hard` is trained, so all slots currently resolve to the hard model. marvin also pushes
      the song-select difficulty on every run start, overriding a manual `fretboard model`.
  Opcode space in each namespace is left open for future control (scripted gestures / jaw for
  lemmy; scenes / brightness for lightshow; per-fret sensitivity for the detector).

  **Durability.** The channel is fire-and-forget at L2 — no ACK — and a follower boots on its own
  compiled-in defaults, so "marvin sent it once" is not the same as "the node is in that state".
  Three rules close the gap, all on the coordinator side:
    1. **Retry until sent.** A staged opcode clears its dirty flag only when `send_to_node`
       actually accepted the frame; a refusal leaves it staged for the next service pass.
    2. **Resync on (re)appearance.** When a node's heartbeat shows it arriving — never seen, or its
       presence window had lapsed, or its cumulative counters went *backwards* (which only happens
       when the node restarted, and catches a reboot too quick to look absent) — marvin re-pushes
       every opcode that node has ever been told. This is the only mechanism covering opcodes the
       node can't report back, i.e. the fretboard's arm/stream/model/teacher.
    3. **Reconcile against the echo.** Each actuator node reports its output gate in the heartbeat
       (§7.2 flags bit1). When the report disagrees with what marvin last commanded, marvin
       re-pushes that opcode, rate-limited to ~1 Hz per node. This is what catches a lost frame or
       a *local* change on the node (its own `output` CLI), neither of which looks like an arrival.
       With everything agreeing it sends nothing, so the control channel is off the bus in steady
       state; sustained disagreement (≈3 s) also logs a warning, since a node whose firmware
       predates the echo reports 0 forever and would otherwise be re-pushed silently.
  Marvin's wanted state for the three actuator enables is owned by `actuator/actuator_enable.c`
  and defaults to **all enabled**, so a cold boot plays without operator interaction.
- A static **node table** on marvin maps `{PLCA ID, MAC, node_type}` → the bus
  `detector_id` (and the actuator target for TX). The single fretboard keeps
  `detector_id = DETECTOR_FRETBOARD`. No discovery /
  enumeration protocol — the table is compiled in.
- marvin demuxes incoming frames by src MAC and addresses outgoing ones to a specific node.
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

Payload — **v1 (8 bytes, legacy)**: `version(1)`, `node_type(1)` (1=detector, 2=guitar,
3=controller, 4=animation, 5=lightshow, 6=beat source), `node_id(1)`, `flags(1)`
(bit0 = follower synced, bit1 = **output enabled**), `seq(u32 LE)`. marvin derives the node from the **src MAC** via its
static node table, not by decoding the `node_type` byte — so the payload type is informational
(seq enables drop detection). marvin's `nodes` display recognizes `lemmy` (id 6), `lightshow`
(id 7), and `beatbox` (id 5) as table entries; decoding the payload `node_type` byte remains an
unneeded marvin-side follow-up.

**`flags` bit1 is meaningful per node type.** Only the three actuator nodes — guitar, lemmy,
lightshow — set it, each reporting its own output gate (`T1SFollower_IsOutputEnabled`,
`Servo_IsEnabled`, `BeatShow_IsEnabled` respectively). Every other node leaves it 0 and marvin
never consults it for them. It is the return leg of the `0x88B9` output-enable command: it turns a
fire-and-forget push into a closed loop, letting marvin reconcile (§7.1) and letting the dashboard
show what an actuator *is* rather than what it was asked to be.

**v2 (20 bytes) — telemetry extension** (for the bus-statistics UI, `nodes`/`t1s` console).
`version` is bumped to `2` and the 8-byte header is followed by the node's own cumulative
counters, all little-endian: `tx_count(u32)`, `rx_count(u32)`, `crc_err(u16)`, `sym_err(u16)`
(offsets 8, 12, 16, 18 in the payload). `tx_count`/`rx_count` are frames the node has
sent/received; `crc_err`/`sym_err` are the node's own MAC-PHY FCS / loss-of-framing event tallies
(counted in the follower's `TC6Regs_CB_OnEvent` — there is no per-source error counter, so each
node reports its *own* receiver's errors). The coordinator gates on payload length
(`payload_len >= 20`), so a not-yet-reflashed follower still sending a v1 8-byte heartbeat simply
reports zeros for the new fields — the extension rolls out node-by-node with no flag day. Frames are
padded to the 60-byte Ethernet minimum, so the extra 12 bytes add no wire cost. marvin derives
per-node TX/RX **rates** (frames/s) from the reported cumulative deltas once a second, and its own
(coordinator) row from its local counters + its own `TC6Regs_CB_OnEvent` tallies.

## 8. Transport coexistence

marvin's `FretboardLink_*` API targets the guitar node (`T1SLink_SendToGuitar`) for
commands. UART and T1S transports live behind `MARVIN_FRETBOARD_TRANSPORT={UART,T1S}`:
the header default is UART, and `cmake/marvin/default/user.cmake` sets it to T1S (`=1`).
The fretboard firmware itself is T1S-only, so the UART setting is legacy.

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

## 9. Wire timing and bus occupancy

What marvin's bus-stats screen reports, and why it is measured the way it is.
All times in bit times (**BT** = 100 ns at 10 Mbit/s).

**A PLCA bus cycle** (802.3cg Clause 148) is a BEACON followed by one transmit
opportunity per configured node — `NODE_COUNT` = 8 here:

| element | BT | note |
|---|---|---|
| BEACON | 20 | five N symbols, 2 µs; coordinator only |
| short silence | 16 | after the BEACON, and after every transmission |
| unused TO | 32 | `PLCA_TOTMR` default, 3.2 µs — a full silent slot |
| used TO | frame | transmission, then a short silence |
| preamble + SFD | 64 | 8 bytes ahead of each frame (SSD substituted in) |

So an **idle cycle is 20 + 16 + 8×32 = 292 BT = 29.2 µs**, giving a ceiling of
~34,250 cycles/s. That floor is spent whether or not anyone transmits, which is
why "utilization" needs saying precisely.

**Utilization** = the share of line time carrying frames, and nothing else. On an
idle bus it is ~0% by construction; the remainder is beacon and mandated silence,
not spare capacity that a faster link would recover. A saturating detector stream
(1000 frames/s × 526 B) reads 43%.

    util = (frame bytes + 8 per frame) × 8 / 10,000,000   per second

The **+8 bytes per frame is the preamble and SFD**. The MAC generates them, so
they appear in no frame length and have to be added explicitly — at ~1000
frames/s that is ~8 kB/s, worth ~0.6 percentage points. The reported byte rate
includes them for exactly this reason: **utilization is `bytes/s × 8 / 10 Mbit`
by hand**, at any traffic level, and a byte figure that excluded framing would
silently fail that check.

**TO occupancy** is the headroom figure. A PLCA bus runs out of room when a node
wants to transmit more often than its TO comes around — not when the bit rate
nears 10 Mbit/s.

**Everything is measured on marvin, from byte counts.** Its MAC is promiscuous
(`TC6Regs_Init(..., promiscuous=true)`), so it observes every frame on the segment
including ones it is not party to, and it never hears its own transmissions —
so `rx + tx` on marvin is the whole bus, counted exactly once. Two consequences
worth knowing:

- **Do not sum the per-node frame rates to get bus load.** A frame occupies the
  medium once but appears in its sender's `tx` and in every listener's `rx`, so
  the sum roughly doubles the truth. The per-node rates are for the node table.
- **The two directions report different lengths for the same frame.** On receive,
  `len` is the full wire frame *including pad and FCS* — `MAC_NCFGR.RFCS` defaults
  to 0, so the MAC relays the FCS rather than stripping it. On transmit the host
  passes neither: the MAC pads to 60 bytes and appends the FCS itself
  (`QTXCFG.MACFCSDIS` defaults to 0). A tx length has to be grown to match.

**Not used yet:** the LAN8651 has hardware PLCA event counters — `CTRCTRL`
(0x0020) `TOCTRE`/`BCNTRE` enable `TOCNTH/L` (transmit opportunities offered) and
`BCNCNTH/L` (beacons), both clear-on-read. Polling `TOCNT` would replace the
derived cycle rate with a measured one and double as a PLCA-health check. There
are **no octet counters** in the MAC statistics block (MMS1 0x288–0x293 is frame
and error counts only), so byte totals have to be accumulated in software
regardless.

## 10. Open items

- **Active-guitar selection.** One guitar node today; marvin targets it via
  `node_for_type(GUITAR)`. A second guitar variant would need a selector like `active`.
- **Local arm hazard.** The fretboard's local `arm` command shares the flag marvin sets;
  arming locally while `active cv` double-drives fauxmote (§7.1).
- **PoDL** supply voltage + PD-side regulator topology (BOM, not firmware) — not built;
  sink board in progress in [`hardware/t1s-sink/`](../hardware/t1s-sink/).
- **PLCA hardware counters** (`TOCNT`/`BCNCNT`, §9) not yet polled.

## References

- **OA TC6 driver (all nodes):**
  [`MicrochipTech/oa-tc6-lib`](https://github.com/MicrochipTech/oa-tc6-lib) —
  Microchip's open-source OPEN Alliance TC6 (10BASE-T1x MAC-PHY Serial
  Interface) library: SPI chunk protocol + control transactions, the shared
  portable layer (§3, §5).
- LAN8650/1 (LAN8651B1) datasheet — 10BASE-T1S MAC-PHY Ethernet Controller with
  SPI, on [Microchip online docs](https://onlinedocs.microchip.com/) /
  the LAN8651 product page.
- OPEN Alliance *10BASE-T1x MAC-PHY Serial Interface* specification (TC6), v1.1.
