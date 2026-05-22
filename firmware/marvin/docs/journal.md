# marvin — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for marvin. Newest entries at the top. For *what marvin is* (purpose, subsystems, interfaces, milestones), read [`spec.md`](spec.md) — this journal does not duplicate it.

---

## Current focus

**M1 — Reference detector v0.** First CV detector running on captured frames, publishing `detector_state_t` records onto the detector-state bus. No actuation yet. See [`spec.md`](spec.md) §4.2 for subsystem detail and §8 for the milestone progression.

Scaffold is in place as of 2026-05-20: `detector/detector.{h,c}` owns the bus queue + a temporary drain task that logs records-per-second; `detector/cv_marvin_v1.{h,c}` subscribes to the video frame queue and publishes one `detector_state_t` per frame with `frame_epoch = Video_FrameInfo.frame_count` and all-zero fret state. Next: pick the v0 algorithm (Q8) and start populating `fret[]`.

**Capture/display pipeline is done as of 2026-05-02:** source → TC358743 → CSI-2 @ 972 Mbps/lane → SAM9X75 → ISC DMA → DDR (BGRX32) → XLCDC OVR1 → LVDSC → 10.1″ 1280×800 LVDS panel.

> **Deeper-dive references:**
> - [`spec.md`](spec.md) — system-level spec (read first).
> - [`capture_pipeline.md`](capture_pipeline.md) — HDMI → DDR capture stage (CSI2DC, ISC, register settings, datasheet citations).
> - [`display_path.md`](display_path.md) — DDR → LCD display stage (XLCDC OVR1 wiring, pillarbox, cache coherence).

