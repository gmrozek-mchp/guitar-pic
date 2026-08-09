# lemmy — Specification

> What lemmy *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: smart — beat-driven nod wired.** T1S PLCA follower (id 6, link + presence + CLI), the
> two-servo TCC0 PWM driver + position/calibration layer are up and verified on hardware. lemmy now
> consumes [`beatbox`](../beatbox/SPEC.md)'s beat frame (ethertype `0x88B8`) and runs a local nod
> engine (`nod_engine.{c,h}` + `beat_nod.{c,h}`) to head-bang the neck servo in time to the music,
> pending on-hardware verification against a live beatbox. See §6 and [`docs/journal.md`](docs/journal.md).

## 1. Purpose

lemmy is the **animation node** on the marvin T1S bus — an animated guitar-playing puppet.
It drives **two R/C hobby servos** to bring the puppet to life:

- **Neck joint** — nods / head-bangs the head.
- **Bottom jaw** — opens and closes the mouth.

Its **primary purpose is to nod the head in time to the music**, driven by beat signals from a
future **beatbox** node (T1S id 5). Long term the jaw may animate "talking." lemmy does **no
sensing and no game logic** — like [`guitar`](../guitar/SPEC.md) it is a follower that receives
signals over the bus and actuates.

lemmy is a new **node class** (*animation*) alongside the existing detector / guitar / controller
classes (top-level [`SPEC.md`](../../SPEC.md) §2 "Node classes"). It shares the T1S follower glue
and the LAN8651 MAC-PHY with `guitar`; only the output stage (servos vs. GPIO/LEDs) and its command
source differ.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | PIC32CM6408PL10048 (same family as guitar/fretboard; Cortex-M0+, 24 MHz) |
| Toolchain | XC32 |
| MAC-PHY | LAN8651 (10BASE-T1S), SPI Mode 0, ≤ ~12 MHz on the 24 MHz part |
| Actuators | 2× R/C hobby servos (neck joint, bottom jaw), standard 50 Hz / 1–2 ms pulse |

**T1S control pinout** — mirrors the [`guitar`](../guitar/SPEC.md) ATE_2026 board: SERCOM0 in
**SPI-master mode** (Mode 0: CPOL=0/CPHA=0, MSB first) for the LAN8651, GPIO chip-select / reset,
and an EIC external-interrupt pin for `IRQ_N`:

| Signal | Pin | Function |
|--------|-----|----------|
| `T1S_SCK` | PA05 | SERCOM0 PAD1 |
| `T1S_MISO` | PA07 | SERCOM0 PAD3 |
| `T1S_MOSI` | PA04 | SERCOM0 PAD0 |
| `T1S_CS` | PA06 | GPIO, idle high |
| `T1S_RST` | PA03 | GPIO, idle high |
| `T1S_IRQ_N` | PA02 | EIC EXTINT2, falling edge |
| `CDC_TX` / `CDC_RX` | PB00 / PB01 | SERCOM1 USART (debug console) |

**Servo outputs** — two TCC0 PWM channels, NPWM single-slope, DIV16 (1.5 MHz) / `PER = 29999` →
exactly **50 Hz / 20 ms** frame; 0.667 µs/tick gives 1500 counts across the 1.0–2.0 ms pulse window:

| Servo | Pin | Function |
|-------|-----|----------|
| Neck joint (nod / head-bang) | PA16 | TCC0_WO0 (CC0) |
| Bottom jaw (mouth open/close) | PA17 | TCC0_WO1 (CC1) |

Servos are powered from a separate rail (not the MCU 3V3) with a common ground; servo current must
not sink through the logic supply. The T1S bus is serviced from the main loop, woken by `IRQ_N`.
PoDL on the pair (design-direction only; see [T1S/PoDL link](../../docs/t1s-podl-link.md) §6) would
power the node but **not** the servos.

## 3. The T1S link

A **PLCA follower** — the mirror of marvin's coordinator glue, reusing the shared
[`third_party/oa-tc6-lib`](../../third_party/oa-tc6-lib) and the same L2 framing, identical in shape
to `guitar`'s:

