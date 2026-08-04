# fretboard — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the fretboard firmware. Newest entries at the top. For *what fretboard is* (purpose, hardware, modules, frame format), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

**2026-08-04 — extended heartbeat to v2: report per-node telemetry for marvin's bus-stats UI.** Same contract as the other followers, but `t1s_detector.c` has four TX paths (data→coordinator, command→guitar, GUITAR→fauxmote, heartbeat) and had no RX counter. `0x88B6` payload 8→20 bytes (`T1S_HB_VERSION` 1→2, `T1S_HB_LEN` 8→20): append LE `tx_count_u32, rx_count_u32, crc_err_u16, sym_err_u16`. New `s_tx_total` counts **completed** transmits in all four `*_tx_done` callbacks (distinct from the existing `s_tx_count` = data-frames-only, kept for the CLI/banner); new `s_rx_count` counts all received frames at the top of `TC6_CB_OnRxEthernetPacket` (this node otherwise only consumes the control channel); new `s_crc_err`/`s_sym_err` in `TC6Regs_CB_OnEvent`. Wire contract: `docs/t1s-podl-link.md` §7.2; marvin parses gated on length → standalone reflash. App logic only. **Pending build + on-hardware check.**

---

## Current focus

**T1S-only sense+actuate node (single behaviour — no build flags).** UART and the
`FRETBOARD_LINK`/`FRETBOARD_MODE` flags are gone. Each 240 Hz TC0 tick the fretboard
scans the five phototransistors; the on-device int8 model ([`model_infer.c`](../model_infer.c) /
[`model_infer_stream.c`](../model_infer_stream.c), weights in [`model_weights.h`](../model_weights.h))
infers the Wii-guitar bitmask from the ADC window (in the **main loop** — it overruns the tick in the
ISR; see decision log + edge-ai `runtime.md` §3). Over T1S the node then:

- **streams** the 18-byte data frame (ADC scan + the driven bitmask as `applied_mask` + marvin's CV
  teacher command as `commanded_mask`) to the marvin coordinator (`0x88B5`, logging / edge-ai training)
  — **boots disabled**, enabled over the control channel (opcode `0x02`, `fretboard stream on|off`) or
  the local `stream [on|off]` CLI command (shared flag, last writer wins); `sample_seq` advances while
  off, and
- **drives fauxmote** (ESP32 Wiimote+guitar emulator, controller node id 1) directly with an
  mf_proto GUITAR message on the controller channel (`0x88B7`, peer-to-peer) — this is the
  game-critical path (fauxmote controls the Wii); armed + edge-triggered + 50 ms refresh +
  release-on-disarm, same as the guitar command; whammy/aux reserved (generic sender), and
- **commands** the guitar node (id 3) directly with the inferred 1-byte bitmask (`0x88B5`,
  peer-to-peer) — an *indicator* (open-drain presser); while armed, edge-triggered + a 50 ms
  refresh so a dropped frame self-heals; **disarming sends one final all-released frame then goes
  silent** (no contention for the guitar). While armed marvin gates its own CV output off, so exactly
  one detector drives fauxmote. marvin asserts arm only during an active song with the fretboard
  selected (`active fretboard`) — not in menus/manual control (see marvin journal 2026-08-02 later).

Plus a 500 ms presence heartbeat (`0x88B6`, node_type 1 = detector). All TC6 TX/service is in the main
loop ([`T1SDetector_Tasks`](../t1s_detector.c)); the ISR only scans + stages the data frame. It has **no
local Wii-guitar outputs** (those pins are the LAN8651 SPI). PLCA follower id 4, MAC `02:00:00:00:00:04`
(renumbered from id 1 in source 2026-07-28; last flashed/verified at id 1 — re-flash fretboard + marvin
together for id 4 to take effect).

Actuation is armed via the local **`arm [on|off]` CLI command** or marvin's **control channel** (ethertype
`0x88B9`, opcode `0x01` arm, arg 0|1 → `fretboard arm|disarm`) — a single shared arm flag, last writer wins,
no lockout. Boots disarmed (silent on the command channel until armed). Arm over the bus tracks marvin's
per-song gameplay window (asserted only while a song is playing with the fretboard as the selected
detector; disasserted when the song ends or `active cv` is chosen). **LED0 (PB02)** is the T1S liveness heartbeat ([`status_led.c`](../status_led.c),
lub-dub on the bus / single blip when down), decoupled from the arm state. **Coordination caveat:** the guitar applies whoever
transmitted last (both marvin and the fretboard target `02:..:03`), so only one source may be armed at a time;
**automatic** active-detector/active-guitar coordination is still the follow-up.

