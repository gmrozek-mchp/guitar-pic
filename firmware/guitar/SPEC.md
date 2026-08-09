# guitar — Specification

> What guitar *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: working on hardware.** The T1S PLCA follower is up (chipRev read, link
> synced), receives marvin's command over T1S and drives its output GPIOs, sends a
> presence heartbeat, and exposes a debug-UART CLI. As of 2026-07-23 the firmware
> targets the **ATE_2026 board**, whose output stage is **active-high status LEDs**
> (no Wii guitar / Wiimote) — see §2. Remaining work is the full multi-node system
> (G3) — see §6 and [`docs/journal.md`](docs/journal.md).

## 1. Purpose

guitar is the **actuator/indicator node** on the marvin T1S bus. It receives a 1-byte
button bitmask from [marvin](../marvin/docs/spec.md) over 10BASE-T1S and drives its output
GPIOs from that mask. It does **no sensing and no game logic** — it is purely the actuation
half of what today's [fretboard](../fretboard/SPEC.md) firmware does, lifted onto its own node.

The output stage is board-dependent, driven by the same command → GPIO mapping code:

- **Wii-guitar variant** (original): the fret/strum GPIOs are software open-drain, driving a
  real Wii guitar controller's buttons (assert = drive low, release = tri-state).
- **ATE_2026 variant** (current board, 2026-07-23): the GPIOs drive **active-high status LEDs**
  (`Set` = lit, `Clear` = off) — no Wii guitar, no Wiimote. Strum up/down collapse to a single
  STRUM indicator.

The mask → GPIO mapping (`BTN_APPLY` macro + `buttons_*`) lives in
[`t1s_follower.c`](config.mcc/src/t1s_follower.c) and is shared across variants — the "button"
naming is kept intentionally so a future board can carry both Wii actuators *and* LEDs.

This split lets sensing (detector nodes) and actuation (guitar nodes) live on separate nodes
of one PLCA bus, with marvin selecting the active one of each class (see top-level
[`SPEC.md`](../../SPEC.md) §2 "Node classes"). Multiple guitar variants may coexist on the bus.
fretboard keeps actuating until this node is proven; marvin flips its command target then.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | PIC32CM PL10 (same family as fretboard; Cortex-M0+, 24 MHz) |
| Toolchain | XC32 |
| MAC-PHY | LAN8651 (10BASE-T1S), SPI Mode 0, ≤ ~12 MHz on the 24 MHz part |

**Pin map (ATE_2026 board, PIC32CM6408PL10048)** — SERCOM0 in **SPI-master mode**
(Mode 0: CPOL=0/CPHA=0, MSB first) for the LAN8651, GPIO chip-select / reset, and an
EIC external-interrupt pin for `IRQ_N`:

| Signal | Pin | Function |
|--------|-----|----------|
| `T1S_SCK` | PA05 | SERCOM0 PAD1 |
| `T1S_MISO` | PA07 | SERCOM0 PAD3 |
| `T1S_MOSI` | PA04 | SERCOM0 PAD0 |
| `T1S_CS` | PA06 | GPIO, idle high |
| `T1S_RST` | PA03 | GPIO, idle high |
| `T1S_IRQ_N` | PA02 | EIC EXTINT2, falling edge |
| `CDC_TX` / `CDC_RX` | PB00 / PB01 | SERCOM1 USART (debug console) |
| `LED0` | PB02 | GPIO, active-low — liveness / link-state heartbeat |

Output GPIOs — **active-high status LEDs** (`Set` = lit, `Clear` = off; all outputs, init low),
bit layout matching the wire command:

  | Bit | Output | Pin |
  |----:|--------|-----|
  | 0 | Green fret | PA12 |
  | 1 | Red fret | PA11 |
  | 2 | Yellow fret | PA10 |
  | 3 | Blue fret | PA09 |
  | 4 | Orange fret | PA08 |
  | 5, 6 | Strum (up/down collapsed to one STRUM indicator) | PA13 |

The bus is serviced from the main loop, woken by `IRQ_N` (no periodic ADC scan — that's the
detector's job). PoDL on the pair powers the node (transparent to firmware;
see [T1S/PoDL link](../../docs/t1s-podl-link.md) §6).

> The original Wii-guitar board instead used **software open-drain** on seven pins (5 frets +
> strum down + strum up), with a different pinout (`CS`=PA15, `RST`=PA14, `IRQ_N`=PA13/EXTINT13).
> The command → GPIO mapping code is shared; only the drive polarity and pinout differ.

## 3. The T1S link

A **PLCA follower** — the mirror of marvin's coordinator glue, reusing the shared
[`third_party/oa-tc6-lib`](../../third_party/oa-tc6-lib) and the same L2 framing:

- **Node id 3**, MAC `02:00:00:00:00:03` (addressing scheme: [T1S/PoDL link](../../docs/t1s-podl-link.md) §7.1).
- `TC6_Init` + `TC6Regs_Init(enablePlca=true, nodeId=3, nodeCount=8, …)` — follower, **not**
  coordinator.
- Custom ethertype `0x88B5`; marvin's command rides as a 1-byte payload inside the 14-byte
  Ethernet header. The guitar accepts frames addressed to its MAC from the coordinator.
- **Bare-metal** (no FreeRTOS on PL10): the TC6 service loop runs from the main loop / timer
  tick, not a task; `IRQ_N` is wired to a SERCOM-EIC pin (not a PIO controller as on marvin).
