# guitar — Specification

> What guitar *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: working on hardware.** The T1S PLCA follower is up (chipRev read, link
> synced), receives marvin's command over T1S and actuates the Wii GPIOs, sends a
> presence heartbeat, and exposes a debug-UART CLI. Remaining work is the full
> multi-node system (G3) — see §6 and [`docs/journal.md`](docs/journal.md).

## 1. Purpose

guitar is the **Wii-guitar actuator node** on the marvin T1S bus. It receives a 1-byte
button bitmask from [marvin](../marvin/docs/spec.md) over 10BASE-T1S and drives a real Wii
guitar controller's buttons via open-drain GPIO. It does **no sensing and no game logic** —
it is purely the actuation half of what today's [fretboard](../fretboard/SPEC.md) firmware
does, lifted onto its own node.

This split lets sensing (detector nodes) and actuation (guitar nodes) live on separate nodes
of one PLCA bus, with marvin selecting the active one of each class (see top-level
[`SPEC.md`](../../SPEC.md) §2 "Node classes"). Multiple guitar variants may coexist on the bus.

The actuation logic (bitmask → open-drain assert / tri-state release) lives in the
`BTN_APPLY` macro in [`t1s_follower.c`](config.mcc/src/t1s_follower.c) — it has no detector
dependencies. fretboard keeps actuating until this node is proven; marvin flips its command
target then.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | PIC32CM PL10 (same family as fretboard; Cortex-M0+, 24 MHz) |
| Toolchain | XC32 |
| MAC-PHY | LAN8651 (10BASE-T1S), SPI Mode 0, ≤ ~12 MHz on the 24 MHz part |

**To be fixed when the guitar MCC project is generated** (Greg, in MPLAB — same shape as
marvin's FLEXCOM4 setup):

- One SERCOM in **SPI-master mode** (Mode 0: CPOL=0/CPHA=0, MSB first) for the LAN8651, plus
  GPIO for `CS_N` (hardware SS if held across a transaction, else bit-banged), `IRQ_N`
  (external-interrupt pin), and `RST`.
- Seven **open-drain** button-output GPIOs (assert = drive low, release = input/tri-state so the
  controller's pull-up restores idle), bit layout matching the wire command:

  | Bit | Output |
  |----:|--------|
  | 0 | Green fret |
  | 1 | Red fret |
  | 2 | Yellow fret |
  | 3 | Blue fret |
  | 4 | Orange fret |
  | 5 | Strum down |
  | 6 | Strum up |

- A periodic timer (TC) tick to service the bus / apply commands (no 240 Hz ADC scan — that's
  the detector's job).

PoDL on the pair powers the node (transparent to firmware; see [T1S/PoDL link](../../docs/t1s-podl-link.md) §6).

## 3. The T1S link

A **PLCA follower** — the mirror of marvin's coordinator glue, reusing the shared
[`third_party/oa-tc6-lib`](../../third_party/oa-tc6-lib) and the same L2 framing:

- **Node id 2**, MAC `02:00:00:00:00:02` (addressing scheme: [T1S/PoDL link](../../docs/t1s-podl-link.md) §7.1).
- `TC6_Init` + `TC6Regs_Init(enablePlca=true, nodeId=2, nodeCount=8, …)` — follower, **not**
  coordinator.
- Custom ethertype `0x88B5`; marvin's command rides as a 1-byte payload inside the 14-byte
  Ethernet header. The guitar accepts frames addressed to its MAC from the coordinator.
- **Bare-metal** (no FreeRTOS on PL10): the TC6 service loop runs from the main loop / timer
  tick, not a task; `IRQ_N` is wired to a SERCOM-EIC pin (not a PIO controller as on marvin).

The marvin-side reference for all of this is [`firmware/marvin/default/src/net/t1s/t1s_link.c`](../marvin/default/src/net/t1s/t1s_link.c)
(coordinator) — the follower inverts the roles: RX the command, no detector-stream TX.

## 4. Firmware design

Implemented in [`t1s_follower.c`](config.mcc/src/t1s_follower.c):

1. **Bring-up:** reset the LAN8651 (`RST` pulse), configure SPI (Mode 0), `TC6_Init` +
   `TC6Regs_Init` as follower id 2. Gate: read chip revision + PLCA *follower* status.
2. **RX path:** TC6 delivers the marvin command frame → validate ethertype `0x88B5` → take the
   1-byte payload as the button bitmask → apply to the 7 GPIOs (open-drain assert / tri-state
   release via the `BTN_APPLY` macro).
3. **TX path:** none required initially. Optional `applied_mask` telemetry back to marvin (for
   edge-ai zero-skew labels) is **deferred** with the edge-ai re-homing effort.
4. **Service:** call `TC6_Service` from the main loop / tick, woken by `IRQ_N`.
5. **CLI** (debug aid, on the SERCOM1 UART via embedded-cli, static allocation): `t1s` (link /
   sync / chipRev / PLCA / counters), `btn <mask>` and `tap <mask> [ms]` drive the button GPIOs
   directly — so the Wii-guitar wiring can be exercised before the T1S link is up.

## 5. What this is *not*

- **Not a sensor.** No phototransistors, no ADC — sensing is the detector node's job.
- **Not an on-device model.** The edge-ai MODEL_DRIVEN inference stays on fretboard for now;
  re-homing it (detector infers → T1S → guitar) is out of scope.
- **Not a game brain.** No chord/strum timing — marvin's timing pipeline owns that and sends
  the resulting bitmask.
- **Not the PLCA coordinator.** marvin (node 0) beacons the cycle; guitar is a follower.

## 6. Milestones

| Status | Item |
|---|---|
| ✅ | **G0** — guitar MCC project: SERCOM0 SPI (Mode 0), EIC EXTINT13 (falling) on `IRQ_N`=PA13, `CS`=PA15 / `RST`=PA14 GPIO, 7 button GPIOs, SERCOM1 debug UART (CS/IRQ_N reassigned to match wiring) |
| ✅ | **G1** — T1S follower bring-up on hardware: `LAN8651 up - chipRev=2 … PLCA follower id=2/8` |
| ✅ | **G2** — end-to-end: marvin's command over T1S → the addressed Wii GPIO asserts |
| ✅ | **CLI** — `t1s`/`btn`/`tap`/`id`/`plca` on the debug UART (embedded-cli). Drives the Wii-guitar GPIOs locally and reports T1S link/sync/PLCA status. |
| ✅ | **Heartbeat** — periodic presence frame (ethertype `0x88B6`) to the coordinator so marvin's `nodes` shows this node present |
| 🔭 | **G3** — full system: `fretboard` (detector) + `guitar` (actuator) both on the bus with marvin selecting the active of each (marvin already targets the guitar; needs the fretboard moved to T1S) |
