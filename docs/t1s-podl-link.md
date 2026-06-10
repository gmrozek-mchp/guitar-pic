# fretboard ↔ marvin link — 10BASE-T1S + PoDL

Low-level design for moving the fretboard ↔ marvin link from a FLEXCOM2/SERCOM
UART pair to **10BASE-T1S single-pair Ethernet with "dumb" Power-over-Data-Lines
(PoDL)**. System-level summary and rationale live in [`../SPEC.md`](../SPEC.md)
§6; this document holds the engineering detail.

Status: **direction, not yet built.** The link today is FLEXCOM2 UART (system
spec M2). Nothing here is flashed.

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

marvin runs **bare-metal (no Linux)**, so it does *not* get a free netdev — it
needs its own TC6 driver. The upside: a **single portable `oa_tc6` layer shared
between marvin (Cortex-A5) and fretboard (Cortex-M0+)**, which is itself a tidy
demonstration that the OPEN Alliance SPI driver scales across the family. marvin
resources are not a concern.

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

## 7. Topology: point-to-point vs PLCA multidrop

Recommended direction: **enable PLCA from the start** rather than strict
point-to-point. A 2-node PLCA bus already demonstrates the headline T1S feature
(collision-free multidrop on one pair) at no extra cost, and leaves the door
open to a second node sharing the same pair and power. Open until decided.

## 8. Open items

- PLCA vs pure point-to-point (see §7) — lean PLCA.
- PoDL supply voltage and the PD-side regulator topology (BOM, not firmware).
- Custom ethertype value and the two hardcoded MAC addresses.
- Whether to keep the UART link as a fallback during bring-up or cut over
  outright (affects whether SERCOM1's data path is reclaimed — §4).
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