**MCC done** (SERCOM0 SPI Mode 0 on PA04/05/07; T1S_CS PA15 / T1S_RST PA14 / T1S_IRQ_N PA13 EXTINT13
falling; SysTick 1 ms; SERCOM1 ring-buffer TX 512). **Operator CLI** on SERCOM1 ([`cli.c`](../cli.c),
vendored `third_party/embedded-cli/`): `t1s` (link + data/cmd tx counts), `adc`, `id`, `plca`. Remaining:
build-wiring (add the T1S + embedded-cli sources/include dirs) and on-hardware bring-up. Build at `-O2`/`-O3`.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-08-03 | **Atomic teacher label: data frame grows 17→18 B with a `commanded_mask` byte; new `0x88B9` teacher opcode `0x04` latches marvin's CV command into every frame.** Fretboard: `data_stream` frame is `… \| applied_mask (u8) \| commanded_mask (u8) \| 0xFC` (`_Static_assert(sizeof==18)`); `data_stream_send(applied_mask, commanded_mask)`. `t1s_detector` gains `T1S_CTRL_TEACHER (0x04)` → `s_teacher_mask` (7-bit, latched regardless of arm/stream), RX case, and a `T1SDetector_TeacherMask()` accessor; `main.c` passes it each tick. marvin: `T1S_DET_CTRL_TEACHER (4)` + `OP_COUNT` 3→4 (staging/flush generic), edge-triggered teacher send in `FretboardLink_Send` (T1S transport only, retried until it lands); `fretboard_link` parses `frame[16]` and emits it; `perf_log` repurposes its spare `reserved` byte → `commanded_mask` (record size unchanged), schema **v5→v6**. Host: `marvin-perf` records/decode carry `commanded_mask`; new export `--labels=commanded-fb` sources labels from it (edge-ai's preferred distillation target); `edge-ai` loader accepts it verbatim (identical schema to actuator-fb). | The old `--labels=actuator` join reconstructed the label↔feature pairing across marvin's bursty USB RX and separate TX clocks, which Phase-2 training floored at ~12 ms strum skew. Latching marvin's CV command into the same 240 Hz frame that carries the ADC scan makes the teacher label atomic at the source — no cross-stream join. Chose a new byte (not overloading `applied_mask`) because in detector-only firmware `applied_mask` is the node's *own* model output, not the teacher; both are now captured so a model-vs-teacher diff is available. Teacher opcode is edge-triggered + retried (matches the arm-send pattern) and un-gated by arm/stream so a capture needn't arm the node. marvin's record already had a spare byte, so only the fretboard *wire* frame grew — marvin's schema bump is version-only. |
| 2026-08-02 | **Fretboard now drives fauxmote directly (controller channel `0x88B7`) so it is the real game input; `active` on marvin is the switch.** New firmware-owned `mf_proto.h` (third byte-for-byte-synced copy of the marvin/fauxmote header — full vocabulary, not just GUITAR). `t1s_detector.c` gains a generic `send_fauxmote(type, payload, len)` + `flush_fauxmote()` mirror of the guitar-command path (dst `02:..:01`, ethertype `0x88B7`, body `[TYPE][payload…]` — no SOF/LEN/CRC, MAC-PHY FCS covers it). `flush_fauxmote` emits `[MF_MSG_GUITAR, mask&0x7F, MF_WHAMMY_REST, 0]`; `s_fx_dirty` set alongside `s_cmd_dirty` at every `SetCommand` edge, flushed at the same `T1S_CMD_REFRESH_MS`. Both gated on the existing ARM flag (`s_cmd_enabled`) — no new opcode; disarm sends one release then silence. marvin: `FretboardLink_Send` and `fx_tx_task`'s GUITAR slice both early-return unless `Detector_GetActive()==DETECTOR_CV_MARVIN_V1` (one release frame on the cv→fretboard handoff so no note sticks); `active <cv\|fretboard>` calls `Detector_SetActive` **and** `T1SLink_SendFretboardCtrl(ARM, id==fretboard)`. `detect` trimmed to `cv` only (the `fretboard` variant was a dead no-op). `mf_proto.h` added to the fretboard fileSet. | Per Greg: fauxmote is what actually controls the game, so the active detector must drive it — "no reason for marvin to be middleman." Before, the fretboard's mask reached fauxmote only by marvin re-mirroring it, and `active` set only marvin's internal `s_active_id` (no T1S frame) so it changed nothing on the node. Making `active` arm/disarm the fretboard **and** gate marvin's own CV output makes it the single arbiter — exactly one detector drives fauxmote at a time, clean handoff both directions. Copying the **full** mf_proto vocabulary + a generic `(type,payload,len)` sender (whammy pinned to rest, aux 0, both reserved) means a future fretboard whammy/accelerometer (`MF_MSG_ACCEL`) is a new call, not a new mechanism — nothing here precludes them (fauxmote already decodes whammy). Reused the ARM gate (not a new opcode) since arm already *is* marvin's active-detector selection over the bus. Guitar node stays as an indicator. **Manual-override hazard** left documented, not solved: arming locally via CLI/SW0 while `active==cv` can double-drive fauxmote — the node's `arm` is last-writer-wins with `active`. |
| 2026-08-02 | **Added 5 selectable inference-model slots (per-difficulty easy/medium/hard/expert + reserved `auto`), runtime-selectable over CLI and T1S — plumbing only.** New firmware-owned (not generated) `models.h` holds the difficulty→`model_def_t*` registry + a `static inline model_resolve()` (bounds-checks, `auto`→hard stub, reports the effective difficulty); it's `#include`d only by the active engine TU so weights instantiate once. `model_infer.h` gains the `MODEL_SEL_*` enum (`MODEL_SEL_DEFAULT = HARD`) + selection API (`model_infer_set_sel`/`_get_sel`/`_effective`/`_sel_name`/`_sel_trained`), implemented as a small wrapper in **both** `model_infer.c` + `model_infer_stream.c` (one public name each, only the compiled engine defines it). `t1s_detector` holds an `s_model_sel` byte written by the local `model` CLI command or new control opcode `0x03` (`T1S_CTRL_MODEL`, arg 0..4) — last writer wins; `main.c` applies changes to the engine on-change (cold path, no ISR cost). marvin: `T1S_DET_CTRL_MODEL (3)` + `OP_COUNT` 2→3 and a `fretboard model <difficulty>` console command. Only `hard` is trained; the other slots **alias to `&model_hard`** via `#ifdef MODEL_HAVE_*` and `auto` resolves to hard, so all five are selectable and drive identically today. | Per Greg: one model per GH difficulty (same NN arch, only weights/thresholds differ) plus a 5th `auto` slot to experiment with an adaptive policy later. Scope is **plumbing only** — per-difficulty corpora aren't captured yet, so real weights for easy/medium/expert are future edge-ai work (needs Greg in the loop). Building the full selection path now (registry + enum + CLI + T1S + marvin) means dropping a trained difficulty later is just: generate `model_weights_<diff>.h`, `#include` + `#define MODEL_HAVE_<DIFF>` in `models.h`, register in the fileSet — no wiring changes. The registry lives in firmware-owned `models.h` (not the generated `model_weights.h`) so hand-editing to add a model is legitimate; keeping it in a header included only by the active engine preserves single weight instantiation (the inactive engine TU stays empty). `auto` is modeled as a policy slot (not a static model) so the adaptive logic has a single documented hook (`model_resolve`), and `model_infer_effective()` lets the CLI show `auto (→hard)`. |
| 2026-08-02 | **`T1SDetector_IsConnected()` now reflects real on-bus state (PLCA_STATUS bit 15), not just MAC-PHY init — adopted beatbox's pattern.** Renamed `s_link_up` → `s_initialized` (local MAC-PHY bring-up done + data path enabled); added `s_plca_op`, polled every 250 ms from `T1SDetector_Tasks` via a background `TC6_ReadRegister(PLCA_STATUS)` → `on_plca_status` callback (caches bit 15 = "coordinator beacon on the wire"). `IsConnected()` now returns `s_plca_op`; **all TX (data flush, guitar command, heartbeat) and `SendFrame` are gated on `s_plca_op`** instead of init-done. The existing on-demand `plca` CLI read (`on_plca_read`) is unchanged. | Per Greg: "connected" should mean *actually on the bus and communicating*, not just that the LAN8651 finished its register sequence — the old init-done flag went true (and stayed true) even with no coordinator present, so the LED heartbeat / CLI `link:` line lied. PLCA_STATUS bit 15 is the real signal (asserts only when the coordinator's beacon is seen). Gating TX on it also fixes a latent hang: a follower has no transmit opportunity without the beacon, so sending earlier queued a frame that never drained and stalled the `*_busy` guards. First of the follower nodes to get this cleanup; lemmy + lightshow to follow (same edit). |
| 2026-08-02 | **LED0 decoupled from arm state → T1S liveness heartbeat (`status_led.c`/`.h`, mirror of lemmy/lightshow).** New `status_led` module: non-blocking 1 Hz heartbeat off the SysTick ms clock — lub-dub double pulse when `T1SDetector_IsConnected()`, single blip when the bus is down. `main.c` drops the `LED0_ON/OFF` macros and the arm→LED mirroring in `actuation_armed()` (now just returns `T1SDetector_Armed()`), calls `StatusLed_Initialize()` after `T1SDetector_Initialize()` and `StatusLed_Tasks()` in the main loop. Sources added to the mplab.json fileSet. | Per Greg: LED0-as-arm-indicator gave no liveness signal (a dark LED could mean disarmed *or* hung firmware). The heartbeat pattern used on lemmy/lightshow always shows the firmware is alive and encodes bus link-state at a glance; arm state is observable over the CLI/`t1s` line and marvin instead. Keeps the three follower nodes' status LEDs consistent. |
| 2026-08-02 | **Added a local `stream [on|off]` CLI command for the data stream (parallel to the T1S control).** `T1SDetector_SetStream()` added alongside the existing `T1SDetector_StreamEnabled()`; the CLI command and the control-channel RX (opcode 0x02) write the same `s_stream_enabled` flag — last writer wins, no lockout. Same shape as the `arm` command. | Mirrors the arm-command change so the data feed can be toggled from the bench console as well as from marvin, with neither locking out the other. |
| 2026-08-02 | **Dropped the SW0 arm toggle — arm is now an `arm [on|off]` CLI command; no remote-authoritative lockout.** Collapsed the split `s_ctrl_armed`/`s_ctrl_arm_valid` (remote) + `s_armed` (SW0) into one `s_armed` in `t1s_detector.c`, written by either the control-channel RX (opcode 0x01) or the new `T1SDetector_SetArmed()` — last writer wins. Removed the SW0 read/debounce from `main.c` (`actuation_armed()` just mirrors `T1SDetector_Armed()` to LED0) and the `T1SDetector_RemoteArm()` accessor. New fretboard `arm` CLI command; `t1s` arm line simplified to on/off. | Per Greg: replace the physical button with a console command and drop the "remote authoritative once seen" rule — a single shared arm flag either source can set is simpler and matches the operator workflow (drive it from the CLI or from marvin interchangeably, no mode where one locks out the other). SW0 (PB03) is left configured by MCC but unread. |
| 2026-08-02 | **Data stream to marvin is now remotely gated (control opcode `0x02`) and boots *disabled*.** Fretboard: `T1S_CTRL_STREAM (0x02)` + `s_stream_enabled` flag gated in `flush_data_frame()`, RX dispatch case, `T1SDetector_StreamEnabled()` accessor; CLI `t1s` shows `stream=on/off`. marvin: `T1S_DET_CTRL_STREAM (2)` + `T1S_DET_CTRL_OP_COUNT` 1→2 (staging/flush already generic), `fretboard stream <on\|off>` console command. `sample_seq` keeps advancing while off so the first frame after re-enable shows the true gap. | The 17-byte ADC+`applied_mask` feed is only ever consumed by marvin (logging / edge-ai capture), so marvin should decide when it flows rather than the node blindly transmitting 240 Hz onto the shared bus at all times. Booting disabled keeps the bus quiet until marvin opts in (chose disabled-default over enabled-default so nothing streams unless asked). Reuses the same `0x88B9` per-node control pattern as arm — one more opcode in the detector namespace. |
| 2026-08-02 | **Disarmed = silent on the guitar command channel (was: kept sending `0x00` at ~20 Hz).** `T1SDetector_SetCommand()` gained an `active` flag (`t1s_detector.c`/`.h`); the 50 ms refresh only fires while armed, and the arm→disarm edge queues one final all-released frame then stops. `main.c` captures the effective armed state per tick (`s_actuation_active`) and passes it. Doc/SPEC/link-doc wording updated to match. | The 50 ms self-heal refresh re-sent the current mask continuously whenever the link was up — so a *disarmed* node still transmitted `0x00` at ~20 Hz to the guitar MAC, stomping on any other command source (marvin). That defeated the purpose of `fretboard disarm`, which exists precisely so marvin can take the guitar. Now disarm truly hands the guitar over: one clean release (so no note sticks) then silence. The guitar is last-writer-wins, so a silent node cannot contend. |
| 2026-08-02 | **Added the `0x88B9` control channel (remote arm/disarm) — fretboard now in sync with lemmy/lightshow.** Fretboard: `T1S_ETHERTYPE_CTRL` + opcode `T1S_CTRL_ARM (0x01)` in `t1s_detector.c`, RX dispatch implemented in `TC6_CB_OnRxEthernetPacket` (was a no-op — this node now consumes RX), new `T1SDetector_RemoteArm()`/`T1SDetector_LastCtrl()` accessors. `main.c` `actuation_armed()` makes the remote arm authoritative once seen (SW0 governs until then). `cli.c` `t1s` now shows ctrl rx count + arm source. marvin: `T1SLink_SendFretboardCtrl()` + per-opcode staging/flush (mirror of the lightshow channel) and a `fretboard arm|disarm` console command. Also fixed a stale "guitar id 2" comment in `t1s_detector.h` (code was already id 3). | Brings fretboard onto the same per-node control-channel pattern the animation/lighting nodes gained, and closes the long-standing coordination gap: marvin can now gate the detector's actuation from the bus (manual active-detector selection) instead of relying only on the node's local SW0. Remote-authoritative-once-seen keeps SW0 as a bench fallback while making marvin the source of truth in the integrated system. Automatic active-detector/guitar arbitration (marvin arming the chosen detector and silencing its own command path) remains the follow-up. |
| 2026-07-28 | **Fretboard's own node id renumbered id 1 → 4** (`t1s_detector.c` `T1S_NODE_ID`, MAC now `02:00:00:00:00:04`; marvin's `s_nodes[]` detector entry also → 4). `detector_id` (`DETECTOR_ADC_FRETBOARD`) and heartbeat `node_type` (1 = detector) are unchanged — only the PLCA id/MAC moved. Re-flash fretboard **and** marvin together (marvin filters by src MAC). | Completes the bus renumber to the canonical table (docs/t1s-podl-link.md §7.1): detector = 4. Frees ids 1–2 for the fauxmote controllers so a default-id-1 fauxmote no longer collides with the fretboard. The fretboard id lives at two coordinated points (its firmware + marvin's coordinator table); both must move together or marvin won't recognize the node's frames/heartbeat. |
| 2026-07-28 | **Peer-to-peer guitar target renumbered id 2 → 3** (`t1s_detector.c` `T1S_GUITAR_ID`, dst MAC now `02:00:00:00:00:03`). The fretboard's **own** node id stays 1 (target 4 renumber deferred — since done, see the 1 → 4 entry above). Part of the coordinated guitar-id move (guitar firmware + marvin table also go to 3). | The guitar node moved to its target id 3 (docs/t1s-podl-link.md §7.1); the fretboard drives the guitar directly (peer-to-peer), so its hardcoded destination has to follow the guitar or that command path misses the node. The fretboard's own 1→4 renumber is a separate coordinated step with marvin's table. |
| 2026-06-17 | **Keep the command path on the 240 Hz tick cadence (do NOT decouple it to immediate main-loop send).** The model emits one bitmask per 240 Hz sample; the ISR gates it into `s_current_cmd` each tick and the main loop sends that, so actuation is quantized to the ~4.2 ms grid. Leave it. To reduce the *new* T1S transport delay instead, raise the host SPI clock (1 MHz → ~12 MHz, both fretboard and guitar; SERCOM `BAUD=0` = GCLK/2 = 12 MHz) and/or fold a constant offset into the training labels — **not** by changing the command-to-tick coupling. | The model's timing was characterized against this inference→tick→actuate pipeline (the photo-dip→strum delay is baked into the distillation labels, so its output index is fit to *this* cadence). Decoupling would shift every actuation earlier by up to a tick **and** replace the deterministic 240 Hz grid with main-loop jitter — for a model trained on 240 Hz-sampled data, consistent grid-aligned latency beats lower-but-variable latency. Separately, the T1S move already changed timing vs. the old `MODEL_DRIVEN` *local-GPIO* standalone: actuation is now ~1.5–2 ms **later** (host-SPI chunk each end @ 1 MHz dominates; PLCA media access is sub-ms). That transport delta — not the tick — is the real new variable, so the knob is SPI speed (12 MHz removes ~1 ms) + re-validating gameplay timing on the T1S rig (scope fretboard `SetCommand`→guitar apply, check hit rate). Worst-case command latency today ≈ 6–7 ms (≈4.2 ms tick + ~1.5–2 ms transport); ~2–3 ms typical. See edge-ai journal + [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md). |
| 2026-06-17 | **T1S-only sense+actuate node; the model drives the guitar over T1S.** Removed the UART path and the `FRETBOARD_LINK` + `FRETBOARD_MODE` build flags entirely — one behaviour. The on-device model is always on; its inferred bitmask is (a) streamed to marvin as the data frame's `applied_mask` and (b) sent **directly to the guitar node** over T1S (peer-to-peer, edge-triggered + 50 ms refresh). New `T1SDetector_SetCommand()` + a command TX path/buffer in `t1s_detector.c` (guitar MAC `02:..:02`); `data_stream_send(applied_mask)` takes the mask as a param; the 21-byte model frame is dropped (marvin parses the 17-byte layout). SW0 arms actuation (interim manual active gate); `TC6_TX_ETH_QSIZE` 2→4 (data + command + heartbeat concurrent). | Supersedes the 2026-06-16 "Stage 1 only / model stays UART-bench telemetry" call: per Greg, **inferring actuator commands from the phototransistor data is the entire purpose of this detector**, so it must drive the guitar — and with the outputs gone, that path is T1S. Collapsing to a single T1S behaviour (no UART/flags) matches "clean it all up for T1S." Direct peer-to-peer keeps latency low and the guitar dumb (applies the last `0x88B5` frame from anyone). **Caveat:** no active-source arbitration yet — while the fretboard is armed, marvin must not also command the guitar (both address `02:..:02`); marvin-side active-detector/active-guitar coordination is the follow-up. |
| 2026-06-17 | **Actuator code removed — fretboard is detector-only.** The Wii-guitar button/strum GPIOs were removed from the MCC config; `cmd_receive.{c,h}` + the orphaned `fret_button.{c,h}` and `fret_detect.{c,h}` are deleted. `data_stream` no longer sources `applied_mask` from `cmd_receive` (plain frame = 0; the model frame takes the inferred mask as a parameter). `MODEL_DRIVEN` keeps running the on-device model but as **telemetry** — it streams its inferred bitmask and no longer drives GPIO; SW0 just gates whether the mask is streamed. Reverses the 2026-06-02 "keep the orphaned fallback modules" decision. | The actuator role is fully on the `guitar` node now, so the outputs (and the takeover-fallback `fret_button` path that drove them) have no hardware to drive — leaving the code would be dead and non-compiling (the `BUTTON_*` pin macros are gone from `plib_port.h`). Keeping the model as telemetry (rather than deleting it) preserves the edge-ai on-device validation path with no actuation; Stage 2 re-homes the inferred mask to a guitar node over T1S (direct peer-to-peer). |
| 2026-06-17 | **Fretboard becomes a T1S detector node (id 1) — firmware written.** New build axis `FRETBOARD_LINK = {UART, T1S}` in `fretboard_config.h` (orthogonal to `FRETBOARD_MODE`). The `T1S` build adds `t1s_detector.{c,h}` (TX mirror of guitar's `t1s_follower.c`; shared `third_party/oa-tc6-lib` + a `tc6-conf.h`): follower id 1 / MAC `02:..:01`, streams the existing 17-byte frame to the coordinator (`0x88B5`) + a 500 ms presence heartbeat (`0x88B6`, type 1). `data_stream_send()` routes to `T1SDetector_SendFrame()` instead of SERCOM1; `main.c` services the link from the main loop. **Detector-only:** the `T1S` build compiles out `cmd_receive`/model/button-drive (`FRETBOARD_MODEL_ACTIVE` gate) so the button GPIOs are free for SPI/CS/IRQ/RST. **Command plane for Stage 2 = direct peer-to-peer:** the active detector will TX its command straight to a guitar node's MAC; marvin coordinates active-detector/active-guitar and logs, but is out of the gameplay command path. | Executes the 2026-06-16 detector re-scope onto the bus, now that the guitar node is proven (guitar G1+G2). marvin's RX side was already built (`fretboard_link.c` T1S frame handler + node-table id 1) — the only missing half was the fretboard transmitting, so this is purely additive on the fretboard. Build flag keeps the UART path as the live fallback (same parallel-coexistence as marvin's `MARVIN_FRETBOARD_TRANSPORT` and guitar's split). TX staged in the ISR + flushed in the main loop because TC6 must not run in the 240 Hz ISR (same reason model inference moved out, 2026-06-04); drop-on-busy shows as a `sample_seq` gap, matching the UART drop-on-full semantics. Detector-only frees the button pins for the LAN8651 SPI (guitar reused exactly those pins) and matches the node-class split — actuation lives on `guitar`. Direct peer-to-peer keeps latency low and the guitar dumb (applies the last `0x88B5` frame from anyone); marvin's coordination guarantees one command source at a time. |
| 2026-06-16 | **Direction: fretboard re-scopes to a phototransistor *detector* node; the Wii-guitar *actuator* role moves to the new [`guitar`](../../guitar/SPEC.md) subproject.** Not yet executed — the firmware still does both roles (`fret_scan`/`data_stream` detector + `cmd_receive` actuator + `MODEL_DRIVEN` model) and **keeps actuating until the guitar node is proven**, then marvin flips its command target. No code change this pass. | Part of the T1S multi-node restructuring (top-level [`SPEC.md`](../../../SPEC.md) §2): sensing and actuation become separate node classes on one PLCA bus so multiple detector/guitar variants can coexist and marvin selects the active of each. Keeping fretboard working until guitar is validated holds the playing system up throughout (parallel-coexistence, like the UART/T1S build flag). Edge-ai `MODEL_DRIVEN` re-homing (detector infers → T1S → guitar) is deferred — model modes stay untouched for now. |
| 2026-06-04 | **Added `MODEL_DRIVEN` mode: on-device int8 inference (`model_infer.c/.h` + generated `model_weights.h`), SW0/LED0 enable toggle, and inference run in the main loop — NOT the TC0 ISR.** Active model is a swappable `model_def_t` pointer (`model_infer_set_model`). Build at `-O2`/`-O3`. | Standalone bring-up of the edge-ai model (edge-ai journal 2026-06-04). **Inference must not run in the 240 Hz ISR**: measured on hardware, doing so dropped the callback rate to ~150–190 Hz (so the model's 250 ms window/lag went wrong) *and* starved the interrupt-driven SERCOM TX (recv ~22 fps, heavy drops). Moving `model_infer_run()` to the main loop and having the ISR only sample + apply a `volatile uint8_t` latest-command restored clean 240/240 Hz. The pointer-swap model_def lets per-difficulty weights be selected at runtime and keeps the door open for serial-loaded weights (RAM headroom confirmed; 8 KB SRAM). SW0 (PB03 active-low momentary, debounced) toggles control; LED0 (PB02) shows state; boots disabled (outputs released). `MARVIN_DRIVEN` stays the alternate build for the I/O-bridge path. |
| 2026-06-03 | Data-stream frame grows 12→17 bytes: `0x03 \| g r y b o (5×u16 LE) \| sample_seq (u32 LE) \| applied_mask (u8) \| 0xFC`. `sample_seq` is a monotonic counter incremented once per tick in `data_stream_send` (before the TX-buffer check, so a dropped send shows as a gap); `applied_mask` comes from a new `cmd_receive_current_mask()` getter. Resolves open-questions #3 and #4. | Closes the edge-ai data-sync hole (edge-ai journal 2026-06-03). Pairing the actuator state with the ADC scan *in the same frame* makes label↔feature alignment atomic at the source, instead of marvin reconstructing it across its bursty USB RX and separate TX clocks — which Phase-2 training showed floored strum timing at ~20 ms. The seq counter lets the host reconstruct true 240 Hz ordering and detect dropped frames. Callback order is scan→send→receive, so `current_mask` at send time is exactly the state driven *during* this scan. Wire-format break: requires marvin's RX parser + `perf_rec_fretboard_raw_t` (schema v4) to update in lockstep. 17 B × 240 Hz = 4.08 KB/s, still ~12× under the 500 000-baud budget. |
| 2026-06-02 | Leave the orphaned `fret_detect.*` and `fret_button.*` modules in place (still in MPLAB fileSet, still building, no callers). Don't delete, don't move to a `fallback/` directory. | Marvin's 2026-05-20 decision keeps a "fretboard-takeover" fallback mode in scope — `fret_button.c` is the existing implementation of that mode and is cheap to keep around. Cost is small (a few KB of flash + the stale `#include "fret_detect.h"` in `data_stream.c`). Revisit if the takeover mode is formally dropped or if these files start drifting against a refactor. |
| 2026-06-02 | Loop period moved to 240 Hz, driven by TC0 callback (was 500 Hz from SYSTICK). | Game logic now runs on marvin; the host is the rate-setter and 240 Hz comfortably covers Guitar Hero note-onset timing while leaving SAM9X75 RX-side budget. TC0 callback removes any drift from a polled-SYSTICK loop. |
| 2026-06-02 | SERCOM1 baud raised to 500 000 (was lower). | At 240 Hz × 12-byte TX frames + sporadic RX command bytes, 500 000 baud (≈ 50 000 B/s usable) gives ~17× headroom over the 2 880 B/s steady-state — plenty for jitter and back-pressure without flow control. |
| 2026-06-02 | Data-stream frame is 12 bytes: `0x03 | g | r | y | b | o | 0xFC` (5×u16 LE). | Pressed-state booleans removed; with detection on the host, raw ADC values are the only payload that matters. Smaller frame ⇒ more TX headroom, simpler host parser. |
| 2026-06-02 | Command receive: drain RX each tick, apply only the **last byte** as a 7-bit button bitmask. | The host is expected to send commands at ≤ tick rate; coalescing avoids working through stale bitmasks if anything backs up. Single-byte format means no framing/CRC overhead in either direction — corruption window is one tick at worst. |

---

## Open questions

1. **No corruption signalling on the wire.** A bit-flip inside the 5 uint16 payload bytes is silently accepted — only out-of-range start/end bytes are caught. At 500 000 baud over EDBG-CDC this is probably fine, but if we ever see suspect detector behaviour, adding a Fletcher-16 byte (matching marvin's wire format) is a one-byte frame growth.

2. **No host→firmware framing.** Command stream is raw bitmask bytes with no start byte. A spurious byte (e.g. line glitch on RX) becomes a button command. Acceptable for now because the line is short and runs over the same EDBG-CDC pair as TX, but worth revisiting if we see ghost presses.

3. ~~**T1S link transport — direction, not yet built.**~~ **Resolved 2026-06-17 (see decision log):** the fretboard T1S detector firmware is written — `t1s_detector.{c,h}` (follower id 1, MAC `02:00:00:00:00:01`, ethertype `0x88B5` data + `0x88B6` heartbeat) behind the `FRETBOARD_LINK = T1S` build flag, reusing the shared `third_party/oa-tc6-lib`. The 17-byte frame rides the Ethernet payload unchanged (only the transport swaps, as planned). **Remaining (gated on Greg):** the MCC regen (a SERCOM in SPI-master mode + T1S_CS/T1S_RST/T1S_IRQ_N GPIOs + EIC on IRQ_N — mirror of guitar G0; the detector-only build frees the button pins) and on-hardware bring-up (target banner `LAN8651 up … PLCA follower id=1/8`). The T1S fileSet must add `t1s_detector.c`, `cli.c`, `third_party/embedded-cli/embedded_cli.c`, and `oa-tc6-lib` `tc6.c`/`tc6-regs.c`, plus the `libtc6/inc`+`src` and `third_party/embedded-cli` include dirs, and **exclude** `cmd_receive.c`/`fret_button.c` (button-pin refs that conflict with the SPI pins). PoDL is transparent to the MCU (zero firmware footprint); final PoDL BOM still open.

> Resolved 2026-06-03 (see decision log): #3 "no sample timestamp" and #4 "no applied actuator state" — both fixed by growing the frame to 17 bytes with `sample_seq` + `applied_mask`.

---

## Session log

### 2026-08-03 — Capture drop fix: skip inference while disarmed+streaming (main-loop cadence)

- **Symptom:** the first `commanded-fb` corpus (5 songs, ~284k rows) showed ~1.0–1.4% dropped frames — and crucially **every gap is exactly one frame** (avg drop length 1.0 across all songs), i.e. isolated single-frame drops, never bursts.
- **Root cause:** the data-frame staging is a single latest-wins buffer — the 240 Hz ISR stages one frame/tick, the main loop flushes ≤1/pass. When a main-loop pass exceeds one tick (4.17 ms), the ISR overwrites the un-flushed frame → that `sample_seq` is lost (documented at [`t1s_detector.h`](../t1s_detector.h) staging comment). The pass time is dominated by inference, which ran **every tick even when disarmed** — so during an `active cv` capture the node burned the main loop on a model whose output it discards (`cmd = armed ? s_latest_cmd : 0`), occasionally overrunning a tick. The single-frame gap length is the signature: one overrun = one dropped frame, then recovery.
- **SPI ruled out:** SERCOM0 host SPI to the LAN8651 is **12 MHz** (`SERCOM0_Frequency` 24 MHz, `BAUD=0` → 24/(2·1)). An 18 B frame ≈ 1 TC6 chunk ≈ 43 µs — negligible vs the tick. (Corrects the stale "1 MHz" note in the edge-ai 2026-06-17 entry.) So throughput isn't the bottleneck; main-loop cadence is.
- **Fix ([`main.c`](../main.c)):** gate inference on `run_infer = s_actuation_active || !T1SDetector_StreamEnabled()`. So the heavy step is skipped only in the exact capture case (disarmed **and** streaming); it still runs armed (gameplay) and disarmed-not-streaming (menus, keeps the model warm for an instant arm). The streaming engine still drains its ISR→main sample queue every pass (so it can't overflow) but skips the compute. Expected: main-loop pass drops well under a tick during capture → drop rate → ~0.
  - Edge note: a capture(disarmed+streaming, not stepping)→armed transition would leave the streaming model with stale state (brief warmup) — not a normal flow (capture is `active cv`, node stays disarmed; deploy play has streaming off). The recompute engine is unaffected (its ring is fed by the ISR every tick).
- **Pending:** reflash, recapture a short clip, confirm drop% ≈ 0 before re-capturing the full corpus. (First corpus was trainable as-is; a baseline model is training on it now.)

### 2026-08-03 — Atomic teacher label (`commanded_mask`, 18-byte frame, teacher opcode `0x04`)

- Hardware changed slightly (sensor gain/recalibration only — same 5 phototransistors, same
  positions), so the corpus needs recapturing and the model retraining. Rather than re-run the old
  cross-stream `--labels=actuator` join (~12 ms skew), added an **atomic teacher label**: marvin's CV
  command is stamped into the same frame as the ADC scan.
- Fretboard wire frame grew **17→18 B**: added `commanded_mask` at offset 16 (`end` → 17);
  `data_stream_send()` now takes `(applied_mask, commanded_mask)`. New `T1S_CTRL_TEACHER (0x04)`
  control opcode latches marvin's command into `s_teacher_mask` (un-gated by arm/stream);
  `T1SDetector_TeacherMask()` feeds it into every frame from the TC0 ISR via `main.c`.
- marvin side: `T1S_DET_CTRL_TEACHER (4)` + `OP_COUNT` 3→4; edge-triggered teacher send in
  `FretboardLink_Send` (T1S only, retried until it lands); `fretboard_link` parses + emits
  `commanded_mask`; `perf_log` reused its spare byte (record size unchanged), schema **v5→v6**.
- Host: `marvin-perf` decode/records + `--labels=commanded-fb` export mode (edge-ai's preferred
  distillation target); `edge-ai` loader accepts it verbatim. `marvin-perf` suite: 154 passed;
  edge-ai data-loader tests pass. (Two edge-ai bitexact C tests fail on a pre-existing
  `models.h`-vs-`model_weights.h` include mismatch from the 5-model plumbing commit — unrelated.)
- **Follow-on (2026-08-03 later, marvin-side only — node firmware unchanged):** `commanded_mask` now
  carries marvin's **released-style** per-note command, not the mask it drives. Marvin still plays
  back-to-back holds live on the wire, but strips the timing pipeline's legato fret-hold from the
  teacher label so the captured training data has no back-to-back hold artifact. The fretboard just
  latches whatever the `0x04` opcode sends, so nothing changed here; but a captured `commanded_mask`
  is the released-style command (the fretboard's own lazy actuator re-adds the physical hold). See
  marvin journal 2026-08-03 (later) + edge-ai decision log.
- **Pending (hardware-in-the-loop, foreground with Greg):** build+flash fretboard (both
  `MODEL_INFER_STREAMING` 0/1) **and** marvin (coordinated wire-format + schema break); capture the
  5-song corpus with marvin as active CV teacher; export `--labels=commanded-fb`; retrain →
  requantize → regenerate `model_weights.h` → reflash.

### 2026-08-02 — Fretboard drives fauxmote directly; `active` is the arbiter

- Made the fretboard the real game input: it now drives **fauxmote** (controller node id 1) directly
  over the `0x88B7` controller channel with an mf_proto GUITAR message, in addition to the guitar-node
  indicator. Verified the wire format first — the `0x88B7` body is `[TYPE][payload…]` with no
  SOF/LEN/CRC (MAC-PHY FCS covers integrity), and fauxmote's RX does not filter by source MAC — so no
  fauxmote firmware change was needed and the sender is a plain 4-byte frame.
- Copied the **full** shared `mf_proto.h` into the fretboard tree (third byte-for-byte-synced copy) and
  built a generic `send_fauxmote(type, payload, len)` + `flush_fauxmote()` mirroring the existing
  guitar-command path, so future whammy/accel are new calls (whammy pinned to `MF_WHAMMY_REST`, aux 0,
  both reserved). Reused the existing ARM gate (opcode `0x01`) for the new send — no new opcode; disarm
  emits one release then goes silent.
- marvin side: both `FretboardLink_Send` and `fx_tx_task`'s GUITAR slice now gate on
  `Detector_GetActive()==DETECTOR_CV_MARVIN_V1` (one all-release GUITAR frame on the cv→fretboard
  handoff). `active <cv|fretboard>` now calls `Detector_SetActive` **and**
  `T1SLink_SendFretboardCtrl(ARM, …)` — it is the single switch. `detect` trimmed to `cv` (dropped the
  dead `fretboard` variant); help/status strings updated. Registered `mf_proto.h` in the fretboard
  fileSet.
- Open follow-up unchanged: the local `arm`/SW0 override can still double-drive fauxmote if armed while
  `active==cv` (last-writer-wins with `active`) — documented manual-override hazard, not yet solved.
- **Not yet built/flashed** — MPLAB build of fretboard (both `MODEL_INFER_STREAMING` 0/1) + marvin and
  on-hardware handoff verification are the next step.

### 2026-08-02 — 5 selectable inference models (per-difficulty + auto), plumbing only

- Built the full runtime model-selection path so the fretboard can carry one model per Guitar Hero
  difficulty (`easy`/`medium`/`hard`/`expert`) plus a reserved `auto` slot for a future adaptive mode.
  Same NN arch for all; only weights/thresholds differ. Selectable over the local CLI **and** T1S (marvin).
- **Scope: plumbing only.** Per-difficulty corpora aren't captured, so real easy/medium/expert weights
  don't exist. The untrained slots alias to `&model_hard` (`#ifdef MODEL_HAVE_*`) and `auto` resolves to
  hard — all five are selectable and drive the guitar identically today. Training the real models is
  separate future edge-ai work (corpora + Greg in the loop; see `feedback_training_involvement`).
- **New `models.h`** (firmware-owned, not generated): difficulty→`model_def_t*` registry + `model_resolve()`
  (bounds-check, `auto`→hard stub, effective-difficulty out-param). Included only by the active engine TU,
  so weights still instantiate exactly once.
- **`model_infer.h`**: `MODEL_SEL_*` enum (`MODEL_SEL_DEFAULT = HARD`) + selection API; implemented as a
  wrapper in both `model_infer.c` and `model_infer_stream.c` (one public name each, only the compiled
  engine defines it — the other TU is empty under its `#if [!]MODEL_INFER_STREAMING` guard).
- **`t1s_detector.c/.h`**: control opcode `0x03` (`T1S_CTRL_MODEL`, arg 0..4) + `s_model_sel` byte +
  `T1SDetector_SetModelSel`/`_ModelSel` accessors. The module only holds the byte; **`main.c` applies it
  to the engine on change** (mirrors how `actuation_armed()` reads `T1SDetector_Armed()`).
- **`cli.c`**: `model [easy|medium|hard|expert|auto]` — no-arg lists all slots with placeholder / `auto`
  markers + the effective difficulty; a name selects it. 7th binding (cap is 8).
- **marvin**: `T1S_DET_CTRL_MODEL (3)` + `T1S_DET_CTRL_OP_COUNT` 2→3 (staging arrays auto-resize); a
  `fretboard model <difficulty>` console subcommand with a local name→index table (marvin doesn't share
  the fretboard enum).
- **Build/docs**: added `models.h` to the mplab fileSet; updated `SPEC.md` (control-channel opcode table,
  CLI list, new `model_infer` module section + File Map) and this journal. No edge-ai code change — the
  `emit_c_header(--name <diff>)` exporter already emits correctly-prefixed per-model headers.
- **Not built/flashed yet** — needs an MPLAB build in both `MODEL_INFER_STREAMING` 0 and 1 + a marvin
  build, then CLI/T1S smoke tests (all five select; behaviour parity since all → hard).

### 2026-08-02 — Sync pass: node id confirmed + `0x88B9` control channel (remote arm)

- Returned to fretboard to finalize the design; confirmed T1S was already brought up (link was UP on
  hardware 2026-06-17). This was a **sync/cleanup pass**, not a bring-up.
- **Node id already done in source, both trees:** fretboard `T1S_NODE_ID = 4` / guitar target 3, and
  marvin's node table already has `{ 4u, DETECTOR_ADC_FRETBOARD, T1S_NODE_FRETBOARD }`. The only remaining
  node-id action is a **coordinated re-flash** (fretboard + marvin together) — a hardware step. Fixed one
  stale "guitar id 2" comment in `t1s_detector.h`; no `FRETBOARD_LINK`/`cmd_receive` leftovers; `tc6-conf.h`
  diffs vs lightshow are legit (bigger TX queue for 3 concurrent streams).
- **Control channel added (the real divergence):** lemmy + lightshow had gained a per-node `0x88B9`
  control channel; fretboard hadn't. Added it with a **remote arm/disarm** opcode (fretboard side + marvin
  side + `fretboard arm|disarm` console command — see decision log). Fretboard now consumes RX for the
  first time (`TC6_CB_OnRxEthernetPacket` was a stub); the MAC filter already accepts unicast to its own
  MAC, same as lightshow's control RX, so no MCC/init change was needed.
- **Layout divergence noted, not changed:** fretboard keeps app sources at the top level + module named
  `t1s_detector.c`, whereas guitar/lemmy/lightshow use `config.mcc/src/` + `t1s_follower.c`. The name is
  semantically correct (it *is* a detector, not a plain follower) and the layout move is a bigger MCC
  refactor — left as-is.
- **Not built/flashed** — needs MPLAB build (the `0x88B9` RX path is new on this node) + on-hardware
  bring-up. Watch: `fretboard arm|disarm` from marvin flips the node's `arm:` line in the `t1s` CLI to
  `remote`, and the guitar responds; confirm the 240 Hz data rate holds with the extra RX traffic.

### 2026-06-17 — Link UP on hardware (detector heartbeat seen on marvin)

- The fretboard T1S node is **live on the bus**: `LAN8651 up … PLCA follower id=1/8`, and marvin's
  `nodes` shows the detector (id 1) present via its `0x88B6` heartbeat. First end-to-end fretboard↔marvin
  over T1S.
- **Bring-up blocker + gotcha (cost ~an afternoon):** the firmware hung in the vendored
  `TC6Regs_Init → DoInitialization` chip-rev wait (tc6-regs.c:338, no timeout) because **SERCOM0 SPI
  transfers never completed** — `spi_done_cb` never fired. Root cause: **MCC did not enable the SERCOM0
  APB clock** (`MCLK_APBCMASK` was `0x88c` = SERCOM1+TC0+ADC, missing bit 1 = SERCOM0; the guitar's
  `0x806` has it). With no APB clock, every SERCOM0 register access is a silent no-op (no bus fault — which
  is why the banner still printed), so the SPI never ran and its completion IRQ never asserted. SERCOM1
  (debug UART) had its bit, so logging worked throughout — masking the cause. **MCC showed the SERCOM0
  clock as enabled but wasn't emitting the mask bit; toggling/reasserting it in MCC fixed code generation.**
  Diagnosed by decoding `MCLK_APBCMASK` against `mclk.h` and one-shot `DIAG:` checkpoints in
  `spi_done_cb`/init (since removed). For the next node: if SPI bring-up hangs, **check the APB-clock mask
  bit for that SERCOM first.**
- Also surfaced/untangled the MCC tree move `fretboard-mcc/` → `config.mcc/` ("standard layout" cleanup).

### 2026-06-17 — T1S-only; model drives the guitar over T1S

- Greg's MCC config landed (reviewed: SERCOM0 SPI Mode 0, EIC EXTINT13, SysTick 1 ms) and was committed
  (`ac9109b`, "MCC regen for T1S … drop button outputs").
- Per Greg: the detector's purpose is to infer actuator commands and drive the guitar — so collapsed the
  firmware to a **single T1S behaviour** (removed UART + `FRETBOARD_LINK` + `FRETBOARD_MODE`). The model is
  always on; its inferred bitmask streams to marvin (`applied_mask` in the 17-byte frame) **and** goes
  straight to the guitar node over T1S.
  - `t1s_detector.c`: added the command-to-guitar TX path — `T1SDetector_SetCommand()` (main loop, edge-
    triggered) + `flush_command()` with a 50 ms refresh, guitar MAC `02:..:02`, its own staging buffer +
    busy flag + `cmd_tx_done`. `TC6_TX_ETH_QSIZE` 2→4 (data + command + heartbeat). Banner now "t1s
    detector + actuator". CLI `t1s` shows data-tx + cmd-tx counts + last cmd.
  - `data_stream.{c,h}`: `data_stream_send(applied_mask)` (param); dropped the 21-byte model frame and all
    `FRETBOARD_LINK` branching — always the 17-byte T1S frame.
  - `main.c`: single path — ISR scans + stages the data frame with the gated command; main loop runs
    inference → `s_latest_cmd`, forwards `s_current_cmd` via `SetCommand`, services TC6 + CLI. SW0 arms
    actuation (LED0 shows armed; boots disarmed → sends 0).
  - `cli.c` / `fretboard_config.h` / `model_infer*.h`: removed the build-flag guards + stale `CMD_BIT_*` /
    `MODEL_DRIVEN` comment references. Grep clean of `FRETBOARD_LINK`/`FRETBOARD_MODE`/`cmd_receive`.
- **Coordination caveat recorded:** no active-source arbitration yet — while the fretboard is armed, marvin
  must not also command the guitar. marvin-side active-detector/active-guitar selection is the follow-up.
- **Not built/flashed** — remaining: build-wiring (add `t1s_detector.c`, `cli.c`, `embedded_cli.c`,
  `tc6.c`/`tc6-regs.c` + include dirs to the fileSet) then on-hardware bring-up. Watch: data (240 Hz) +
  command (edge + 20 Hz refresh) + heartbeat all share the PLCA TX — confirm the data rate holds ≈240 Hz.

### 2026-06-17 — MCC for T1S done + actuator code removed (detector-only)

- **MCC regen reviewed — complete & correct.** SERCOM0 SPI master Mode 0 (`CPOL_IDLE_LOW | CPHA_LEADING_EDGE | DORD_MSB`, DOPO0/DIPO3) on PA04 MOSI / PA05 SCK / PA07 MISO; `T1S_CS`=PA15, `T1S_RST`=PA14 (GPIO, idle high); `T1S_IRQ_N`=PA13 / EIC EXTINT13, SENSE13=FALL, INTENSET bit13; EIC+SERCOM0 in NVIC; SysTick added (1 ms tick, `GetTickCounter()`=ms); SERCOM1 USART kept in ring-buffer mode (TX 512 / RX 128). Pins/macros/PLib names all match `t1s_detector.c` + `cli.c`. (SysTick was missing on the first regen pass — added.)
- **Removed the actuator entirely** (outputs gone from hardware): deleted `cmd_receive.{c,h}`, `fret_button.{c,h}`, `fret_detect.{c,h}`. Cleaned references — `data_stream.c` drops the `cmd_receive`/`fret_detect` includes (plain `applied_mask`=0; `data_stream_send_model()` now takes the inferred mask as a param); `main.c` drops `cmd_receive_*`; the model path streams its mask as telemetry instead of driving GPIO. Grep confirms no remaining `cmd_receive`/`fret_button`/`fret_detect`/`BUTTON_`/`STRUM_` references. The deleted files were already out of the MPLAB fileSet.
- **Remaining build-wiring** (T1S build): add `t1s_detector.c`, `cli.c`, `third_party/embedded-cli/embedded_cli.c`, `oa-tc6-lib` `tc6.c`/`tc6-regs.c` + the `libtc6/inc`+`src` and `embedded-cli` include dirs to the fileSet; define `FRETBOARD_LINK=FRETBOARD_LINK_T1S`. Then on-hardware bring-up.

### 2026-06-17 — Fretboard T1S detector firmware (Stage 1)

- Wrote the detector-node firmware behind a new `FRETBOARD_LINK = {UART, T1S}` build axis
  (`fretboard_config.h`), orthogonal to `FRETBOARD_MODE`:
  - `t1s_detector.{c,h}` — TX mirror of guitar's `t1s_follower.c`. Follower id 1 / MAC `02:..:01`,
    coordinator-MAC dst, ethertype `0x88B5` for the 17-byte data frame + `0x88B6` heartbeat (type 1 =
    detector). Bare-metal: SysTick clock, RST pulse, `SERCOM0_SPI_CallbackRegister`, GPIO CS held across the
    chunk, EIC IRQ_N, non-blocking `TC6Regs_Init(nodeId=1, follower, non-promiscuous)`, serviced from the
    main loop. `T1SDetector_SendFrame()` stages a frame in the ISR (latest-wins); `T1SDetector_Tasks()`
    flushes it + the 500 ms heartbeat from the main loop, one TX in flight (`s_tx_busy`). `tc6-conf.h`
    copied from guitar (PL10 sizing).
  - `data_stream.c` — `data_stream_send()`/`_send_model()` build the same frame, then hand it to
    `T1SDetector_SendFrame()` under `FRETBOARD_LINK == T1S` instead of `SERCOM1_USART_Write()`. `applied_mask`
    is constant 0 in this build (detector doesn't actuate; `cmd_receive.c` excluded).
  - `main.c` — `FRETBOARD_MODEL_ACTIVE` gate makes the model path mutually exclusive with the T1S build
    (button/SW0/LED0 GPIOs are repurposed for the LAN8651). T1S build: `T1SDetector_Initialize()` after
    `data_stream_init()`, `T1SDetector_Tasks()` in the main loop, ISR scans + streams only.
- marvin side needed **no changes** — `fretboard_link.c`'s T1S frame handler already parses the 17-byte
  frame from node id 1 (`detector_id == DETECTOR_ADC_FRETBOARD`) and emits `PERF_REC_FRETBOARD_RAW`; the
  node table + heartbeat-presence demux already have id 1. The `t1s` branch builds T1S by default.
- Decisions locked: command plane for Stage 2 = **direct peer-to-peer** (active detector → guitar MAC, marvin
  coordinates + logs); this iteration is **Stage 1 only** (detector streams; marvin drives the guitar).
- **Operator CLI on SERCOM1** (`cli.{c,h}` + vendored `third_party/embedded-cli/`, mirror of the guitar node).
  In the T1S build SERCOM1 is free (data moved to the bus), so it becomes an interactive console:
  `t1s` (link/sync/chipRev/PLCA/credits/tx/err), `adc` (latest 5-channel scan), `id` / `plca` (async MAC-PHY
  reg reads, enqueue-only per the guitar 2026-06-17 fix). Bare-metal: `CLI_Tasks()` drains the SERCOM1 RX ring
  each main-loop pass. Added `T1SDetector_GetState/NodeId/NodeCount/ReadId/ReadPlca` accessors. CLI compiled
  only under `FRETBOARD_LINK == T1S` (SERCOM1 is the data stream in the UART build). **MCC note:** keep
  SERCOM1 in ring-buffer mode with a TX ring ≥ 512 B so a `t1s` dump (~8 lines) isn't truncated.
- **Not built/flashed** — gated on the MCC regen (SERCOM SPI + T1S_CS/RST/IRQ_N, mirror of guitar G0) and the
  LAN8651 wiring. Bring-up watch: confirm marvin's `FRETBOARD_RAW` rate holds ≈240 Hz over the bus (the
  240 Hz TX is the new stressor vs guitar's 500 ms heartbeat) and the `nodes` command shows id 1 present.

### 2026-06-09 — T1S + PoDL link direction documented

- Evaluated moving the marvin link from SERCOM1 UART to **10BASE-T1S + dumb PoDL** (LAN8651B1 MAC-PHY each end). Feasible on the PIC32CM PL10: no IP stack, just SPI + the OPEN Alliance TC6 chunk protocol + a 14-byte L2 header; the existing 17-byte/1-byte frame formats ride inside the Ethernet payload unchanged. Est. ~6–10 KB flash / ~1–2 KB SRAM, one free SERCOM (SPI) + CS_N/IRQ_N/reset. Motivation: PoDL (power+data on one pair), noise/cable tolerance, PLCA multidrop, and a Microchip T1S+PoDL system demonstration — *not* bandwidth (UART has ~12× headroom).
- Documented system-level in [`../../../SPEC.md`](../../../SPEC.md) §5/§6/§7 and low-level in new [`../../../docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md). Added open-question #3 here; mirrored a note in the marvin journal (it gets its own bare-metal TC6 driver — no free netdev).
- **No code changes.** Direction only; revisit open sub-items (PLCA vs p2p, ethertype/MACs, UART fallback) before implementation.

### 2026-06-03 — 17-byte frame: sample_seq + applied_mask for edge-ai sync

- Grew the data frame 12→17 B (decision log). `data_stream.c`: added `sample_seq` (file-static `s_sample_seq`, incremented per tick before the TX-buffer guard) and `applied_mask` (from the new `cmd_receive_current_mask()` getter); `_Static_assert` 17; included `cmd_receive.h`. `cmd_receive.{c,h}`: exposed `current_mask` via the getter.
- Lockstep partners updated the same day: marvin RX parser (`fretboard_link.c`, `DS_FRAME_LEN` 12→17, extract seq+mask) and `perf_rec_fretboard_raw_t` (perf-log schema **v4** — see marvin journal), host `marvin-perf` decoder, and the `--labels=actuator-fb` exporter + edge-ai loader (windows within contiguous `fb_seq` runs).
- **Not yet built/flashed** — needs MPLAB + the rig. Deploy fretboard + marvin together (wire-format break). Then re-capture the Expert corpus and A/B the new atomic labels against the old cross-stream join (edge-ai journal).

### 2026-06-03 — Edge-AI design proposal authored, then promoted to its own subproject

- Initial draft landed here as `edge_ai_spec.md`, then moved to its own top-level subproject at [`tools/edge-ai/`](../../../tools/edge-ai/) (docs in [`tools/edge-ai/docs/`](../../../tools/edge-ai/docs/) — SPEC + architecture + training + runtime + rollout + review). Rationale: the work is mostly an offline Python / training-pipeline effort; phases 1–4 don't touch fretboard firmware at all, only phase 5 (cut-over) does. Living under `firmware/fretboard/docs/` framed it as a near-term fretboard plan when it's really its own development effort.
- Locked decisions (preserved across the move): output is 6 bits (5 frets + 1 collapsed strum bit; the wire byte still uses bit 5 for strum-down, bit 6 stays 0 — the up/down collapse is *at export time*, the raw `PERF_REC_ACTUATOR` capture keeps both bits so a future human-trainer corpus can preserve direction). Supervision is distillation against `PERF_REC_ACTUATOR.intended_mask` from marvin self-play. Intended deployment target is the fretboard PIC32CM, but offline phases are MCU-agnostic.
- The shape was picked specifically to dissolve the spatial-mismatch issue between `cv_marvin_v1` (sampling at `y=311`) and the photoxistor row: distilling against marvin's commands means the +220 ms strum delay is baked into the labels, so the model just learns whatever delay maps photo dip → strum from a wide-enough causal window — no mechanical alignment needed.
- No code changes. Open questions surfaced in [`tools/edge-ai/docs/review.md`](../../../tools/edge-ai/docs/review.md) (Q1: hold-vs-edge channel per fret, Q2: AR feedback, Q3: look-ahead vs. now-cast, Q4: strike-line CV detector as alt label source, Q5: photoxistor placement). Awaiting reviewer pushback before any of phase 1 (exporter `--labels=actuator` mode) lands.
- Top-level [`SPEC.md`](../../../SPEC.md) §3 has a new edge-ai row pointing at the subproject.

### 2026-06-02 — I/O-bridge spec rewrite + ds_monitor

- Confirmed actual architecture by reading sources: `main.c` has only `fret_scan_all` / `data_stream_send` / `cmd_receive_update` in the TC0 callback; `fret_detect.c` and `fret_button.c` are present but unreferenced.
- Wrote [`tools/ds_monitor.py`](../tools/ds_monitor.py): pyserial-based monitor that resyncs on `0x03 ... 0xFC` framing, prints actual frame rate (with % drift vs 240 Hz expected), bytes/s, framing-error drops, and current ADC values once per second.
- Rewrote [`../SPEC.md`](../SPEC.md) to reflect the I/O-bridge model: TC0 @ 240 Hz, 12-byte TX frame, 1-byte RX bitmask command, 500 000 baud. Old SPEC described the now-extinct on-device chord/strum pipeline.
- Created this journal (per top-level `CLAUDE.md`).