- **Node id 6**, MAC `02:00:00:00:00:06` (addressing scheme: [T1S/PoDL link](../../docs/t1s-podl-link.md) §7.1).
- `TC6_Init` + `TC6Regs_Init(enablePlca=true, nodeId=6, nodeCount=8, …)` — follower, **not** coordinator.
- **Bare-metal** (no FreeRTOS on PL10): the TC6 service loop runs from the main loop / SysTick tick;
  `IRQ_N` is wired to a SERCOM-EIC pin.
- **Presence heartbeat** (ethertype `0x88B6`) to the coordinator so marvin's `nodes` shows lemmy
  present. A new `node_type = 4` (*animation*) is proposed for the heartbeat payload — marvin's §7.2
  decode + `nodes` display learn it (marvin-side follow-up). Heartbeat TX is gated on PLCA actually
  operating (`PLCA_STATUS` bit 15, polled every 250 ms) — not just local MAC-PHY init — so a follower
  never queues a frame before the coordinator's beacon exists; `T1SFollower_IsConnected()` reports this
  real on-bus state.
- **Command planes:** three coexist, routed by ethertype. (1) **beat frame** — [`beatbox`](../beatbox/SPEC.md)
  (id 5) **broadcasts** an 8-byte `LightshowFrame` under **ethertype `0x88B8`** (dst `FF:FF:FF:FF:FF:FF`)
  at ~23.4 Hz; lemmy consumes it locally and runs the nod engine to drive the neck (see §4). (2) **direct
  servo command** — a `0x88B5` unicast frame carrying `[neck_i8, jaw_i8]` still drives both servos
  latest-wins (marvin's `lemmy <neck> <jaw>`), the manual/override seam when the nod is disabled or
  beatbox is quiet. (3) **control channel** — a `0x88B9` unicast frame carrying a typed `[opcode, arg]`
  tunes the nod remotely: `0x01` enable/disable, `0x02` trim, `0x03` oscillator (marvin's
  `lemmy nod|trim|osc`), mapping onto the same `BeatNod`/`NodEngine` setters as lemmy's local `nod` CLI.
  `nod off` here is what frees the neck for the `0x88B5` manual path. Opcode `0x04` is the
  **servo output enable** (marvin's `lemmy output on|off`) → `Servo_SetEnabled`; see §4. Opcode space is
  left open for future scripted gestures / jaw talking (L4).
  - **`nod enable` and `output enable` are different knobs, deliberately.** The first detaches the neck
    from the beat engine so a manual position sticks; the second gates the servos outright, against
    *every* source — the nod, marvin's `0x88B5` positions, and beatbox's. Only the second answers "lemmy
    must not move".
- **Heartbeat echo.** The `0x88B6` heartbeat's `flags` byte carries **bit1 = output enabled**
  (`Servo_IsEnabled()`), the return leg of opcode `0x04`: marvin compares it against what it last
  commanded and re-pushes on mismatch, so the gate converges after a lost frame, a reboot, or a local
  `output` command.

The marvin-side reference is [`firmware/marvin/default/src/net/t1s/t1s_link.c`](../marvin/default/src/net/t1s/t1s_link.c)
(coordinator); the follower reference is [`guitar`](../guitar/config.mcc/src/t1s_follower.c).

## 4. Firmware design

Planned to mirror `guitar`'s `t1s_follower.{c,h}` + `cli.{c,h}` (reused, retargeted), then add a servo
layer:

1. **Bring-up:** reset the LAN8651 (`RST` pulse), configure SPI (Mode 0), `TC6_Init` + `TC6Regs_Init`
   as follower id 6. Gate: read chip revision + PLCA *follower* status.
2. **Service:** call `TC6_Service` from the main loop / tick, woken by `IRQ_N`.
3. **Heartbeat:** periodic (≈500 ms) `0x88B6` presence frame to the coordinator.
4. **CLI** (debug aid, SERCOM1 UART via embedded-cli, static allocation): `t1s` (link / sync /
   chipRev / PLCA / counters), `servo`/`pos`/`cal` (exercise + calibrate the mechanism), `nod`
   (beat-nod status; `on|off` / `trim <n>` / `osc <0|1>`), and `output <on|off>` (the gate below).
   `servo`/`pos`/`cal` say so explicitly when the gate is closed — otherwise calibrating a gated node
   looks like broken hardware. The local setting is a bench aid, not an override: marvin reconciles it
   against the heartbeat echo while it is on the bus.
5. **Servo layer** (L2): TCC0 PWM for the two servos + a position/calibration layer (`servo.{c,h}`).
   - **Output gate.** `Servo_SetEnabled` gates `Servo_SetPulseUs`, the single hardware write every path
     funnels through (`Servo_SetPosition` maps through calibration and then calls it), so one flag stops
     the nod engine, marvin, beatbox and the CLI alike. Requested pulses/positions are still recorded
     while gated and re-applied on enable, so the motion source resumes where it believes it is;
     disabling parks both servos at neutral first rather than freezing mid-nod. Defaults enabled, and
     marvin's boot default is also enabled.
6. **Beat-driven nod** (L3, `nod_engine.{c,h}` + `beat_nod.{c,h}`): the `0x88B8` consumer. `beat_nod`
   stashes each beat frame from the RX path; `BeatNod_Tasks()` (main loop) reconstructs per-band beat
   onsets from the frame flags, lifts `energy` into the engine's loudness domain, ticks the pure
   integer `nod_engine` once per frame (its tempo/oscillator tracking is self-contained), then maps
   the engine's target angle onto the neck servo position. The head parks at neutral when frames stop.
   The nod engine ports from the source project (`config.mcc.bak/nod_engine.c`), decoupled from its
   servo so the caller reads the angle out. jaw stays neutral (L4).

Static allocation only (no malloc), per project rule.

## 5. What this is *not*

- **Not a sensor.** No phototransistors, no ADC.
- **Not a game brain.** No chord/strum timing.
- **Not the PLCA coordinator.** marvin (node 0) beacons the cycle; lemmy is a follower.
- **Not a Wii actuator.** It does not touch a Wii guitar / Wiimote — it animates a puppet. (`guitar`
  is the Wii/LED actuator node.)
- **Not choreographed by another node.** The nod is derived locally from beatbox's beat frame; no
  node sends lemmy a per-frame neck angle. The `0x88B5` direct-servo path is a manual override only.

## 6. Milestones

| Status | Item |
|---|---|
| ✅ | **L0a** — base MCC project scaffolded (PIC32CM6408PL10048): clock/EVSYS/NVIC/PORT, CMSIS+DFP, default main loop |
| ✅ | **L0b** — T1S/CLI peripherals in MCC: SERCOM0 SPI (Mode 0), EIC EXTINT2 (falling) on `IRQ_N`=PA02, `CS`=PA06 / `RST`=PA03 GPIO, SERCOM1 debug UART (PB00/PB01) — mirror of `guitar` G0 (verified byte-identical) |
| ✅ | **L1** — T1S follower bring-up on hardware: `LAN8651 up … PLCA follower id=6/8`, presence heartbeat (`node_type=4`), `t1s` CLI — verified on the bus |
| ✅ | **L2** — servo motion: TCC0 PWM for the 2 servos. Raw driver (`servo.{c,h}`) + `servo <neck\|jaw> <us>` CLI, verified driving real servos. Puppet-relative pose + calibration and a `nod`/`jaw` envelope layer come next (L3) |
| 🚧 | **L3** — beat-driven head nod: `nod_engine.{c,h}` (ported, decoupled integer DSP) + `beat_nod.{c,h}` consume beatbox's `0x88B8` beat frame → neck head-bang; `nod` CLI (status / `on\|off` / `trim` / `osc`). Wired; pending on-hardware verification against a live beatbox |
| ✅ | **L3.5** — `0x88B9` control channel: remote nod enable/disable + trim + osc (`[opcode, arg]`), driven by marvin's `lemmy nod\|trim\|osc`. `nod off` frees the neck for the `0x88B5` manual path. Wired; pending on-hardware verification |
| ✅ | **L3.6** — servo **output enable** (`0x88B9` op `0x04`) gating `Servo_SetPulseUs`, so one switch stops every motion source; echoed back as heartbeat `flags` bit1 so marvin reconciles it. Drives marvin's dashboard LEMMY toggle. Wired; pending on-hardware verification |
| 🔭 | **L4** (future) — jaw "talking" animation; scripted gestures on the `0x88B9` channel (new opcodes) |
