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

L1 (T1S follower), L2 (raw servo PWM), and the L3 position + calibration layer are up and verified on
hardware. L3 beat-driven nod is now **wired**: lemmy consumes beatbox's `0x88B8` beat frame locally and
runs a ported nod engine (`nod_engine.{c,h}` + `beat_nod.{c,h}`) to head-bang the neck — pending
on-hardware verification against a live beatbox.

**Next:** verify the nod on hardware with beatbox live on the bus (frame counter advances, locked BPM
tracks the music, neck head-bangs / snaps on strong beats / comeback-slams / parks on silence). Then
jaw "talking" (L4) and the `0x88B9` scripted-gesture / override channel.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-31 | **Make lemmy smart via a local nod engine off the shared `0x88B8` broadcast**, not a dumb-puppet per-frame command stream. lemmy consumes beatbox's (id 5) 8-byte `LightshowFrame` broadcast (ethertype `0x88B8`, dst `FF:…`, ~23.4 Hz) and runs the ported `nod_engine` to head-bang the neck; jaw stays neutral. The `0x88B5` unicast servo path stays as a manual/override seam. | Reuses the proven integer nod engine from the source project verbatim (its only hardware coupling was three `Servo_SetAngle` calls; the angle is already stored and read via `NodEngine_GetTargetAngle`), needs **zero beatbox-side changes** (the engine tracks tempo/phase itself, so no BPM/phase wire layer), and is symmetric with lightshow's `0x88B8` consumer. beatbox's ~23.4 Hz broadcast equals the engine's design frame rate, so every frame-counted constant (osc period, silence window) holds by ticking once per RX frame. A `0x88B9` position/override channel remains the future seam for scripted gestures + jaw talking. |
| 2026-07-29 | **Servo position is `int8_t` -127..127** (0 = neutral), matching the planned T1S command byte 1:1 — one signed byte per servo, applied on RX with no scaling. Calibration (min/neutral/max µs + invert per servo) is a **compiled-in default copied to a RAM working copy**; tuned live via the `cal` CLI which prints paste-ready initializers to fold back into the default and reflash. **Not persisted on-device** — the PL10 has no EEPROM/RWW (datasheet §5/§26); flash-emulated EEPROM (NVMCTRL self-program, page erase / word write) would stall the single flash array during writes, not worth it for set-once cal. | ±127 gives ~4 µs/step (~6 TCC ticks) — far under servo deadband, so no resolution lost vs a wider internal range, and it avoids scaling the wire byte. Hardcoded cal keeps bring-up simple; live `cal` tuning + reflash is the workflow until (if ever) persistence is needed. |
| 2026-07-28 | **lemmy created as the *animation* node class; T1S bring-up before motion.** PIC32CM6408PL10048, PLCA follower **id 6** / MAC `02:00:00:00:00:06` (the slot reserved in [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1). Two R/C hobby servos: neck joint (nod) + bottom jaw. Phase order: L1 T1S follower (link + heartbeat + CLI) → L2 servo motion → L3 beat-driven nod from beatbox (id 5). | Greg's call: prove the node on the bus first, reusing the `guitar` follower glue + `oa-tc6-lib` (same MCU family — keeps the "OA SPI driver scales across the family" demo and minimizes bring-up), then layer motion. The puppet's animation source is a beat feed, not the guitar button bitmask, so it's a distinct node class. |
| 2026-07-28 | **T1S control pinout reuses `guitar`'s ATE_2026 map** (`CS`=PA06, `RST`=PA03, `IRQ_N`=PA02/EXTINT2; SPI SCK=PA05/MISO=PA07/MOSI=PA04; debug UART PB00/PB01). | Same MCU and same LAN8651 wiring lets the `t1s_follower` glue port over near-verbatim (only `T1S_NODE_ID` = 6 changes). Servo PWM pins are separate and fixed at L2. |
| 2026-07-29 | **marvin recognizes lemmy's heartbeat** — added an animation node row (id 6) to marvin's `net/t1s` node table + a `"lemmy"` display name, so `nodes` lists lemmy present. marvin maps id→type via its static table (it does not decode the payload `node_type` byte), so lemmy's advertised `node_type=4` is informational. | Closes the "confirm node_type=4 with marvin" question: awareness is a table row keyed by node id, matching how guitar/fretboard are recognized. |
| 2026-07-28 | **Heartbeat `node_type = 4` (*animation*) proposed** for lemmy's `0x88B6` presence frame. | Existing enum is 1=detector, 2=guitar, 3=controller; lemmy is a new class. marvin's §7.2 decode + `nodes` display need to learn value 4 (marvin-side follow-up). |

---

## Open questions

- **Servo PWM peripheral + pins.** TCC0 (two channels) vs. two TCs; which pins. Fixed at L2/MCC.
  Confirm 50 Hz / 1–2 ms pulse resolution off the 24 MHz clock is adequate.
- **Servo power / drive.** Separate servo rail + common ground; brown-out / inrush handling so servo
  current doesn't disturb the LAN8651 or MCU supply. Hardware, not firmware — flag at L2 bring-up.

---

## Session log

### 2026-07-31 — L3 beat-driven nod wired (make lemmy smart)

- **Ported the nod engine, decoupled from the servo** (`config.mcc/src/nod_engine.{c,h}`): a verbatim
  copy of the source project's (`../../beatbox/config.mcc.bak/nod_engine.c`) integer DSP state machine —
  per-band tempo trackers, tempo-adaptive oscillator, beat-snap with LFSR jitter, comeback bang, silence
  gate. Only change: dropped `#include "servo.h"` and the three `Servo_SetAngle(...)` calls; each path
  already stored `nod_current_angle`, so the caller reads the target angle via `NodEngine_GetTargetAngle()`
  (tenths-of-degree: 170 head-up … 400 osc peak … 650 beat snap … 800 comeback).
- **Thin consumer glue** (`config.mcc/src/beat_nod.{c,h}`), mirroring lightshow's `beat_show`: RX path
  calls `BeatNod_OnFrame(payload, len)` (guard `len >= 8`, stash `seq/energy/bass/treble/kick/flags`, set
  a `s_new_frame` flag, bump count — no compute in the callback). `BeatNod_Tasks()` (main loop) on a new
  frame reconstructs per-band beat onsets from the flags (`bass_beat = BASS ? (BIG?2:1) : 0`, `full_beat`
  from `MID`), lifts `raw_env = energy * 39u` into the engine's 0-10000 loudness domain, ticks
  `NodEngine_Frame(...)` once, maps the target angle onto the neck (`pos = -(angle-170)*127/630`, clamped
  — negated so the nod drives the head *down* toward `SERVO_POS_MIN`), and `Servo_SetPosition(SERVO_NECK,
  pos)`. The map is negated (rather than flipping the neck `cal invert`) so the manual `pos`/`0x88B5`
  override path keeps its hand-tuned calibration. Parks the neck at neutral after ~750 ms with no frame (bus/
  music quiet) — frees the servo for a marvin `0x88B5` override. An enable flag (default on) gates the
  servo writes so `servo`/`pos`/`cal` manual testing isn't fought by the nod; jaw is never touched (L4).
- **RX accepts `0x88B8`** (`t1s_follower.c`): after reading the ethertype, a `0x88B8` frame goes to
  `BeatNod_OnFrame` and returns; the existing `0x88B5` neck/jaw command path is unchanged.
- **`nod` CLI** (`cli.c`): status (frame count, last `seq/energy/bass/treble/kick/flags`, locked BPM +
  confidence + winning band, current angle / mapped neck position / trim), plus `nod on|off`,
  `nod trim <-127..127>` (`NodEngine_SetPotOffset` — CLI stand-in for the source project's hardware pot,
  which lemmy has no pin for), `nod osc <0|1>` (`NodEngine_SetOscEnabled`).
- `nod_engine.c` + `beat_nod.c` added to `user.cmake`; `BeatNod_Initialize`/`BeatNod_Tasks` wired into
  `main.c`. **Not yet built/flashed** — pending on-hardware verification against a live beatbox (frame
  counter advancing, BPM tracking, neck head-banging / snapping / comeback / parking on silence).

### 2026-07-29 — T1S command RX → servos

- **Wired the command plane RX** ([`t1s_follower.c`](../config.mcc/src/t1s_follower.c)): a `0x88B5`
  frame to id 6 now drives both servos. Payload is **combined, one frame for both**: two signed bytes
  `[neck_i8, jaw_i8]` at fixed offsets, each passed to `Servo_SetPosition` (clamp + cal map), latest-wins.
  int8 matches the position range 1:1 — no scaling. `T1SFollower_LastByte()` → `T1SFollower_LastCmd()`;
  the `t1s` CLI shows `cmd: neck=N jaw=N`.
- **Ethertype: shared `0x88B5` with guitar, not a new one.** Command frames route by destination MAC
  (`02:00:00:00:00:06`), so guitar (id 2) and lemmy (id 6) don't collide on the same ethertype. A new
  ethertype earns its keep only for a different payload *grammar* (as fauxmote's `0x88B7` mf_proto does),
  not merely a different node. If the future beatbox beat plane needs a distinct grammar, it can take its
  own ethertype then.
- **Length check guards against min-frame padding.** The MAC-PHY pads short frames to the 60-byte
  Ethernet minimum, so `len` includes trailing pad — the guard is `len < (HDR + 2)` (enough bytes present)
  and reads fixed offsets, never an exact-length `==`. This is the same trap fauxmote hit (exact-length
  checks dropped padded frames; see fauxmote journal 2026-07-28).
- **Driven end-to-end from marvin.** marvin's `lemmy <neck> <jaw>` / `lemmy center` console command sends
  `send_to_node(6, 0x88B5, [neck,jaw], 2)` via `T1SLink_SendToLemmy` (latest-wins staging flushed by the
  T1S service task). Manual exercise hook for now. **Next:** the `nod`/`jaw` motion envelope, then
  beat-driven nod from a timing-pipeline / beatbox feed.

### 2026-07-29 — L3 position + calibration layer

- **Position layer on top of the raw pulse driver** (`servo.{c,h}`): `Servo_SetPosition(id, pos)` maps a
  signed position through per-servo calibration — a two-segment linear map around an asymmetric neutral
  (`neutral_us`, with `min_us`/`max_us` endpoints) plus an `invert` flag for reversed-mounted servos.
  Position range is **`int8_t` -127..127** to match the planned T1S command byte 1:1. Calibration is a
  compiled-in default copied to a RAM working copy at init (no NVM on the PL10 — see decision log).
  Tested driving real servos.
- **CLI**: `pos <neck|jaw> <-127..127>` (calibrated), `cal [show]` and `cal <neck|jaw> <min|neutral|max|
  invert> <val>` (mutate the working copy, re-apply current position, echo a paste-ready C initializer).
  `info` reports live pulse widths. Committed as `ac640ef`.
- User hand-tuned the compiled-in defaults on hardware: neck `{1000, 1450, 2000, invert}`, jaw
  `{1000, 1600, 1600}` (jaw unipolar — neutral==max, opens on negative position).
- **Next:** wire the T1S RX path to `Servo_SetPosition` (int8-per-servo payload; ethertype TBD with the
  beatbox design), then a `nod`/`jaw` motion envelope, then beat-driven nod.

### 2026-07-29 — L1 + L2 verified on hardware

- **lemmy is live on the T1S bus and the servos move.** With the T1S board and both servos wired,
  the ported follower comes up (link + `0x88B6` presence heartbeat, `node_type=4`) and the raw servo
  driver drives neck (PA16/WO0) + jaw (PA17/WO1) as expected via `servo <neck|jaw> <us>`. Confirms the
  50 Hz / DIV16 TCC0 config and the µs→tick math against real servos.
- L1 and L2 are done as far as bring-up goes. **Next:** L3 — puppet-relative positioning + calibration
  (travel limits / neutral / direction per servo) and a nod/jaw motion envelope, then beat-driven nod.

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
