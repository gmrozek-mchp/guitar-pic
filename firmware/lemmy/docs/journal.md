# lemmy — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the lemmy (animated guitar-playing puppet) firmware. Newest entries at the top. For *what lemmy is* (purpose, hardware, link, firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Bring lemmy up on the T1S bus first, then add motion.** lemmy is the **animation node**: a
PIC32CM6408PL10048 T1S PLCA *follower* (node id 6, MAC `02:00:00:00:00:06`) that will animate a
guitar-playing puppet with **two R/C hobby servos** — a **neck joint** (nod / head-bang) and a
**bottom jaw** (mouth open/close). Its primary job is to nod the head in time to the music from a
future **beatbox** node (id 5); long term the jaw may animate "talking."

The base MCC project is scaffolded and committed (unmodified generator output: clock/EVSYS/NVIC/PORT,
CMSIS+DFP, default `SYS_Initialize`/`SYS_Tasks` main loop). The immediate path mirrors the
[`guitar`](../../guitar/SPEC.md) node's G0→G1: add the T1S/CLI peripherals in MCC (L0b), then port the
`t1s_follower` + `cli` glue for follower bring-up (L1). Servo motion (L2+) comes after the link is
proven.

**Next:** on-hardware bring-up (deferred to next session — no T1S board / servos wired yet). L1:
confirm `LAN8651 up … PLCA follower id=6/8`, the `0x88B6` presence heartbeat (`node_type=4`), and
`t1s`/`id`/`plca` CLI. L2: exercise the two servos via `servo <neck|jaw> <us>` and confirm the
1.0–2.0 ms pulse sweeps the travel.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-28 | **lemmy created as the *animation* node class; T1S bring-up before motion.** PIC32CM6408PL10048, PLCA follower **id 6** / MAC `02:00:00:00:00:06` (the slot reserved in [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1). Two R/C hobby servos: neck joint (nod) + bottom jaw. Phase order: L1 T1S follower (link + heartbeat + CLI) → L2 servo motion → L3 beat-driven nod from beatbox (id 5). | Greg's call: prove the node on the bus first, reusing the `guitar` follower glue + `oa-tc6-lib` (same MCU family — keeps the "OA SPI driver scales across the family" demo and minimizes bring-up), then layer motion. The puppet's animation source is a beat feed, not the guitar button bitmask, so it's a distinct node class. |
| 2026-07-28 | **T1S control pinout reuses `guitar`'s ATE_2026 map** (`CS`=PA06, `RST`=PA03, `IRQ_N`=PA02/EXTINT2; SPI SCK=PA05/MISO=PA07/MOSI=PA04; debug UART PB00/PB01). | Same MCU and same LAN8651 wiring lets the `t1s_follower` glue port over near-verbatim (only `T1S_NODE_ID` = 6 changes). Servo PWM pins are separate and fixed at L2. |
| 2026-07-28 | **Heartbeat `node_type = 4` (*animation*) proposed** for lemmy's `0x88B6` presence frame. | Existing enum is 1=detector, 2=guitar, 3=controller; lemmy is a new class. marvin's §7.2 decode + `nodes` display need to learn value 4 (marvin-side follow-up). |

---

## Open questions

- **Beat-signal command plane.** What does beatbox (id 5) send lemmy, and over which ethertype? A beat
  tick / tempo / phase? A new ethertype vs. reusing an existing one? Coupled to the (not-yet-existing)
  beatbox node's design — deferred until L3. marvin may also want to drive lemmy from its timing
  pipeline.
- **Servo PWM peripheral + pins.** TCC0 (two channels) vs. two TCs; which pins. Fixed at L2/MCC.
  Confirm 50 Hz / 1–2 ms pulse resolution off the 24 MHz clock is adequate.
- **Servo power / drive.** Separate servo rail + common ground; brown-out / inrush handling so servo
  current doesn't disturb the LAN8651 or MCU supply. Hardware, not firmware — flag at L2 bring-up.
- **Heartbeat node_type = 4** — confirm with the marvin side before it's baked into lemmy's payload.

---

## Session log

### 2026-07-28 — L2 raw servo driver (TCC0)

- **TCC0 servo PWM added in MCC** (Greg ran the generator): NPWM single-slope, DIV16 (1.5 MHz) with
  `PER = 29999` → exactly **50 Hz / 20 ms** frame, 0.667 µs/tick (1500 counts across the 1–2 ms pulse
  window). `WO0=PA16` (SERVO_NECK), `WO1=PA17` (SERVO_JAW), duty via `CCBUF` (glitch-free). Reviewed +
  committed as `1f8062d`. Resolved the "servo PWM peripheral + pins" open question. Started at 45.78 Hz
  (DIV8, PER=65535 — the 16-bit floor); switched to DIV16 to hit an exact 50 Hz frame.
- **Raw servo driver** (`servo.{c,h}`): `Servo_Initialize` starts TCC0 and parks both servos at
  `SERVO_US_CENTER` (1500 µs); `Servo_SetPulseUs(id, us)` clamps to `[500, 2500]` µs, converts
  µs→ticks (`us*3/2` at 1.5 MHz), writes `CCBUF`. Deliberately *raw* — puppet-relative positioning /
  calibration layer on top comes next. Wired into `main.c` (after `SYS_Initialize`/`SYSTICK`, before
  the follower); `servo.c` added to `user.cmake`.
- **CLI gains `servo`**: `servo` prints both pulse widths + usage; `servo <neck|jaw|0|1> <us>` sets a
  raw pulse (echoes applied value, flags `(clamped)`). `info` now reports live pulse widths. Added
  `<stdlib.h>` for `strtoul`.
- Builds clean; not yet run against servos (none wired). **Next:** wire T1S board + servos and bring
  up L1 + L2 on hardware.

### 2026-07-28 — L0b complete; L1 follower port

- **L0b done.** SERCOM0 SPI master + EIC EXTINT2 added in MCC (Greg ran the generator) and verified
  against `guitar`: SPI/EIC/EVSYS plibs byte-identical, PA04/PA05/PA07 SPI mux + PA06=CS / PA03=RST
  idle-high GPIO + PA02=IRQ_N (EIC_EXTINT2) all match guitar's ATE_2026 map, `EIC_Initialize()` wired,
  NVIC `SERCOM0_IRQn`/`EIC_IRQn` prio 3. Benign extras vs guitar: on-board LED0 (PB02) / SW0 (PB03),
  128 B UART TX ring. Committed as `02fc85a` (MCC regen).
- **L1 — ported the `guitar` follower**, stripped of actuation (lemmy has no fret/strum GPIOs; servos
  are L2, beat RX semantics are L3). `t1s_follower.{c,h}`: `TC6_Init` + `TC6Regs_Init(nodeId=6,
  nodeCount=8)`, SERCOM0 SPI + GPIO CS + EIC IRQ_N glue, SysTick ms clock, presence heartbeat on
  `0x88B6` with **`node_type=4` (animation)**. RX path only *counts* frames (last byte + rx count) —
  no output driven. `tc6-conf.h` copied (PL10 8 KB tuning). CLI gains `t1s`/`id`/`plca` (dropped
  guitar's `btn`/`tap`). Wired into `main.c` (`T1SFollower_Initialize`/`_Tasks`); build sources +
  oa-tc6-lib added to `user.cmake`. SysTick is now started in `main.c` (not the follower).
- **Next:** build + flash; expect `LAN8651 up … PLCA follower id=6/8` and marvin's `nodes` to show
  lemmy present once marvin learns `node_type=4`.

### 2026-07-28 — CLI bring-up (L0b partial + first app code)

- **SERCOM1 USART + SysTick added in MCC** (Greg ran the generator): SERCOM1 ring-buffer USART
  @115200 on PB00=TX/PB01=RX, SysTick 1 ms. Verified the console path matches `guitar`'s config (baud,
  mode, pins, ISR/NVIC wiring, `definitions.h` includes) — only benign diff is lemmy's 128 B TX ring
  vs guitar's 512 B.
- **First application code: operator CLI** (`config.mcc/src/cli.{c,h}`) on the SERCOM1 debug UART,
  embedded-cli vendored under `config.mcc/src/third_party/embedded-cli/` (static-allocation, no malloc).
  Wired into `main.c` (`CLI_Initialize` + `CLI_Tasks`); build sources added via hand-authored
  `cmake/lemmy/default/user.cmake` (kept out of the MCC tree). Commands: `info`, `reset`.
- **`reset`** uses `NVIC_SystemReset()`. Two bring-up bugs found and fixed: (1) MCC only *initializes*
  SysTick (leaves `ENABLE` clear), so `SYSTICK_DelayMs` returned instantly — added `SYSTICK_TimerStart()`
  in `main.c` after `SYS_Initialize` (guitar enables it in `t1s_follower`; lemmy has no follower yet).
  (2) the "resetting..." notice went through embedded-cli's deferred print, which only flushes on the
  next process pass we never reach before the reset — switched to a direct `uart_str()` into the TX ring
  so the 20 ms drain delay gets it to the wire.
- **Next:** rest of L0b — SERCOM0 SPI + EIC EXTINT2 (`IRQ_N`=PA02), `CS`=PA06 / `RST`=PA03 GPIO for the
  LAN8651, then L1 (port the `t1s_follower` glue, follower id 6 + heartbeat).

### 2026-07-28 — subproject created (scaffold + plan)

- Greg set up the base lemmy MPLAB/MCC project (PIC32CM6408PL10048) in `firmware/lemmy/`; committed as
  unmodified generator output (`lemmy: scaffold base MCC project`, commit `713a9db`) — clock/EVSYS/NVIC/
  PORT plibs, CMSIS+DFP packs, linker/startup, default main loop; workspace registers the folder and
  pins clangd `--header-insertion=never`.
- Recorded the design: **animation node**, PLCA follower id 6, two R/C servos (neck nod + jaw), beat-
  driven from a future beatbox (id 5). Phase order agreed: **T1S first, motion second.**
- Created `SPEC.md`, this journal, and refreshed `README.md`. Registered in top-level
  [`SPEC.md`](../../SPEC.md), [`CLAUDE.md`](../../CLAUDE.md), and confirmed the id-6 reservation in
  [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1.
- **No firmware code yet.** Next: L0b (T1S/CLI peripherals in MCC) then L1 (follower bring-up),
  mirroring `guitar` G0/G1.