> **Earlier entries** (capture/display bring-up, 2026-05-01 → 2026-05-15) are preserved in [`journal-archive.md`](journal-archive.md).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-05-21 | `configMAX_PRIORITIES` bumped 5 → 8; tasks re-tiered into 4 active bands (UI=2, diagnostic+perf-wire=3, vision=4, strum-critical+host-wire=5) plus 3 reserve bands (1, 6, 7) and idle (0). MCC tasks re-prioritised via the per-component yml files so future regens preserve the layout. | Pre-change layout had 11 tasks bunched at priority 1 (9 MCC pollers + VideoTask + DetectorDrain) and 4 at priority 2, with no headroom. Two concrete problems: (a) VideoTask was *below* its consumer CvMarvinV1, a classic priority inversion that would bite during any priority-2 busy spell; (b) Legato (UI rendering) at the same priority as USB host driver tasks meant a heavy frame redraw could stall fretboard wire transit. The new layout puts strum-critical output above vision-real-time above diagnostic-I/O above UI, which matches the failure-cost ordering: a late strum is a missed note, a late detector tick recovers next frame, a late UI paint is jitter only. Reserve bands cost ~60 B BSS each, leave room for future watchdog or fault-recovery work to land without re-shuffling. Setting priorities in MCC yml (rather than patching `tasks.c` post-regen) avoids a re-apply patch — the source of truth and the emitted code now agree. |
| 2026-05-21 | `manual_control` lands as the second producer of `fretboard_link`, alongside `timing_pipeline`. Mode-level arbitration is two-mode (`timing_pipeline` vs `manual_control`) and embedded in `ManualControl_SetEnabled`: entering manual mode calls `TimingPipeline_SetEnabled(false)` then takes the wire; exiting reverses. | First concrete validation of the 2026-05-20 multi-producer `fretboard_link` design — `FretboardLink_Send` stays the only entry point; arbitration lives one layer up. Embedded toggle is enough for two producers; refactor to a dedicated `actuator_mode` arbiter when a third producer (game-state controller §4.8) arrives. `timing_pipeline.publish_mask` keeps updating internal state when gated so the next ≤5 ms tick after re-enable republishes a correct mask without a stale frame. |
| 2026-05-21 | Legato chosen for the manual-control surface; widgets authored in Microchip Graphics Composer (regenerated into `le_gen_screen_Screen0.{h,c}`); marvin-side code is a thin event-binding shim (`ui/manual_input.c`) | Composer makes the layout iterable in a tool rather than C source, and keeps our code out of MCC-clobbered files: only the binding shim references the Composer-generated widget pointers. **Does not commit spec Q5 for the full operator UI** — that's still M5's call. The thin-shim shape works equally well behind a future custom GFX2D UI by swapping the bind module. |
| 2026-05-21 | Manual-control input model is single-finger momentary; multi-finger chord input is out of scope for this surface | Legato's input pipeline (`legato_input.c:340/429`) routes each touch to a single focus widget, so a second simultaneous touch cancels the first button's release and strands its `pressed` state. Workarounds (custom maxtouch dispatcher; toggle-style frets) are real but not worth the complexity here — primary use case is game-menu navigation, which is single-finger by nature. Revisit only if a user-facing flow needs held chords. |
| 2026-05-21 | `fret_t` lives in `game/fret.h`, a top-level domain header. Detector / actuator / UI all include it directly; detector.h re-includes it so `detector_state_t` still compiles cleanly. | The fret colors (G/R/Y/B/O) are a Guitar Hero / Rock Band domain primitive, not a detector concept — the actuator and UI shouldn't reach into `detector/` for them. `game/` also stakes out the directory the future game-state controller (spec §4.8) will land in, so we don't have a transient home. Header-only, no cmake change. |
| 2026-05-20 | Relax Microchip's host CDC-ACM Communications-interface protocol check to accept `bInterfaceProtocol == 0x00 (None)` in addition to `0x01 (AT_V250)` | The fretboard hardware exposes its UART to marvin via the on-board EDBG debugger's CDC ACM (composite VID=0x03EB PID=0x2175: HID + CDC ACM IAD + vendor + MSC). EDBG declares the comm interface with `bInterfaceProtocol=0x00` rather than the AT-command protocol Microchip hardcodes. The TPL/IAD match accepts the device (host stack does call `interfaceAssign`), but the per-interface check inside `F_USB_HOST_CDC_InterfaceAssign` then rejects the comm interface, the interrupt pipe never opens, the CDC instance flips to STATE_ERROR, and our attach handler never fires. Both are CDC ACM in practice and Linux/macOS accept either. Two-line OR per check in `usb_host_cdc.c`; logged as patch #7 in the re-apply list. |
| 2026-05-20 | `USB_HOST_Initialize` ordering relative to `DRV_USB_EHCI/OHCI_Initialize` and `Legato_Initialize` is **not** load-bearing — keep MCC's emitted order | A prior session hand-edited `initialization.c` to move `USB_HOST_Initialize` *after* the EHCI/OHCI driver inits on the theory that the host layer needed the HCD interfaces present at init time. Empirical retest with the CDC diagnostic logging in place: MCC's original order (`USB_HOST_Initialize` → `Legato_Initialize` → driver inits) enumerates and binds the CDC IAD just as well. The host layer evidently looks up HCD interfaces lazily at `USB_HOST_BusEnable` time, not at `USB_HOST_Initialize`. Reverted the hand-edit; one fewer re-apply patch to maintain. (This is unrelated to patch #6, which is the *peripheral* init order — MMU/AIC ahead of TC0/FLEXCOM6/XLCDC — that one is still a real regression and stays in the list.) |
| 2026-05-20 | Marvin replaces the fret-tuner PC as the runtime brain | One-host architecture. fret-tuner persists as an off-band dev/calibration tool. fretboard stays a dumb relay (ADC out, bitmask in). |
| 2026-05-20 | Marvin is the *reference detector* — not a debug aid | HDMI-direct CV is ground-truth for the system. Fretboard's photo-detection and any future Edge-AI MCU are trained against marvin's output. Recording reference data is therefore a first-class subsystem (spec §4.6), not an afterthought. |
| 2026-05-20 | Multiple coexisting detectors are in scope; one canonical detector-state bus | At minimum `cv_marvin_v1` (CV) and `adc_fretboard` (over UART ingest). Bus shape: typed `detector_state_t` records on a FreeRTOS queue, single-consumer (timing pipeline). Recording snoops via tee from each producer. Spec §4.2. |
| 2026-05-20 | Timing pipeline (chord window, FIFO, strum scheduling) lives on marvin by default; fretboard-takeover is a fallback mode | Centralized = single source of truth, easier multi-detector fusion. Latency budget allows a few ms of UART jitter. Fallback mode disables marvin's pipeline; fretboard runs its own existing `fret_button.c` and feeds emitted-command telemetry up to marvin for capture/display. Spec §4.4 + §6. |
| 2026-05-20 | Reference-data export = SD card, format = detector-state + sparse keyframes | SD chosen over Ethernet/USB CDC because primary consumers are Edge-AI training (offline) and replay/sanity-check (offline). Live side-by-side comparison deferred. Format: dense `detector_state_t` records for all detectors + raw BGRX32 keyframes every 60 frames (1.38 MB/s sustained, comfortable on Class-10). No software JPEG (Cortex-A5 can't keep up; SAM9X75 has no HW encoder). Spec §4.6. |
| 2026-05-20 | `frame_epoch` (32-bit, incremented per captured ISC frame) is the master sync token across recordings, detectors, and any future off-board observers | Need single shared timeline; off-board detectors (e.g. Edge-AI device) record in their own format keyed by marvin's `frame_epoch` so post-hoc training-data alignment is trivial. Wraps after ~830 days @ 60 Hz. |
| 2026-05-20 | Operating modes are independent toggles (`detect_enable`, `marvin_timing_enable`, `actuate_enable`, `record_enable`); named modes are presets over them | Matches `tools/fret-tuner/SPEC.md`'s detect/actuate split which has worked well in practice. Avoids a brittle global state machine. Spec §6. |
| 2026-05-01 | First real driver work is the TC358743 HDMI-to-CSI bridge | It's the gate between hardware-wired and pixels-in-memory. Everything downstream (vision, actuation) depends on it. |
| 2026-05-01 | Reference source = mainline Linux tc358743 driver + community 480p dumps | Linux driver has correct state machine / EDID / IRQ handling; community dumps have verified 480p register values. |
| 2026-05-01 | Integration shape (sensor vs standalone) deferred until Phase 3 | Avoid premature structure; real answer depends on how much state the driver actually needs. |
| 2026-05-01 | Journal lives at `firmware/marvin/docs/journal.md` | Firmware docs co-located with firmware; top-level `docs/` is hardware/mechanical. |
| 2026-05-01 | TC358743 I2C address: **`0x0f`** (7-bit) | Matches the in-tree kernel DT example (`imx6q-h100.dts:190`, `tc358743@f`) and the Waveshare v1/v2 default tie-off. Confirm by I2C bus scan during Phase 1 bring-up. |
| 2026-05-01 | REFCLK: **27 MHz** | Waveshare v1/v2 on-board XTAL. Kernel driver restricts REFCLK to {26, 27, 42} MHz (`tc358743.c:717-719`); 27 MHz is the Waveshare v1/v2 value. |
| 2026-05-01 | Target CSI-2 output format: **RGB888** (MIPI data type `0x24`) | Marvin pipeline is already configured for RGB888 end-to-end: `CSI_DATA_FORMAT_TYPE = CSI2_DATA_FORMAT_RGB888`, ISC input RGB 8-bit, CSI2DC pipes RGB888, ISC output ARGB32. Emitting YUV from TC358743 would require reconfiguring ISC/CSI2DC — staying with RGB is cheaper. |
| 2026-05-01 | Register-access helpers will be a direct port from the kernel driver | `tc358743.c:131-269` has clean `i2c_rd/wr` + `i2c_rd8/wr8/rd16/wr16/rd32/wr32`. Map to FLEXCOM6 TWI underneath. |
| 2026-05-01 | J29 (22-pin MIPI) pinout confirmed; PC19=pin 17, PC15=pin 18 (not 5/6) | Per SAM9X75-Curiosity User Guide §3.4.10 (queried via Microchip MCP). Pins 5/6 are MIPI_D1_N/P — CSI data lane 1 diff pairs, not GPIOs. Correct GPIO pins for RESET/INT are pin 17 (PC19, `MIPI_CSI_GPIO0`) and pin 18 (PC15, `MIPI_CSI_GPIO1`). I2C on pins 20 (PA25, `CAM_I²C_CLK`) and 21 (PA24, `CAM_I²C_DATA`) confirmed to match marvin's FLEXCOM6 pin assignment (`pin_configurations.csv:85-86`). |
| 2026-05-01 | Phase 1 will drive RESET via software (I2C `SYSCTL.Sreset`), not via GPIO | Even if PC19 reaches the 15-pin side through the adapter, Waveshare v1/v2 typically does NOT route TC358743 `RESET_N` out to the 15-pin FFC — the chip's reset is an on-board RC/POR network. Software reset over I2C is always available and is what the kernel driver uses. Means we don't need to validate the GPIO path before Phase 1 bring-up. |
| 2026-05-01 | Delays use Harmony's `SYS_TIME` service (non-blocking shape) | Already initialized in `SYS_Initialize`; existing marvin code (`drv_isc.c`, `plib_isc.c`) uses it with `SYS_TIME_DelayUS/MS` + `SYS_TIME_DelayIsComplete`. Non-blocking pattern inside our state machine keeps `APP_Tasks()` (and Legato) from stalling during reset hold times, and scales naturally into Phase 3's longer init sequence. |
| 2026-05-01 | Our I2C open uses `DRV_IO_INTENT_READWRITE`, not `EXCLUSIVE` | `EXCLUSIVE` is rejected by Harmony's I2C driver if any other client is already open. Even after libcamera is removed we prefer `READWRITE` so our module can coexist with any future MCC-generated I2C client. `DRV_I2C_CLIENTS_NUMBER_IDX0 = 2` in `configuration.h` already permits multi-client use. |
| 2026-05-01 | Libcamera (Camera Module + Image Sensor Driver + Vision Camera Library) to be removed from MCC | Conflicts with TC358743 bridge approach: libcamera expects a directly-attached sensor (IMX219/OV5640/etc.), probes them at boot (adds bus traffic + console noise), and configures ISC/CSI/CSI2DC with sensor-style assumptions that we'll need to override in Phase 3. Keeping CSI/ISC/CSI2DC peripheral/driver layers — we'll drive those directly from the TC358743 module. Greg regenerating MCC. |
| 2026-05-01 | Post-regen: keep a minimal `drv_image_sensor.h` shim at the original path | ISC driver (`drv_isc.c:15, 246, 257, 363-365`) and `configuration.h:106-107` reference `DRV_IMAGE_SENSOR_*` enum values that MCC didn't scrub when the image-sensor component was removed. Shim defines just the enum values used (exact numeric equivalence to originals). Lives at the MCC-generated path because `drv_isc.c`'s include is hardcoded — but MCC no longer regenerates that directory, so the shim is stable. |
| 2026-05-01 | `CAMERA_ENABLE_DEBUG=0` provided via `user.cmake` compile definition | Both `drv_csi.c:45` and `drv_isc.c:18` define `debug_print(...) if (CAMERA_ENABLE_DEBUG) fprintf(...)` and the symbol used to come from the deleted `camera.h`. Define it as a compile flag instead of editing `configuration.h` (MCC-regenerated) — `user.cmake` survives regens. Value `0` elides the debug prints entirely; if we ever need them, bump to `1`. |
| 2026-05-01 | `csiBitRate = 0x14` set from `isc_capture`, not MCC | `drv_csi.c` hardcodes `csiBitRate = 0x16` (wrong — testing showed 0x14 works). Rather than modifying MCC-generated code, we treat the MCC value as a default and override in `isc_capture` alongside the already-necessary `csiFrameWidth/Height/Fps` overrides. Only one MCC-file modification stays (`plib_csi.c` `CSI_Analog_Init` bug fix). |
| 2026-05-01 | ISC Bayer blocks auto-bypass for RGB input (previously an open question) | `drv_isc.c:363-365` disables CFA/WB/Gamma/CSC/Sub422/Sub420 whenever `inputFormat == DRV_IMAGE_SENSOR_RGB` — no MCC config changes needed despite `ISC_ENABLE_DPC/GDC/WHITE_BALANCE/GAMMA` remaining `true` in `configuration.h`. Those flags are Bayer-path only and are ignored in the RGB branch. |
| 2026-05-02 | Capture pipeline outputs BGRX32 natively (CSI2DC RMS=0 + ISC RLP BYPASS + DMA PACKED32) | Datasheet §49.6.54 + §50.6.19 + §50.6.20 show this is the only in-pipeline path to 32 bpp for MIPI RGB888 bypass. ARGB32 RLP mode is unusable on this path (requires CSC module output format). Alpha=0x00 is acceptable trade for zero-CPU capture; 0xFF can be added post-hoc if needed. Full rationale in `capture_pipeline.md`. |
| 2026-05-02 | Limited→full-range RGB expansion will be done in HEO CSC block on the display side only; capture buffer stays unexpanded for vision consumers | Both Wii and Pi emit RGB limited-range (16–235). Expanding on capture would cost CPU and break vision consumers by inflating values without adding information (linear rescale carries no new signal). LCD display needs full-range 0–255 or it looks muted, so the HEO CSC matrix (programmable `M × in + offset`) will be configured as an identity RGB→RGB with limited→full gain/offset. Zero CPU, display-only, capture untouched. See `display_path.md` §5. |
| 2026-05-20 | M1 detector scaffolding: one task per detector, single shared bus queue (`xDetectorStateQueue`), temporary in-module drain task until the timing pipeline (M3) lands | Matches spec §4.2.4. Per-detector tasks isolate slow detectors from the frame pipeline; the shared queue is the hand-off point that the timing pipeline will eventually own as sole consumer. The drain task lives in `detector.c` so it travels with the bus and can be deleted in one place when M3 wires the real consumer. Records-per-second log line gives a coarse health signal during bring-up. |
| 2026-05-20 | `frame_epoch` is sourced from `Video_FrameInfo.frame_count` for M1; a persistent counter that survives ISC restart is M5's problem | Recording (M5) is the first consumer that actually needs cross-restart continuity. Until then the post-arm-monotonic counter is fine for plumbing validation, and pretending otherwise would mean threading another counter through the video module before it has a real consumer. |
| 2026-05-20 | Detector control model: per-detector `enable` (multiple may be on simultaneously for side-by-side recording) + single `active` selector (which detector_id the timing pipeline acts on) | Want side-by-side training data — every enabled detector publishes onto the bus; recording (M5) saves them all. But only one detector should ever drive actuation, so the timing pipeline (M3) filters the bus by `Detector_GetActive()` and ignores the rest. Cleaner than a global enum because it decouples "is this running?" from "is this canonical?", and lets us hot-swap the active detector without disabling its peers. Defaults: all-disabled, active=cv_marvin_v1. App explicitly calls `Detector_Enable` + `Detector_SetActive` after init. |
| 2026-05-20 | Non-detector vision tasks (menu navigation, score reading, etc.) are video-frame consumers, not detectors — `detector_state_t` stays narrow to fret-press shape | Forcing menu/score readers through `detector_state_t` would either abuse `fret[]` as untyped data or push the canonical record toward a tagged-union shape that hurts the recording layout. Cheaper to keep two separate concepts: (a) "video frame consumer" — anyone subscribing to the video module's frame queue, output type and downstream consumers are theirs to define; (b) "detector" — narrow note-timing emitter onto `xDetectorStateQueue`. cv_marvin_v1 happens to be both. Implication: `Video_SubscribeFrames` needs to go from single- to multi-subscriber before the second video CV task lands. |
| 2026-05-20 | `Video_FrameInfo.buffer` now reflects the buffer that just completed (per-frame routing); HEO is re-pointed in the ISR every frame so the panel sees every captured frame | Pre-fix: `ISC_Capture_GetBufferAddress()` returned the framebuffer base regardless of which descriptor-ring slot ISC just wrote, and HEO was bound once to the base — so HEO only ever read slot 0, and slot 1 was written but never observed. Effective behavior was ~half-rate display. Fix: ISC IRQ computes `slot = (frameIndex - 1 + N) % N` (driver increments before callback per drv_isc.c:47-50), passes `base + slot * frame_size` into the user callback, and video.c re-points HEO at that address with `XLCDC_SetLayerAddress(..., update=true)` — latches at next vsync. Also unblocks ring depth > 2 since each subscriber sees the genuinely-just-written buffer. |
| 2026-05-20 | `Video_SubscribeFrames` is multi-subscriber (max 4), each subscriber chooses its own queue depth + back-pressure policy | Need parallel video CV tasks (note detector + recording + future menu/score readers) on one capture pipeline. Static array of 4 `QueueHandle_t` slots; ISR fans out one `xQueueSendFromISR` per subscribed slot. Aligned-pointer slot reads/writes are atomic on Cortex-A5 so the IRQ can scan without locking; `taskENTER_CRITICAL` only needed in `Subscribe`/`Unsubscribe` to prevent two task-side writers racing on the same empty slot. Producer always uses `xQueueSendFromISR` (drop-on-full); consumer chooses queue depth — depth 1 = "newest only", deeper = "preserve every frame". |
| 2026-05-20 | All marvin-owned FreeRTOS objects use `xTaskCreateStatic` / `xQueueCreateStatic` / `xSemaphoreCreateMutexStatic`; FreeRTOS heap stays at heap_1 only because MCC code (XLCDC/MAXTOUCH/LEGATO/SYS_INPUT tasks, OSAL, Legato variable heap) needs it | Project rule is static-only memory allocation. MCC-generated code can't be made static without forking, so we keep `configSUPPORT_DYNAMIC_ALLOCATION = 1` for MCC and route all our own code through the static APIs. MCC reconfigured to enable `configSUPPORT_STATIC_ALLOCATION = 1` and emit `vApplicationGetIdleTaskMemory` in `freertos_hooks.c`. Per-module `Static{Task,Queue,Semaphore}_t` plus stack/storage arrays at file scope. |
| 2026-05-20 | ISC framebuffer pool moved from `.region_cache_aligned` to `.region_nocache`; max capture cap dropped 1920×1080 → 1280×720 to fit | First CPU consumer of frame data (cv_marvin_v1) needed cache-coherent reads. Two paths considered: invalidate D-cache per frame in the ISR, or move the pool to the linker's already-defined uncached region. Nocache is simpler and bug-resistant — every reader sees DMA-fresh bytes without explicit maintenance. Trade is full-DDR-fetch on every read; fine for sparse 5×5 patch sampling, would matter for a full-frame scan. `ram_nocache` is 16 MB so the 4-buffer × 1080p pool (≈24 MB) had to shrink; capping at 720p (≈11 MB) matches our current TC358743 ceiling and leaves headroom. Reverting to 1080p later requires either growing `ram_nocache` in `ddram.ld` or going back to cached + manual invalidate. |
| 2026-05-20 | cv_marvin_v1 keeps detector internals (press_count, raw color distances) private; bus record stays canonical (`pressed[] / confidence / raw_value`) | Different detectors need different per-frame data, but downstream bus consumers (timing pipeline, recorder, fretboard link) only care about "is this fret pressed at frame N." Pushing detector-specific richness onto the bus would either grow the canonical record or force a tagged-union shape that complicates recording layout. Encapsulating internals in the detector module preserves the narrow bus and lets each detector evolve its own state independently. press_count specifically lives in a private `s_press_count[FRET_COUNT]` array; if the strum scheduler later wants it, expose via a getter or a second "detail" channel. Recording-format choice (custom packed binary vs nanopb vs CBOR) deferred to M5. |
| 2026-05-20 | cv_marvin_v1 sensor coords are in native capture-frame space, no 1920×1080 reference scaling | fret-tuner detect_video.py carries a 1920×1080 reference frame and scales coords at runtime — that frame size is what the Elgato HDMI-to-USB bridge produced, not a per-camera variable. Marvin uses the TC358743 (different bridge chip, same direct-pixel topology) and captures at the source's native resolution. The Wii is the primary production source at 480p60 = 720×480, so the static defaults are scaled from Python's 1920×1080 values by (3/8, 4/9) once at port time and stored in 720×480 space. Same-pixel-strike-line geometry as the Python detector — these are real coords, not placeholders. Calibration UI (M6) will refine them. |
| 2026-05-20 | Fretboard-link submit queue belongs to `fretboard_link`, not `timing_pipeline`; producers call `FretboardLink_Send(mask)` | Game menu control (spec §4.8) needs to drive the actuator without going through the chord-window scheduler — its inputs are "navigate menu, press start", not gameplay notes. If the queue lives in `timing_pipeline`, the controller has to either reach across modules or duplicate transport. Moving ownership down to `fretboard_link` makes timing_pipeline one of N producers; the menu controller and any manual-test path call the same `Send` API. Latest-wins (depth-1 + xQueueOverwrite) already gives the right behavior — newest submitter wins on the wire. **Mode-level arbitration** (which producer is allowed to submit when, e.g. timing-only during gameplay vs controller-only in menus) lives one layer up; the link stays dumb. |
| 2026-05-20 | M2 fretboard link split into three layers — `detector → timing_pipeline → fretboard_link` — with the timing FIFO living on marvin and a 1-byte bitmask wire format | (a) Three-layer split keeps each concern small: detector emits state, timing_pipeline owns chord-window + strum scheduling, fretboard_link owns the USB CDC byte channel. Each can be replaced independently (different detector, different actuator transport) without touching the others. (b) Timing FIFO on marvin, not fretboard: marvin already runs the canonical detector and the recording pipeline, so co-locating timing keeps a single source of truth and avoids re-deriving state on the actuator side. Spec §4.4. The fretboard-takeover fallback (spec §6) is a separate mode where marvin disables its pipeline and fretboard runs `fret_button.c` standalone — that's the high-availability path, not the steady-state. (c) 1-byte bitmask matches `tools/fret-tuner/actuator.py` and `firmware/fretboard/cmd_receive.h` exactly (G/R/Y/B/O at bits 0..4, strum-down bit 5, strum-up bit 6) — no reason to invent a new framing while the protocol is this small. Latest-wins semantics on the marvin→fretboard queue (depth 1 + overwrite) so a stalled USB write can't accumulate stale chords. (d) **Actuator-latency compensation deferred until mechanical actuators arrive.** Today's actuator is direct GPIO assertion on the fretboard PCB — latency ≈ 0, so today's design is sound. Future paths considered: (1) marvin compensates by subtracting a fretboard-published latency from `assert_at`/`strum_at`; (2) fretboard compensates by accepting timestamped events and scheduling locally. Path 2 needs a richer wire format (event + abs-timestamp); path 1 keeps the byte protocol but pushes the timing math up. Pick when we know which actuator (voice coil / electromagnet / DIY solenoid) we're driving. |
| 2026-05-20 | Game-state awareness & high-level game control is a marvin subsystem (spec §4.8). Distinct from the gameplay note-detection path: a video-frame consumer (not a detector) emits typed game-state events on `xGameStateQueue`, and a controller layer translates verbs ("start single-player song X") into fret/strum sequences sent over the same fretboard link | Want marvin to drive whole-session play, not just notes once gameplay starts. Game-state observation belongs on marvin because it's CV on the same captured frames. Separating its bus from `xDetectorStateQueue` keeps `detector_state_t` narrow per the 2026-05-20 multi-detector decision. Per-game catalog (menu graph + song list) is metadata kept on marvin. Default arbitration with timing pipeline: mutually exclusive — controller runs only outside `gameplay` state. Phasing: M9 observer-only, M10 control. Open: recognizer algorithm (Q10), arbitration boundary (Q11). |

---

## Open questions

_(Questions we haven't answered yet. Move to decision log with rationale once resolved. System-level open questions are tracked in [`spec.md`](spec.md) §9 — anything tagged with `Q1..Q9` there.)_

- ~~CSI-TX minimum bitrate at 480p60~~ — **resolved 2026-05-01**: we ended up dropping to 297 Mbps/lane (below the kernel's tabulated 594 Mbps) with no problems. 297 pairs with host HSFREQRANGE band 0x14 and the kernel's 594 Mbps D-PHY timings happen to still be safe at this rate.
- ~~`TXACT` absent in CSI_STATUS after Phase 4 enable~~ — **deferred 2026-05-01**: TXACT has been observed at both 0 and 1 at various times after enable. Likely momentary (active-packet indicator, not stream-on flag). CSI_ERR=0 and HLT=0 consistently. Phase 5 frames-in-memory confirmed the chain works regardless.
- ~~Source mode variability~~ — **explained 2026-05-01**: 1440x240p observation was Wii post-reset default. Put Wii in 480p mode; we stay on 720x480p@60.

**Carried into future sessions:**

- **FreeRTOS analytics — kernel flags now on, dump path still TODO.** 2026-05-21: `configGENERATE_RUN_TIME_STATS`, `configUSE_TRACE_FACILITY`, `configUSE_STATS_FORMATTING_FUNCTIONS`, `INCLUDE_uxTaskGetStackHighWaterMark` enabled via MCC; run-time counter wired to `SYS_TIME_CounterGet` in `FreeRTOSConfig.h` (patch #8). Still missing: an on-demand way to surface the data (UART command? UI button? log on idle hook?), and a periodic `PERF_REC_TASK_HIGHWATER` emission on the perf-log channel. Pull `vApplicationStackOverflowHook` into the same scope: MCC's default is a silent spin, so an actual overflow is indistinguishable from any other freeze — patching the hook to emit task name + spin (carefully, since the stack is already corrupt) belongs with the rest of the analytics work. Already bit us once: 2026-05-21 freeze in `open_cdc()` was first misdiagnosed as a stack overflow because we had no visibility into actual stack usage of the FBL task.

- **Legato `LE_MEMORY_MANAGER_SIZE` adequacy.** Legato has its own internal pool (`LE_MALLOC` per touch event in `leInput_InjectTouchDown`). With the manual-control surface adding 8 buttons and frequent press/release events during menu nav, confirm `legato_config.h` `LE_MEMORY_MANAGER_SIZE` has headroom for typical event bursts. Watch for Legato heap-exhaustion symptoms (silent dropped events, widget redraw glitches) once the UI is exercised on hardware.

- **Single-screen attach assumption for `ui/manual_input`.** `screenHide_Screen0` deletes the root widget tree, taking the Composer-generated `Screen0_*` widget pointers with it. We bind callbacks once after the first `screenShow_Screen0`. If a second Legato screen is ever added, `ManualInput_Bind` must re-run inside that screen's show path (or whichever screen owns the manual-control widgets). Not an issue today — flagged for whenever the operator UI grows beyond Screen0.

- **Spec ↔ implementation drift on capture format.** Spec still uses "BGRX32" in ~10 places (§2 status table, §2.1 diagram, §3.1 hardware list, §4.1 video task, §4.2 detector input, §4.6.5 keyframe filename + size math, §4.7 sample config). The actual capture has been BGR888 packed (3 B/pixel) since the 2026-05-02 switch (`isc_capture.c:65-74`). Fixed §4.2.2 inline 2026-05-20 because M1 reads against it; the rest needs a sweep — including recalc of keyframe size (720×480 × 3 = 1.04 MB, not 1.38 MB) and on-disk recording layout. Defer to a doc-only pass before M5.

- **MCC-file modifications maintenance risk.** A from-scratch MCC regen on 2026-05-15 confirmed **four** local modifications get clobbered. The 2026-05-20 USB-host regen added a fifth. Re-apply after every regen:

  1. **`plib_csi.c` — `CSI_Analog_Init` refactor.** MCC emits per-lane HS-RX init inline with three bugs: Lane 1 in the 2-lane branch is missing the bit-rate write, Lane 2's lane-select code is `0x24` instead of `0x64`, and the 4-lane else-if branch skips Lane 1 entirely. Replace with the `csi_phy_hs_rx_init(lane_code, bit_rate)` helper and call it for lanes 0/1/2/3 from a flat `if (nlanes >= …)` chain.

  2. **`plib_xlcdc.c` `XLCDC_EnableClocks` — `PMC_PLL_ACR`.** Set to datasheet-optimal `0x12023010` for the LVDSPLL with our 24 MHz reference (in the `fIN ∈ [20 MHz, 32 MHz]` band). Specifically: `LOOP_FILTER=0x12, LOCK_THR=0x2, UTMIBG=1, UTMIVR=1, CONTROL=0x10`. MCC's default emits a different (less-optimal) analog config (`LOOP_FILTER=0x1B, LOCK_THR=0x4, no UTMI bits`). Doesn't affect functional behavior in our use (50 Hz refresh works either way), but is the manufacturer-recommended jitter/lock optimum.

  3. **`plib_csi2dc.c` `CSI2DC_Configure_VideoPipe` — `| CSI2DC_VPCFGR_RMS_1`.** Required by the RGB888-packed capture path: byte-stream packs 4 BGR pixels across 12 bytes per CSI-2 RMS spec (Table 49.27). Without it, capture writes wrong-format bytes and the display shows garbage. *(Earlier journal note claimed this was now MCC's default — incorrect; this regen confirmed MCC still emits without RMS_1.)*

  4. **`drv_image_sensor.h` enum shim.** MCC's regen *deletes* this file when the image-sensor component is disabled. Restore the 30-line shim at `default/src/config/default/vision/drivers/image_sensor/drv_image_sensor.h` defining `DRV_IMAGE_SENSOR_RAW_BAYER…JPEG` and `DRV_IMAGE_SENSOR_8_BIT…40_BIT` enums (numeric values must match the original — `drv_isc.c` derives `bits_per_pixel = 4 - inputBits`). Without it, `drv_isc.c` and `configuration.h` won't compile.

  5. **`initialization.c` `DRV_USB_VBUSPowerEnable` — per-pin VBUS calls.** MCC emits a single `VBUS_AH_*_Set/Clear()` call expecting one pin named `VBUS_AH`, but our config has VBUS on two pins (PC27 + PC31, one per USB port). The pin-macro generator produces `VBUS_AH_PC27_PowerEnable_*` and `VBUS_AH_PC31_PowerEnable_*` separately and no unified wrapper, so the MCC-emitted code fails to compile. Replace lines 162-163 (Set branch) and 169-170 (Clear branch) with explicit calls to both per-pin macros — `VBUS_AH_PC27_PowerEnable_Set(); VBUS_AH_PC31_PowerEnable_Set();` and the matching Clear pair. The MCC comment in the function ("name it to 'VBUS_AH'") acknowledges the single-pin assumption.

  6. **`initialization.c` `SYS_Initialize` — peripheral init order.** The USB-host MCC regen (2026-05-20) reordered SYS_Initialize so that `TC0_CH0_TimerInitialize`, `FLEXCOM6_TWI_Initialize`, and `XLCDC_Initialize` run *before* `MMU_Initialize` and `AIC_INT_Initialize`. Symptom on hardware: TC358743 probe wedges on its first I²C write — `OSAL_SEM_Pend(transferDone, WAIT_FOREVER)` never returns because the FLEXCOM6 ISR never fires. (Display + capture totally dead; FBL heartbeat keeps printing because it doesn't depend on a peripheral interrupt.) Fix: move the `MMU_Initialize → AIC_INT_Initialize → WDT-disable` block back to *before* the TC0/FLEXCOM6/XLCDC inits, matching the pre-USB-regen order. After this revert, video came back immediately. Resolved on 2026-05-20.

  7. **`usb_host_cdc.c` — accept `bInterfaceProtocol == 0` in CDC ACM Communications-interface match.** Two checks (single-interface path ~line 677, IAD path ~line 798) currently require `bInterfaceProtocol == USB_CDC_PROTOCOL_AT_V250` (`0x01`). EDBG-style USB-to-UART bridges (the on-board PIC/AVR debugger CDC, common across Microchip dev boards — including the fretboard board this project uses) declare the comm interface with `bInterfaceProtocol = USB_CDC_PROTOCOL_NO_CLASS_SPECIFIC` (`0x00`). Without this patch the host CDC driver accepts the IAD via the TPL match (TPL ignores subclass/protocol) but rejects the comm interface inside `F_USB_HOST_CDC_InterfaceAssign`, the interrupt pipe never opens, the CDC instance flips to `STATE_ERROR`, and the app-level attach handler never fires. Patch: extend each check to `(... == AT_V250 || ... == NO_CLASS_SPECIFIC)`. Two one-line OR additions. Re-apply after every USB-host MCC regen.

  8. **`FreeRTOSConfig.h` — `portCONFIGURE_TIMER_FOR_RUN_TIME_STATS` / `portGET_RUN_TIME_COUNTER_VALUE` macros + 64-bit counter type.** With `configGENERATE_RUN_TIME_STATS = 1` (set via `FREERTOS_GENERATE_RUN_TIME_STATS` MCC symbol), FreeRTOS expects the application to provide a free-running counter. MCC's FreeRTOS Harmony component does not expose a knob for an app-specific counter source. Append at the end of `FreeRTOSConfig.h` (just before `#endif`):
     ```c
     extern uint64_t SYS_TIME_Counter64Get(void);
     #define configRUN_TIME_COUNTER_TYPE             uint64_t
     #define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
     #define portGET_RUN_TIME_COUNTER_VALUE()        SYS_TIME_Counter64Get()
     ```
     `SYS_TIME` is already initialised by Harmony before the scheduler starts, so the configure macro is a no-op. `SYS_TIME_Counter64Get` returns the full 64-bit TC0 CH0 counter; the matching `configRUN_TIME_COUNTER_TYPE = uint64_t` overrides FreeRTOS's `uint32_t` default so cumulative `ulRunTimeCounter` values don't wrap (uint32_t at 266 MHz wraps after ~16 s of accumulated CPU time across all tasks, which made percentages garbage almost immediately). Bare `extern` (rather than including a Harmony header) keeps `FreeRTOSConfig.h` consumable by low-level kernel sources that don't pull in `definitions.h`.

  Previously listed but now resolved or moot:
  - ~~`plib_xlcdc.c` LVDSPLL multiplier~~ — MCC now emits the chosen `MUL/FRACR/DIVPMC` for our 50 Hz target once the XLCDC driver MCC config was set correctly. Manual override no longer needed.
  - ~~`plib_lvdsc.c` `LVDSC_CFGR.DEN_POL`~~ — Latest Harmony gfx library intentionally omits the DEN_POL field. File reverted to MCC default; not load-bearing.

  Recovery plan: re-apply all eight (small, self-contained diffs). Long-term options are (a) file MCC bugs, (b) shim into our own files, (c) live with periodic re-application.

- **`log_csi_status` was removed** (Phase 5 restructure — tc358743 no longer auto-enables stream). If we ever want to re-query TC358743 CSI_STATUS/CSI_ERR bits, re-add the helper. The register addresses and masks are still defined in the file.

- **ISC_Capture framebuffer sized for max 1920×1080 × 2 (~16 MB)** — wastes DDR at our current 720×480 use, but trivial at 256 MB total DDR and gives headroom for different sources. Reconsider if DDR becomes tight later.

- **Retry semantics on source loss.** `app_coordinate_capture` uses a `capture_attempted` flag to prevent retry spam. On unlock it resets, so the next lock triggers a fresh Configure+Start. Untested for fast lock/unlock cycles — if the Wii toggles power, watch for state-machine glitches.

- **Frame counter wraps at UINT32_MAX.** `g_frame_count` is 32-bit; at 60 fps it wraps after ~2.3 years continuous run. Not an immediate issue.

- **`TP_STRUM_DELAY_MS` likely needs to vary by difficulty.** 220 ms tuned well on Expert (notes are dense, the delay lines up against fast-moving notes near the strike line). Easy/Medium/Hard place notes higher up the highway with longer travel time, so the same 220 ms may strike too early. Open: per-difficulty preset, runtime-tunable from the manual-control surface, or auto-tuned from observed note-velocity. Defer until the game-state controller (spec §4.8) lands and difficulty is known to marvin — until then, expert-tuned 220 ms is the working default.

---

## Session log

### 2026-05-21 — FreeRTOS resource inventory + priority/analytics review

Stood up the work to enable FreeRTOS analytics (`vTaskListTasks`, `vTaskGetRunTimeStatistics`, `uxTaskGetStackHighWaterMark`) and audit task priority assignments across marvin. Three findings worth logging — one outright bug, one priority inversion, one structural problem with the priority budget.

**Resource inventory (16 tasks, 8 sync primitives).**

Hand-written (under `default/src/`, all `xTaskCreateStatic` per the static-allocation rule):

| Task | Prio | Stack (words) | File:Line | Cadence | Notes |
|---|---:|---:|---|---|---|
| `VideoTask` | 1 | 1024 | [video.c:323](../default/src/video/video.c#L323) | 20 ms poll | Capture-pipeline state machine; arms ISC on TC358743 lock; rebinds HEO per frame in ISR |
| `CvMarvinV1` | 2 | 1024 | [cv_marvin_v1.c:357](../default/src/detector/cv_marvin_v1.c#L357) | event (frame queue, portMAX_DELAY) | 5×5 patch sample + threshold + bus publish |
| `Timing` | 2 | 768 | [timing_pipeline.c:358](../default/src/actuator/timing_pipeline.c#L358) | event (bus queue, 5 ms timeout) | Chord window + strum scheduler |
| `FretLink` | 2 | 768 | [fretboard_link.c:248](../default/src/actuator/fretboard_link.c#L248) | event (cmd queue, 50 ms timeout) | USB-host CDC writer + 50 ms heartbeat republish |
| `PerfDrain` | 2 | 512 | [perf_log.c:176](../default/src/perf_log/perf_log.c#L176) | event (state queue, 20 ms timeout) | Frames + writes to USB-device CDC sink |
| `DetectorDrain` | 1 | 512 | [detector.c:69](../default/src/detector/detector.c#L69) | event (bus queue, portMAX_DELAY) | **M1 stub consumer; should have been deleted when M3 wired Timing** |

(`ManualControl` has no task — it's a synchronous API that calls `FretboardLink_Send` directly.)

MCC-generated (under `default/src/config/default/`, all `xTaskCreate` dynamic, all priority 1, all 1024-word stacks, all 10 ms `vTaskDelay` polling loops):

| Task | File:Line |
|---|---|
| `XLCDC_Tasks` | [tasks.c:178](../default/src/config/default/tasks.c#L178) |
| `DRV_MAXTOUCH_Tasks` | [tasks.c:187](../default/src/config/default/tasks.c#L187) |
| `USB_DEVICE_TASKS` | [tasks.c:199](../default/src/config/default/tasks.c#L199) |
| `USB_HOST_TASKS` | [tasks.c:208](../default/src/config/default/tasks.c#L208) |
| `DRV_USB_UDPHS_TASKS` | [tasks.c:217](../default/src/config/default/tasks.c#L217) |
| `LEGATO_Tasks` | [tasks.c:226](../default/src/config/default/tasks.c#L226) |
| `DRV_USB_HOST_TASKS` | [tasks.c:235](../default/src/config/default/tasks.c#L235) — wraps EHCI + OHCI |
| `SYS_INPUT_Tasks` | [tasks.c:244](../default/src/config/default/tasks.c#L244) |
| `APP_Tasks` | [tasks.c:257](../default/src/config/default/tasks.c#L257) — self-deletes after one tick (see [app.c:198-205](../default/src/app.c#L198-L205)) |

Sync primitives (all hand-written, all static):

| Primitive | Type | Depth × item | Producers → Consumers |
|---|---|---|---|
| `s_bus_queue` ([detector.c:61](../default/src/detector/detector.c#L61)) | Queue | 8 × `detector_state_t` | CvMarvinV1 → **Timing + DetectorDrain (bug)** |
| `frames` ([cv_marvin_v1.c:309](../default/src/detector/cv_marvin_v1.c#L309)) | Queue | 1 × `Video_FrameInfo` | Video ISR → CvMarvinV1 |
| `s_cmd_queue` ([fretboard_link.c:230](../default/src/actuator/fretboard_link.c#L230)) | Queue | 1 × `uint8_t` (overwrite) | Timing + ManualControl → FretLink |
| `s_state_q` ([perf_log.c:161](../default/src/perf_log/perf_log.c#L161)) | Queue | 128 × 40 B union slot | All perf producers → PerfDrain |
| `s_patch_q` ([perf_log.c:167](../default/src/perf_log/perf_log.c#L167)) | Queue | 8 × `perf_rec_patch_t` | CvMarvinV1 (future) → PerfDrain |
| `s_write_done` ([fretboard_link.c:236](../default/src/actuator/fretboard_link.c#L236)) | BinarySemaphore | — | Host CDC ISR → FretLink |
| `s_ctrl_done` ([fretboard_link.c:239](../default/src/actuator/fretboard_link.c#L239)) | BinarySemaphore | — | Host CDC ISR → FretLink |
| `s_write_done` ([perf_log_sink_cdc.c:50](../default/src/perf_log/perf_log_sink_cdc.c#L50)) | BinarySemaphore | — | Device CDC ISR → PerfDrain |
| `s_mutex` ([log.c:20](../default/src/log.c#L20)) | Mutex | — | log_vprintf() — **`portMAX_DELAY` ⇒ ISR-illegal**, see 2026-05-21 freeze entry |

No event groups, no stream/message buffers, no task notifications anywhere in the codebase today.

**Finding 1 (bug, fix before next test): `DetectorDrain` is double-consuming the bus.** Both `DetectorDrain` (priority 1, [detector.c:30-57](../default/src/detector/detector.c#L30-L57)) and `Timing` (priority 2, [timing_pipeline.c:330,338](../default/src/actuator/timing_pipeline.c#L330)) call `xQueueReceive` on `s_bus_queue`. FreeRTOS queues are single-consumer-per-record by design, so each task sees roughly half the records — the half DetectorDrain gets is silently dropped. Plays nicely with what we observe (gameplay still works, but timing decisions are derived from every other detector publish at best). The 2026-05-20 plan-of-record explicitly said `DetectorDrain` would be deleted when M3 wired the real consumer; that step was missed. Fix is one-line: delete the `xTaskCreateStatic(drain_task, …)` call in `Detector_Initialize` and the supporting `drain_task` function. Throughput logging it provided is moot now that the perf-log path exists.

**Finding 2 (priority inversion): `VideoTask` runs below its consumers.** `VideoTask` is the source of frames `CvMarvinV1` blocks on, but it's at priority 1 while `CvMarvinV1`/`Timing`/`FretLink`/`PerfDrain` all sit at 2. A late-frame condition (TC358743 lock churn, ISC retry) needs `VideoTask` to push the capture-pipeline state machine forward, but any priority-2 work currently runnable will preempt it. The detector then blocks on an empty frame queue while the video task can't run. Empirically not biting because `CvMarvinV1` only runs on frame arrival (event-driven, blocks immediately after consuming), but it's a latent class of stall whenever a priority-2 task ends up briefly busy. `VideoTask` belongs at priority 2 (or above) — the producer of the real-time pipeline shouldn't be in the same priority band as MCC's polling tasks.

**Finding 3 (structural): the priority budget is too narrow.** `configMAX_PRIORITIES = 5` (priorities 0..4). Idle is 0. With time-slicing on (`configUSE_TIME_SLICING = 1`, `configIDLE_SHOULD_YIELD = 1`) and round-robin between equal-priority Ready tasks, current bunching is:

- Priority 4: (unused)
- Priority 3: (unused)
- Priority 2: CvMarvinV1, Timing, FretLink, PerfDrain (4 tasks — real-time path, all event-driven, mostly mutually exclusive)
- Priority 1: 9 MCC polling tasks + VideoTask + DetectorDrain (11 tasks)
- Priority 0: idle

With 11 tasks ready at priority 1 and the tick rate at 1 kHz, each priority-1 task gets ≈ 1 ms of every ≈ 11 ms — tolerable but means each MCC stack does its 10 ms `vTaskDelay` poll on roughly the cadence it asks for plus or minus a tick of jitter. The bigger problem is that we have *zero* headroom for further structure: there's no place to put a faster-than-detector watchdog, a strict-priority audio path, or anything that should clearly outrank `Timing` without sharing. If we ever hit a priority-inversion class problem the kernel can't help via priority inheritance because there's nowhere to invert *to*. Preferred fix is to bump `configMAX_PRIORITIES` to 8 — costs 3 × `sizeof(List_t)` ≈ 60 bytes of BSS per extra band on this port, trivial — and reorganize as:

- 4: real-time strum-critical (Timing, FretLink)
- 3: real-time vision (VideoTask, CvMarvinV1)
- 2: PerfDrain + MCC USB host/device + DRV_USB_UDPHS (anything where stalling means a missed enumeration window)
- 1: MCC display/touch/Legato/SYS_INPUT (UI tasks; visible jitter only, not failure)
- 0: idle

The MCC tasks are dynamic-created but their priority arg comes from a single MCC config knob per task; we can override via the existing `user.cmake` patch list rather than editing `tasks.c` directly. Worth doing alongside enabling analytics — once HWM + run-time-stats are live we'll have the data to confirm the split is correct.

**Analytics enable plan.** Three flags + one macro pair in `FreeRTOSConfig.h`:

```c
#define configGENERATE_RUN_TIME_STATS         1
#define configUSE_TRACE_FACILITY              1
#define configUSE_STATS_FORMATTING_FUNCTIONS  1
#define INCLUDE_uxTaskGetStackHighWaterMark   1
```

`configGENERATE_RUN_TIME_STATS` requires a counter source via two macros:

```c
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  /* SYS_TIME init runs in SYS_Initialize already, no-op */
#define portGET_RUN_TIME_COUNTER_VALUE()          ((uint32_t)SYS_TIME_CounterGet())
```

Same TC0-CH0 source as the perf-log timestamps, so the units are consistent across both surfaces. Cost: `configUSE_TRACE_FACILITY` adds two pointers + a `UBaseType_t` per TCB; on 16 tasks that's a couple hundred bytes. Run-time stats arithmetic on every context switch is one 32-bit subtract + add. Acceptable.

Once enabled, dump on an SBC-style trigger — initially a 10-second-cadence `PERF_REC_TASK_HIGHWATER` from the perf-log drain task is the cheapest path (record format already exists in `perf_log_records.h`), and host-side decoder work makes it visible in the same Gantt view. Stretch: a console UART command to dump `vTaskListTasks` + `vTaskGetRunTimeStatistics` on demand, decoupled from the perf-log path so it works without USB.

Carry-forward "FreeRTOS analytics not yet enabled" note in §Open questions can come out once this lands.

**What landed (findings 1–3 resolved).**

Finding 1 fixed in its own commit: `DetectorDrain` task and its supporting storage deleted from `detector.c`; bus queue create stays. Smoke-tested on hardware before continuing.

Findings 2+3 landed together as a single re-tiering pass after deciding it was cleaner to size the priority budget once with the full layout in mind than to bump VideoTask now and re-shuffle later. `configMAX_PRIORITIES` 5 → 8 (room for 0..7; 60 B BSS for the extra ready-list bands; CLZ-based selection stays constant-time up to 32). Per-task priorities ended up:

| Band | Tasks | Role |
|---:|---|---|
| 7 | (reserved) | Future emergency-stop / hard watchdog |
| 6 | (reserved) | Future soft watchdog / fault recovery |
| 5 | `Timing`, `FretLink`, `USB_HOST_TASKS`, `DRV_USB_HOST_TASKS` | Strum-critical output (and the wire it travels) |
| 4 | `VideoTask`, `CvMarvinV1` | Vision real-time |
| 3 | `PerfDrain`, `USB_DEVICE_TASKS`, `DRV_USB_UDPHS_TASKS` | Diagnostic + perf-log wire |
| 2 | `LEGATO_Tasks`, `DRV_MAXTOUCH_Tasks`, `XLCDC_Tasks`, `SYS_INPUT_Tasks` | UI |
| 1 | (reserved) | Background housekeeping |
| 0 | idle, `APP_Tasks` (self-deletes) | — |

The MCC priorities are set in the per-component yml files (`gfx_legato.yml`, `usb_host.yml`, `drv_usbhs_v1.yml`, `drv_usb_udphs.yml`, `usb_device.yml`, `gfx_maxtouch_controller.yml`, `le_gfx_driver_xlcdc.yml`, `sys_input.yml`) plus `FreeRTOS.yml` for `FREERTOS_MAX_PRIORITIES = 8`, so MCC regen will produce the same `tasks.c` priority arguments — **no MCC re-apply patch needed for this layout**.

Hand-written tasks already had `*_TASK_PRIORITY` constants at file top; updates are one-line each in [video.c](../default/src/video/video.c), [cv_marvin_v1.c](../default/src/detector/cv_marvin_v1.c), [timing_pipeline.c](../default/src/actuator/timing_pipeline.c), [fretboard_link.c](../default/src/actuator/fretboard_link.c), and [perf_log.c](../default/src/perf_log/perf_log.c). Normalized `perf_log.c`'s priority constant from `(tskIDLE_PRIORITY + N)` to a bare integer literal to match the rest of the marvin code; `tskIDLE_PRIORITY` is `0` so this is a stylistic change only.

Hardware behavior post-change: gameplay path unaffected (Easy/Expert smoke-tested). One observation: framebuffer paint is visibly slower to come up during boot, which is expected — Legato is now in band 2, below USB (3/5), VideoTask + CvMarvinV1 (4), and Timing + FretLink (5), so the early-boot window where USB is enumerating, capture is locking, and the detector is spinning up gives Legato less CPU. Steady-state UI responsiveness is unchanged because higher-priority tasks all spend most time blocked.

**Analytics enabled + UART diag dump live.** Flipped `configGENERATE_RUN_TIME_STATS`, `configUSE_TRACE_FACILITY`, `configUSE_STATS_FORMATTING_FUNCTIONS`, and `INCLUDE_uxTaskGetStackHighWaterMark` via the FreeRTOS Harmony component (per-component yml updated). Wired the run-time counter to `SYS_TIME_Counter64Get` with `configRUN_TIME_COUNTER_TYPE = uint64_t` directly in [`FreeRTOSConfig.h`](../default/src/config/default/FreeRTOSConfig.h) — added as patch #8 on the MCC re-apply list. The `uint64_t` choice was forced by hardware: SYS_TIME runs at ~266 MHz, so a `uint32_t` cumulative counter wraps after ~16 s of accumulated CPU time across all tasks and percentages immediately go to nonsense — caught the wrap on the first multi-minute test.
- New [`diag/diag.{h,c}`](../default/src/diag/) module: priority-1 task at 10 s cadence dumps `uxTaskGetSystemState` results line-by-line over the existing `LOG_INFO` channel — task name, state, priority, stack high-water, accumulated CPU (ms), and percentage. Uses a static `TaskStatus_t[24]` array; **does not** call `vTaskListTasks` / `vTaskGetRunTimeStatistics` because both pvPortMalloc internally on every call, and our `heap_1` linker doesn't free — the convenience wrappers freeze the system after ~2 dumps when the 40 KB heap is exhausted (also a static-allocation-rule violation, would have been a footgun even with heap_4).
- Steady-state baseline measured during a multi-minute Easy-mode gameplay session: **97.9% idle**. CvMarvinV1 0.72% (≈ 120 µs/frame at 60 Hz for 5 patches × 5×5 BGR sample + threshold), FretLink 0.34%, Timing 0.10%, LEGATO 0.04%, VideoTask 0.02% (zero-copy DMA confirmed — VideoTask just rotates buffers and notifies subscribers, no per-frame memcpy). Stack high-waters all stable; `PerfDrain` is the tightest at 173 free of 512 (66% used) and worth bumping when producers wire up.
- Still pending: 10 s `PERF_REC_TASK_HIGHWATER` cadence on the perf-log channel (record type already in [perf_log_records.h](../default/src/perf_log/perf_log_records.h)) for off-device replay alongside the rest of the perf-log stream. The on-device UART dump is enough for live tuning today; the perf-log version becomes the durable record once the host decoder lands.

### 2026-05-21 — Perf-log producer-side module landed

Built the perf-log subsystem end-to-end on the producer side, including the real USB-device CDC ACM sink. Producers are not yet wired (module is dark beyond a SESSION + 1 Hz DROP heartbeat), but the wire path is live: a `PERF_REC_SESSION` is re-emitted on every host DTR-rising edge, so reconnecting a terminal mid-session always sees the schema record.

**What landed under [`firmware/marvin/default/src/perf_log/`](../default/src/perf_log/):**
- `perf_log_records.h` — wire format, schema version 1. 16 B common header (`magic` 0x4D56 'MV', `type`, `flags`, `frame_epoch`, `ts_counter`). Record types: SESSION, STAMP, DETECTOR, TIMING, PATCH, DROP, TASK_HIGHWATER. Stage IDs cover ISC_IRQ, VIDEO_PUBLISH, CV_START, CV_END, TP_TICK, FBL_SEND, CDC_WRITE_COMPLETE.
- `perf_log.{h,c}` — façade with separate task / `*FromISR` entry points (mirrors `xQueueSend` / `xQueueSendFromISR`). Two queues sized for the 30× small/large record-size ratio: state queue (slot = 40 B largest small record, depth 128) and patch queue (slot = `sizeof(perf_rec_patch_t)` = 814 B, depth 8). Drop-on-full, never block. Per-queue + sink drop counters RMW under `taskENTER_CRITICAL` / `taskENTER_CRITICAL_FROM_ISR` (the XC32 ARM926 port doesn't link libatomic; GCC's `__atomic_add_fetch` falls through to a libcall and won't link). Drain task at `tskIDLE_PRIORITY + 2` (above idle, below detector/timing) with 2 KB stack; emits one `PERF_REC_DROP` per second.
- `perf_log_sink.h` + `perf_log_sink_cdc.c` — sink interface and USB-device CDC ACM implementation. CRC-16/CCITT-FALSE over `LEN || PAYLOAD`; framing is `0x55 0x4D 0x52 0x56 || u16 LEN || PAYLOAD || u16 CRC`. MCC now ships USB-device + USB-device-CDC class config (UDPHS); the sink owns the application side: opens `USB_DEVICE_INDEX_0`, registers the device-layer event handler (Attach on POWER_DETECTED, register CDC handler on CONFIGURED), and exposes a blocking `WriteFramed`. CDC class events handle GET/SET line coding, control-line state (DTR latched into `s_cls`), and signal `WRITE_COMPLETE` via `xSemaphoreGiveFromISR` on a binary semaphore. Single producer (the drain task); writes are serialized one at a time on a single cache-aligned 832 B staging buffer (header + max patch payload + CRC). DTR-gated: if the host hasn't asserted DTR, frames are dropped into `s_drop_sink`. Write timeout (100 ms) drops + accumulates and the next state-machine pass observes DECONFIGURED if the host went away.
+ Cold-boot-with-cable required an explicit `Detach → 100 ms → Attach` edge after handler registration; relying on the driver's natural VBUS-edge `POWER_DETECTED` event to fire after-the-fact didn't enumerate (suspected: host gave up retrying after observing transient pull-up state during early boot, or the UDPHS driver's `vbusLevel` tracking races with handler-registration timing). The forced edge gives the host an unambiguous device-arrival regardless.
- μs timestamps via `SYS_TIME_CounterGet()` (TC0 CH0 raw counter) — host divides by `timer_freq_hz` from the SESSION record. Explicitly *not* the `xTaskGetTickCount() * (1000000 / configTICK_RATE_HZ)` pattern at [cv_marvin_v1.c:156](../default/src/detector/cv_marvin_v1.c#L156); that's ms-resolution masquerading as μs and is useless for sub-frame attribution.

**Wired into [`app.c`](../default/src/app.c):** `PerfLog_Initialize()` after `Video_Initialize()` (queues exist before any producer can post); `PerfLog_Start()` at the end of `APP_Initialize` (creates the drain task; Harmony brings the scheduler up after `APP_Initialize` returns, so this is the standard "create static tasks before `vTaskStartScheduler`" pattern). `cmake/marvin/default/user.cmake` updated.

**Producers not yet wired** — module is dark today. Next steps land them in this order: (1) ISR producers (`video.c` ISC_IRQ + VIDEO_PUBLISH; `fretboard_link.c` CDC_WRITE_COMPLETE — *no* `LOG_INFO` from this ISR per the 2026-05-20/21 freeze pattern); (2) task producers in `cv_marvin_v1`, `timing_pipeline`, `fretboard_link.FretboardLink_Send`. Replaces the commented-out 2 Hz dump at [`cv_marvin_v1.c:336-351`](../default/src/detector/cv_marvin_v1.c#L336-L351). `PERF_REC_PATCH` emission slots in last (Tier 2 — reuses already-sampled 5×5 patches, no new I/O).

### 2026-05-21 — Perf-logging bandwidth/capacity scoping

Crunched numbers ahead of designing a frame-by-frame perf log (video + detector + actuator state) so we know which resolutions and which transports are actually in scope. Detail below; takeaway is that the spec's already-chosen "sparse keyframes + dense state" point (~1 MB/s) is the only real sweet spot, and the "full-rate raw video" wish is dead on arrival across every transport SAM9X75 has.

**Baseline — 720×480 RGB888 (BGR888-packed in our pipeline) @ 60 Hz**

- Per frame: 720 × 480 × 3 = **1.04 MB**.
- Per second: **62.2 MB/s ≈ 498 Mb/s**.
- All state payload (detector_state_t × 2 detectors + actuator + timing snapshot + per-frame metadata header) is ~150 B/frame ≈ **9 KB/s** — rounding error against pixels. This is a video-bandwidth problem, not a state-volume problem.

**Scaled-down variants (per-second bandwidth)**

| Variant | B/s |
|---|---|
| Full 720×480 RGB888 @ 60 Hz | 62.2 MB/s |
| Same @ 30 Hz | 31.1 MB/s |
| 720×480 RGB565 @ 60 Hz | 41.5 MB/s |
| 720×480 grayscale @ 60 Hz | 20.7 MB/s |
| 360×240 RGB888 @ 60 Hz | 15.6 MB/s |
| 360×240 RGB888 @ 30 Hz | 7.78 MB/s |
| 180×120 RGB888 @ 30 Hz | 1.94 MB/s |
| Strike-line strip 720×80 @ 60 Hz | 10.4 MB/s |
| 5 ROI patches 32×32 RGB888 @ 60 Hz | 922 KB/s |
| State-only (no pixels) @ 60 Hz | 9.1 KB/s |

**Capacity over a session length**

| Stream | 1 min | 5 min (song) | 10 min |
|---|---|---|---|
| Full @ 60 Hz | 3.73 GB | 18.7 GB | 37.3 GB |
| 360×240 @ 30 Hz | 467 MB | 2.33 GB | 4.67 GB |
| 1 keyframe/s + state (≈ spec §4.6) | 62 MB | 311 MB | 622 MB |
| ROI patches 60 Hz | 55 MB | 277 MB | 553 MB |
| State only | 547 KB | 2.7 MB | 5.5 MB |

**Transport ceilings on this hardware** (sustained, realistic)

| Channel | Sustained | Peripheral status |
|---|---|---|
| UART 115.2 kBd (console) | ~11 KB/s | in use (FLEXCOM4) |
| UART 921.6 kBd | ~92 KB/s | EDBG bridge cap |
| UART 3 Mbd (FLEXCOM max) | ~300 KB/s | unlikely through bridges |
| USB CDC ACM (HS device, class-stack overhead) | ~5–15 MB/s | device peripheral unused |
| USB Bulk (HS device, custom class) | ~30–40 MB/s | needs custom host-side reader |
| SDMMC Class-10 via FAT32 | ~5–10 MB/s | already planned for §4.6 keyframes |
| SDMMC UHS-I | ~25–50 MB/s | bus capable, FS overhead bites |
| GMAC Ethernet UDP | ~30–60 MB/s | GMAC unused |
| GMAC Ethernet TCP | ~10–20 MB/s | GMAC unused |

**What fits where**

- **State-only (9 KB/s)** fits everywhere including the console UART — no new transport needed if the goal is just "trace detector + timing + actuator decisions per frame."
- **ROI patches 60 Hz (~1 MB/s)** preserves everything cv_marvin_v1 actually samples; streams comfortably over USB CDC, fits an entire song on SD with room to spare.
- **1 keyframe/s + dense state (~1 MB/s)** is what spec §4.6 already commits to, fits any modern SD card, and survives over USB CDC or Ethernet.
- **Full 60 Hz (62 MB/s)** doesn't fit any transport sustained, *and* a 5-minute song at that rate is 18.7 GB — DOA.

**Implication for what to build next.** The scoping confirms the spec's existing reference-data architecture is the right place to start; perf-logging is a *consumer* of the same infrastructure, not a separate path. Next step is to figure out which questions the perf log actually has to answer (latency attribution? frame-rate stability? detector confidence over time? actuator jitter?) and pick the minimum stream that answers them. State-only might already be enough for the latency/jitter questions; ROI patches are the cheapest way to get "did the detector see what I think it saw" replayability.

### 2026-05-21 — cv_marvin_v1 threshold tuning + timing-pipeline tweaks

First end-to-end gameplay test on hardware. Detector was firing roughly random presses at first; resolved by reading actual signal values rather than guessing.

**Visibility added:**
- State-reactive overlay in `cv_marvin_v1.c` — position rings always; filled center dot when `s_pressed[i]` (white in the hold ring) or `s_edge_active[i]` (fret-color in the edge ring). Lets the user watch chatter live: stable note → steady dot for hold duration; strum window → flash on the edge dot.
- Periodic ~2 Hz log dump (`frame_count % 30 == 0`) showing per-fret `hold_dist / edge_dist / P E` flags.

**Diagnosis (Easy mode, no real B/O presses):**
- Idle hold floor sits at 22–48 across all five frets, **not** the limited-range 16 floor `display_path.md` would suggest. Playfield-glow at the sample sites is well above the limited-range black point.
- Real-press hold spikes to 67–186, leaving a clean gap above the noise ceiling.
- Hold threshold 50 with release-frac 0.6 (release=30) was *below* the idle floor for Y and B → those frets latched permanently pressed and never released.
- Edge threshold 50 was at the peak of real-edge signal (40–53), almost never tripping. timing_pipeline only uses `pressed` today, so dead edges aren't the chatter source — but lowering edge threshold lets `s_edge_active` overlay flicker meaningfully again.

**Settings landed (in `cv_marvin_v1.c`):**
- `CV_HOLD_THRESH`: 50 → **100** (after iterating 65 → 80 → 100; 100 cleanly above noise across all frets, real presses still spike to 150–186).
- `CV_HOLD_RELEASE_FRAC`: 0.60 → **0.78** (release ≈ 78 — above the noise ceiling, comfortably below real-press floor).
- `CV_EDGE_THRESH`: 50 → **25** (gives the overlay an honest edge indicator without affecting pipeline behavior).
- Sample coords nudged 1 px on G/R/Y/B/O edges + B hold for slightly cleaner alignment.

**Performance:** Expert-mode play is now quite good — clean note recognition, fret hold-through across consecutive same-fret notes confirmed by trace through `process_releases` (release suppressed when the next chord-commit's `note_q` entry already needs the bit, within `TP_STRUM_DELAY_MS − TP_CHORD_WINDOW_MS = 190 ms` of detector-release-to-detector-press).

**Timing-pipeline tweaks** (Greg's hand-edits): `TP_STRUM_PULSE_MS` 50→25, `TP_CHORD_WINDOW_MS` 20→30, `TP_FIFO_CAP` 16→32.

Open question added above: `TP_STRUM_DELAY_MS = 220 ms` is Expert-tuned; Easy/Medium/Hard may need different values because note travel time differs.

### 2026-05-21 — Manual fretboard-control producer + timing-pipeline gate

Stood up the actuator-side half of a basic on-device manual-control surface — primarily for game-menu navigation (start a song, advance menus, hit pause) where single-finger input is the natural shape. UI half waits on a Microchip Graphics Composer regen.

What landed:

- `actuator/manual_control.{h,c}` — second producer of `FretboardLink_Send`, alongside `timing_pipeline`. Internal state: `s_fret_mask` (bits 0..4) and `s_strum_mask` (bits 5..6). Public API: `Initialize / SetEnabled / IsEnabled / SetFret / SetStrum`. Both fret and strum are momentary — held while the UI button is down, cleared on release. `recompute_and_send` only calls `FretboardLink_Send` while enabled.
- `TimingPipeline_SetEnabled(bool)` — output gate on `publish_mask`. Pipeline still advances internal state when gated, so the next `advance()` (≤ `TP_TICK_MS` = 5 ms) after re-enable republishes the right mask without a stale frame.
- `ManualControl_SetEnabled` ordering: enter manual mode = gate timing pipeline first, zero local state, send `0`, set enabled flag (so a stale pipeline frame can't race a manual zero on the wire); exit = clear enabled flag, zero state, send `0`, ungate pipeline.
- App init wires `ManualControl_Initialize()` after `TimingPipeline_Initialize()`. No Legato dependency at this stage.
- `cmake/marvin/default/user.cmake` adds `manual_control.c` and `ui/manual_input.c`.

UI half landed same session after Greg ran the Composer regen. Composer added 8 buttons to `Screen0` — `Screen0_Button_Manual_{Green,Red,Yellow,Blue,Orange,StrumDown,StrumUp,Enable}` — plus 15 extern `event_Screen0_Button_Manual_*` handler declarations the application must define. The binding shape came out simpler than the plan called for: `screenShow_Screen0` itself calls `setPressedEventCallback` / `setReleasedEventCallback` with those extern symbols, so the linker resolves them and **no `ManualInput_Bind()` walk is needed**. `ui/manual_input.c` is just the 15 function bodies forwarding to `ManualControl_SetFret / Strum / SetEnabled`. The originally planned `APP_Tasks` poll-loop for null widget pointers is moot — `APP_Tasks` keeps the existing `vTaskDelete(NULL)`.

Strum buttons are momentary, not one-shot: holding the strum button keeps the fretboard's strum line asserted, which Guitar Hero / Rock Band menus interpret as auto-repeat scrolling — the primary use case for this surface. Gameplay-path strums still pulse correctly because they go through `timing_pipeline`, which has its own `TP_STRUM_PULSE_MS` one-shot. The Enable button is `setToggleable(LE_TRUE)` with only `OnReleased`; both the widget and `ManualControl` default to off, so a simple `SetEnabled(!IsEnabled())` flip stays in sync without querying the widget's toggle state.

Plan agent flagged two real blockers worth recording: (1) Legato's input pipeline routes each touch to one focus widget — multi-finger momentary chords aren't possible without a custom dispatcher, so the model is single-finger only; (2) the lazy widget-pointer init means any code that walks the `Screen0_*` pointers has to run post-`screenShow_Screen0` — moot here because Composer does the wiring itself, but still true for any future shim that needs to read or modify widget state.

Decision-log entries above cover the producer-as-peer pattern, the Composer-driven UI choice, and the single-finger input model.

**Bring-up over the wire.** Marvin enumerates the EDBG CDC and `FretboardLink_Send` round-trips cleanly through the queue, but the fretboard MCU's SERCOM1 wasn't receiving anything. Two real fixes:

1. **EDBG bridge needs DTR raised.** The bridge holds its UART TX idle until the host asserts DTR. Added `USB_HOST_CDC_ACM_ControlLineStateSet` with `dtr=1, carrier=1` to `open_cdc()` after the existing `LineCodingSet(115200)`. Baud matters here — fretboard runs a real UART at 115200, not USB-CDC-ignored.
2. **Control-pipe completion handlers run from ISR context.** First instrumented attempt logged `LOG_INFO` directly from the new `SET_LINE_CODING_COMPLETE` / `SET_CONTROL_LINE_STATE_COMPLETE` event cases; system froze mid-print. Microchip's CDC stack dispatches *all* class events from ISR (the existing `WRITE_COMPLETE` case already uses `*FromISR` primitives — that was the precedent). `log_vprintf` takes a mutex with `portMAX_DELAY`, illegal from ISR. Stripped both the diagnostic logging and the latch infrastructure once the link was confirmed functional. Captured for the journal because it's a recurring shape: any new event-handler case in this stack must stay strictly ISR-safe.

### 2026-05-20 — USB CDC host attach: EDBG composite enumerates, CDC class driver now binds

Picked up where the prior session left off — VBUS not asserting, no device detected. Closed both halves.

**VBUS.** MCC's `DRV_USB_VBUSPowerEnable` callback is wired in the host driver but the host stack never invokes it on this build. Asserted the two VBUS GPIOs (`VBUS_AH_PC27_PowerEnable_Set` / `VBUS_AH_PC31_PowerEnable_Set`) directly from `APP_Initialize` ahead of consumer init and `USB_HOST_BusEnable`. EDBG immediately enumerates.

**Class-driver binding.** Enumeration succeeded but the CDC attach handler never fired. Added per-interface descriptor logging inside `F_USB_HOST_UpdateDeviceTask` to see what was actually being matched. Output revealed VID `0x03EB` PID `0x2175` — an EDBG composite with five interfaces: HID/CMSIS-DAP, CDC ACM IAD pair (comm + data), vendor, MSC. The IAD passed TPL match and got assigned to the CDC driver, but the CDC instance still flipped to `STATE_ERROR` without our app-level handler ever seeing the attach.

Root cause was inside Microchip's CDC class driver: `F_USB_HOST_CDC_InterfaceAssign` (both single-interface and IAD paths) requires the comm interface to declare `bInterfaceProtocol == USB_CDC_PROTOCOL_AT_V250` (`0x01`). EDBG declares `0x00` (none) — functionally identical CDC ACM in practice; Linux/macOS accept either. The TPL/IAD pre-check passes (TPL wildcards subclass/protocol), the CDC driver gets the interface group, then immediately rejects it on the AT-V.250 check. The interrupt pipe never opens, the instance errors out, and the app-level listener stays silent.

Fix: relaxed both checks to `(AT_V250 || NO_CLASS_SPECIFIC)`. After the patch the FBL log shows `CDC device attached, handle opened` and `c=Y` heartbeat as expected. Logged as patch #7 in the MCC re-apply list.

Also tested a hypothesis from the prior session that `USB_HOST_Initialize` had to run *after* the EHCI/OHCI driver inits. With the diagnostic logging in place, MCC's emitted order (`USB_HOST_Initialize` → `Legato_Initialize` → driver inits) enumerates and binds the CDC IAD just as well — the host layer evidently looks up HCD interfaces lazily at `USB_HOST_BusEnable` time. Backed out the hand-edit; one fewer re-apply patch to maintain. Decision-log entry above.

Diagnostic instrumentation stripped at the end of the session: the per-interface descriptor dump and enumeration state-change logger in `usb_host.c`, and the periodic OHCI/EHCI register heartbeat in `fretboard_link.c`. What remains on disk: the two `usb_host_cdc.c` protocol-relaxation lines (patch #7), the app-level VBUS asserts, and a minimal `USB_HOST_EVENT` printf so future host-stack drama still surfaces in the log.

Next session: revisit timing-pipeline behavior end-to-end now that the actuator wire is live.

### 2026-05-20 — Hardware bring-up after USB CDC host regen: TC358743 wedge resolved

First on-target test after the M2 actuator path landed. Symptoms: video stream not starting, USB ports unpowered, FBL heartbeat firing with `connected=N`. Boot stopped after `TC358743: probe starting`.

Root cause: the USB MCC regen reordered SYS_Initialize so `MMU_Initialize` and `AIC_INT_Initialize` ran *after* `TC0_CH0_TimerInitialize`, `FLEXCOM6_TWI_Initialize`, and `XLCDC_Initialize`. With AIC initialized late, the FLEXCOM6 interrupt vector wasn't owned by the AIC when the first I²C transfer ran; `OSAL_SEM_Pend(transferDone, WAIT_FOREVER)` blocked forever waiting for an ISR that never fired. FBL stayed alive because it's pure FreeRTOS — no peripheral interrupt dependency.

Fix: reverted SYS_Initialize order so MMU/AIC/WDT-disable run before TC0/FLEXCOM6/XLCDC. Display + capture came back on first boot. Logged as patch #6 in the MCC re-apply list above.

USB enumeration is still not working (VBUS not asserting, no device detected). That's a separate investigation — next session.

### 2026-05-20 — Static-allocation conversion, cv_marvin_v1 port, USB CDC host bring-up

Long session covering three independent threads:

1. **Static-allocation conversion.** Adopted project rule: marvin code never uses dynamic FreeRTOS APIs. MCC regenerated with `configSUPPORT_STATIC_ALLOCATION=1` and `vApplicationGetIdleTaskMemory`. Converted `log.c`, `video.c`, `detector.c`, `cv_marvin_v1.c` to `xTaskCreateStatic` / `xQueueCreateStatic` / `xSemaphoreCreateMutexStatic` with per-module `Static{Task,Queue,Semaphore}_t` plus storage arrays at file scope. FreeRTOS heap stays at heap_1 only because MCC tasks still need it. Also fixed two latent video.c issues surfaced by the build: forward-declared `s_capture_armed/s_display_bound` above the ISR that reads them, added missing `#include <stdbool.h>` to `video.h`.

2. **cv_marvin_v1 reference port.** Ported `tools/fret-tuner/detect_video.py` to C — 5×5 patch BGR sampling, brightness-based hold detect with hysteresis (`HOLD_THRESH=50`, release at 60%), color-filtered edge detect with rising-edge `press_count`, per-fret BGR target/reject filter table. Coords scaled from Python's 1920×1080 anchor to 720×480 (Wii 480p60) at port time — same Elgato direct-pixel topology, just different resolution. Bus record stays canonical (option A): only `pressed / confidence / raw_value` cross the bus; `press_count` and raw color distances stay private to the module. Recording-format choice (custom packed binary vs nanopb vs CBOR) deferred to M5. ISC framebuffer pool moved from `.region_cache_aligned` to `.region_nocache` so CPU readers see DMA writes coherently; cap dropped 1080p→720p to fit the 16 MB nocache region.

3. **USB CDC host MCC regen.** Decided on USB CDC (marvin host, fretboard device) for the actuator/detector link instead of FLEXCOM UART — single cable, auto-enumeration, host-PC-debuggable. MCC regenerated with EHCI+OHCI host drivers and CDC host class. VBUS power-enable pins on PC27+PC31 (one per port). Hand-edit needed in `initialization.c` `DRV_USB_VBUSPowerEnable` because MCC assumes a single VBUS pin named `VBUS_AH` and emits one Set/Clear call; with two pins we need both per-pin macros explicitly. Logged as patch #5 in the MCC re-apply list.

Spec update: added §4.8 game-state awareness & control as a marvin subsystem (peer of detection but separate bus, video-frame consumer not a detector). Defers recognizer algorithm + arbitration boundary as Q10/Q11.

Next session: USB device CDC on fretboard side, then wire-protocol design (framing, command set, baud, ack semantics) before either host or device code is written.

### 2026-05-20 — Video fan-out + per-frame buffer routing

Two-commit sequence opening up video capture for parallel CV consumers:

1. **Per-frame buffer routing + multi-subscriber.** Found that `ISC_Capture_GetBufferAddress()` always returned the base, so HEO and `cv_marvin_v1` were both reading slot 0 only — slot 1 written and silently lost. Bumped the ISC frame callback signature to deliver the just-completed buffer address (computed from `iscObj->frameIndex`, which the driver pre-increments). `video.c` now re-points HEO in the ISR every frame and fans out the frame info to a static array of 4 subscribers.
2. **Ring depth from 2 to 4.** Cheap DDR cost; gives slow consumers ~50 ms read window before lapping.

Deferred: per-task XDMAC sub-region copies for slow / sub-region consumers (e.g., menu/score readers). Will add when the first such consumer arrives.

### 2026-05-20 — M1 scaffolding landed

Stood up the detector subsystem skeleton. New module at `default/src/detector/`:

- `detector.h` — `detector_state_t` (canonical layout from spec §4.2.3), `fret_t` enum, `detector_id_t` enum, `Detector_Initialize`, `Detector_BusQueue`.
- `detector.c` — owns `xDetectorStateQueue` (depth 8, holds `detector_state_t`) and a temporary `DetectorDrain` task that consumes the bus and logs records/sec at INFO. Drain task is M1-only scaffolding; the timing pipeline (M3) will replace it as sole consumer.
- `cv_marvin_v1.{h,c}` — owns its FreeRTOS task. Creates a depth-1 frame queue, calls `Video_SubscribeFrames`, blocks on `xQueueReceive`, publishes one `detector_state_t` per frame with `frame_epoch = frame.frame_count`, `timestamp_us = xTaskGetTickCount() × CV_US_PER_TICK`, all-zero `fret[]`.

Wired from `app.c`'s `APP_Initialize` after `Video_Initialize`. Added both `.c` files to `cmake/marvin/default/user.cmake`. No code change to video or capture; `Video_FrameInfo.bytes_per_pixel` already reports 3 (RGB888 packed).

Spec touch-up: §4.2.2 "BGRX32 frames" → "RGB888-packed (3 B/pixel) frames". Surfaced the broader spec drift on capture format as a carried-forward open question — deserves a doc-only sweep before M5 (recording schema depends on it).

Added per-detector enable/disable + single-active selector to the bus API (`Detector_Enable`/`Disable`/`IsEnabled` + `Detector_SetActive`/`GetActive`). Defaults all-disabled; app explicitly enables `DETECTOR_CV_MARVIN_V1` and selects it as active. cv_marvin_v1 still drains its frame queue when disabled but skips publishing. Future video CV tasks (menu, score) go in as video-frame consumers, not detectors — needs a multi-subscriber upgrade to `Video_SubscribeFrames` before the second one lands.

Next session: pick Q8 (initial CV algorithm — leaning pixel-mean threshold per ROI as the simplest end-to-end bring-up) and start populating the `fret[]` field in cv_marvin_v1. ROI geometry calibration also needs a home — spec §4.7 (system config) is the planned destination.

### 2026-05-20 — System spec drafted

Stepped back from per-feature work and scoped marvin's role across the whole guitar-playing-robot system. Output is [`spec.md`](spec.md), a durable system-level description (versus this journal as running diary). Drafted in document order: §1 purpose & scope, §2 system context (three-tier diagram, ownership table, latency budget), §3 hardware platform inventory, §4 all seven subsystems (4.2 CV detection + detector-state bus and 4.6 reference-data recording fully drafted; 4.1 video capture summarized referencing existing docs; 4.3–4.5 + 4.7 sketched). §5–9 stubbed.

Settled architectural decisions (all in the decision log above):
- Marvin replaces fret-tuner PC as runtime brain.
- Marvin is the reference detector; recording is first-class.
- Multiple coexisting detectors on one canonical bus.
- Timing pipeline centralized on marvin with a fretboard-takeover fallback mode.
- Reference data = SD card + detector-state + sparse keyframes (60-frame stride, raw BGRX32).
- `frame_epoch` is the master sync token.
- Operating modes are independent toggles, not a state machine.

Open questions moved into spec §9: UI framework choice (Q5), initial CV algorithm (Q8), config persistence location (Q9), Ethernet ref-data live stream (Q2; deferred post-M8).

Next session resumes at **M1 — Reference detector v0** (one CV detector running on captured frames, publishing `detector_state_t` records). No code changed today.

_(Earlier session entries — 2026-05-15 back through 2026-05-01 — are in [`journal-archive.md`](journal-archive.md).)_