- **Control channel** (ethertype `0x88B9`, unicast to this node): typed `[opcode, arg]`. One opcode
  today — `0x01` **output enable** (arg 0|1), which gates the button GPIOs. See §4.
- **Presence heartbeat** (ethertype `0x88B6`, `node_type = 2`) to the coordinator so marvin's
  `nodes` shows guitar present. Heartbeat TX is gated on PLCA actually operating (`PLCA_STATUS`
  bit 15, polled every 250 ms) — not just local MAC-PHY init — so a follower never queues a frame
  before the coordinator's beacon exists; `T1SFollower_IsConnected()` reports this real on-bus
  state, and the `LED0` heartbeat encodes it (lub-dub on the bus, single blip when down). The
  heartbeat's `flags` byte carries **bit1 = output enabled**, the return leg of the control channel:
  marvin compares it against what it last commanded and re-pushes on mismatch, so the gate converges
  after a lost frame, a node reboot, or a local `output` command.

The marvin-side reference for all of this is [`firmware/marvin/default/src/net/t1s/t1s_link.c`](../marvin/default/src/net/t1s/t1s_link.c)
(coordinator) — the follower inverts the roles: RX the command, no detector-stream TX.

## 4. Firmware design

Implemented in [`t1s_follower.c`](config.mcc/src/t1s_follower.c):

1. **Bring-up:** reset the LAN8651 (`RST` pulse), configure SPI (Mode 0), `TC6_Init` +
   `TC6Regs_Init` as follower id 3. Gate: read chip revision + PLCA *follower* status.
2. **RX path:** TC6 delivers the marvin command frame → validate ethertype `0x88B5` → take the
   1-byte payload as the button bitmask → apply to the output GPIOs via the `BTN_APPLY` macro
   (ATE_2026 board: active-high LED `Set`/`Clear`; original Wii board: open-drain assert / tri-state
   release). A `0x88B9` frame is dispatched instead as a `[opcode, arg]` control command.
   - **Output enable** gates `buttons_apply_mask`: while disabled every output is released and held
     released, and disabling releases at once rather than at the next command (a mask asserted when
     the gate closed must not stay held on the guitar). The commanded mask is still recorded, so
     `T1SFollower_LastCmd` keeps tracking marvin while the output is gated — that pairing is how the
     CLI distinguishes "gated" from "not receiving". Gating **here** rather than at the coordinator
     is deliberate: inside a song with the fretboard selected, marvin is silent and the fretboard
     drives this node peer-to-peer, so a coordinator-side gate would miss exactly the window where
     the robot is playing.
3. **TX path:** none required initially. Optional `applied_mask` telemetry back to marvin (for
   edge-ai zero-skew labels) is **deferred** with the edge-ai re-homing effort.
4. **Service:** call `TC6_Service` from the main loop / tick, woken by `IRQ_N`.
5. **CLI** (debug aid, on the SERCOM1 UART via embedded-cli, static allocation): `t1s` (link /
   sync / chipRev / PLCA / counters / output gate / last control command), `btn <mask>` and
   `tap <mask> [ms]` drive the button GPIOs directly — so the Wii-guitar wiring can be exercised
   before the T1S link is up — and `output <on|off>` sets the gate locally. The local setting is a
   bench aid, not an override: marvin reconciles the gate against the heartbeat echo and will put it
   back within ~1 s while it is on the bus.

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
| ✅ | **G1** — T1S follower bring-up on hardware: `LAN8651 up - chipRev=2 … PLCA follower id=2/8` (node renumbered to id 3 in source 2026-07-28; re-flash reproduces at `id=3/8`) |
| ✅ | **G2** — end-to-end: marvin's command over T1S → the addressed Wii GPIO asserts |
| ✅ | **CLI** — `t1s`/`btn`/`tap`/`id`/`plca` on the debug UART (embedded-cli). Drives the Wii-guitar GPIOs locally and reports T1S link/sync/PLCA status. |
| ✅ | **Heartbeat** — periodic presence frame (ethertype `0x88B6`) to the coordinator so marvin's `nodes` shows this node present. TX gated on `PLCA_STATUS` bit 15 (polled 250 ms), so `IsConnected` / heartbeat reflect real on-bus state, not just local init |
| ✅ | **Status LED** — `status_led.{c,h}` drives `LED0` (PB02, active-low) as a non-blocking SysTick heartbeat: lub-dub when on the bus, single blip when down; decoupled from any command state |
| ✅ | **ATE_2026 port** — control + output pins remapped (`CS`=PA06, `RST`=PA03, `IRQ_N`=PA02/EXTINT2; LEDs PA08–PA13), output stage flipped to active-high status LEDs, strum collapsed to one STRUM. Builds; not yet exercised on the physical board. |
| ✅ | **Output enable** — `0x88B9` control channel (op `0x01`) gating `buttons_apply_mask`, echoed back as heartbeat `flags` bit1 so marvin reconciles it. Drives marvin's dashboard GUITAR toggle, and holds even while the fretboard drives this node peer-to-peer. Wired; pending on-hardware verification |
| 🔭 | **G3** — full system: `fretboard` (detector) + `guitar` (actuator) both on the bus with marvin selecting the active of each (marvin already targets the guitar; needs the fretboard moved to T1S) |
