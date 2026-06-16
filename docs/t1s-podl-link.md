# fretboard ↔ marvin link — 10BASE-T1S + PoDL

Low-level design for moving the fretboard ↔ marvin link from a FLEXCOM2/SERCOM
UART pair to **10BASE-T1S single-pair Ethernet with "dumb" Power-over-Data-Lines
(PoDL)**. System-level summary and rationale live in [`../SPEC.md`](../SPEC.md)
§6; this document holds the engineering detail.

Status: **scoped, not yet built — marvin-side coding held on two prerequisites.**
The link today is FLEXCOM1 UART (marvin) / SERCOM1 UART (fretboard). Nothing here
is flashed. As of 2026-06-16 the scope widened from a 2-node swap to a **multi-node
PLCA bus** (several guitars + phototransistor detector nodes on one pair) and the
key forks are settled — see §7.1 (addressing), §9 (decisions/open items) and the
marvin journal 2026-06-16 entry. Coding waits on: (1) a free FLEXCOM regenerated in
**SPI-master mode in MCC** (no SPI PLib exists yet — only USART/TWI), and (2)
`oa-tc6-lib` added on disk as a subproject.

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
   - fretboard → marvin: the 17-byte data-stream frame (see fretboard SPEC).
   - marvin → fretboard: the 1-byte button bitmask.

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

SPI clock: SERCOM SPI master maxes at GCLK/2 ≈ **12 MHz** on the 24 MHz part
(not the chip's 25 MHz ceiling) — irrelevant for this data rate.

Everything fits comfortably inside 64 KB / 8 KB.

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

| Node | Class | PLCA ID | MAC (locally administered) | Notes |
|---|---|---|---|---|
| marvin | coordinator | 0 | `02:00:00:00:00:00` | beacons the PLCA cycle; selects active detector + guitar |
| fretboard | detector | 1 | `02:00:00:00:00:01` | photo-ADC stream → `detector_id` 1 |
| guitar | guitar (actuator) | 2 | `02:00:00:00:00:02` | receives the 1-byte command bitmask |
| node *k* | (either) | *k* | `02:00:00:00:00:0k` | future detector/guitar variants |

- One **custom ethertype** `0x88B5` (IEEE local/experimental range; no
  registration needed for a private bus) carries the existing payloads verbatim.
- A static **node table** on marvin maps `{PLCA ID, MAC, node_type}` → the bus
  `detector_id` (and the actuator target for TX). The single fretboard keeps
  `detector_id = 1`, matching today's `adc_fretboard` bus slot. No discovery /
  enumeration protocol — the table is compiled in.
- The 17-byte fretboard→marvin frame and the 1-byte marvin→fretboard bitmask ride
  inside the Ethernet payload unchanged; marvin demuxes incoming frames by src MAC
  and addresses outgoing ones to a specific node.

## 8. Transport coexistence

The marvin `FretboardLink_{Initialize,Send,IsConnected}` API is preserved (with
`Send` gaining a node target). UART and T1S transports live behind a
`MARVIN_FRETBOARD_TRANSPORT={UART,T1S}` **build flag**, default UART: the working
FLEXCOM1 UART link stays intact during T1S bring-up and is cut over (and its data
path reclaimed) only once T1S is proven.

## 9. Decisions & open items

Settled 2026-06-16 (see marvin journal): multi-node design-for-N / build-one-link;
PLCA coordinator on marvin (§7); adapt `oa-tc6-lib` (§5); UART kept in parallel
behind a build flag (§8); develop against a LAN8651 EVB/Click on the SAM9X75
Curiosity.

Still open (non-blocking; settle during bring-up unless noted):

- **Prereq (blocking):** a free FLEXCOM regenerated in SPI-master mode in MCC with
  the EVB pinout — no SPI PLib exists yet.
- **Prereq (blocking):** `oa-tc6-lib` added on disk as a subproject.
- PoDL supply voltage and the PD-side regulator topology (BOM, not firmware).
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
