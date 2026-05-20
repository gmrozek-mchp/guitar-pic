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
| 2026-05-20 | System-level spec lives at [`spec.md`](spec.md); read it first on any non-trivial marvin task | We're at the point where deferring system architecture would mean building feature-by-feature without a coherent target. The journal is the running diary; the spec is the durable description of what marvin is. |
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
| 2026-05-20 | Game-state awareness & high-level game control is a marvin subsystem (spec §4.8). Distinct from the gameplay note-detection path: a video-frame consumer (not a detector) emits typed game-state events on `xGameStateQueue`, and a controller layer translates verbs ("start single-player song X") into fret/strum sequences sent over the same fretboard link | Want marvin to drive whole-session play, not just notes once gameplay starts. Game-state observation belongs on marvin because it's CV on the same captured frames. Separating its bus from `xDetectorStateQueue` keeps `detector_state_t` narrow per the 2026-05-20 multi-detector decision. Per-game catalog (menu graph + song list) is metadata kept on marvin. Default arbitration with timing pipeline: mutually exclusive — controller runs only outside `gameplay` state. Phasing: M9 observer-only, M10 control. Open: recognizer algorithm (Q10), arbitration boundary (Q11). |

---

## Open questions

_(Questions we haven't answered yet. Move to decision log with rationale once resolved. System-level open questions are tracked in [`spec.md`](spec.md) §9 — anything tagged with `Q1..Q9` there.)_

- ~~CSI-TX minimum bitrate at 480p60~~ — **resolved 2026-05-01**: we ended up dropping to 297 Mbps/lane (below the kernel's tabulated 594 Mbps) with no problems. 297 pairs with host HSFREQRANGE band 0x14 and the kernel's 594 Mbps D-PHY timings happen to still be safe at this rate.
- ~~`TXACT` absent in CSI_STATUS after Phase 4 enable~~ — **deferred 2026-05-01**: TXACT has been observed at both 0 and 1 at various times after enable. Likely momentary (active-packet indicator, not stream-on flag). CSI_ERR=0 and HLT=0 consistently. Phase 5 frames-in-memory confirmed the chain works regardless.
- ~~Source mode variability~~ — **explained 2026-05-01**: 1440x240p observation was Wii post-reset default. Put Wii in 480p mode; we stay on 720x480p@60.

**Carried into future sessions:**

- **Spec ↔ implementation drift on capture format.** Spec still uses "BGRX32" in ~10 places (§2 status table, §2.1 diagram, §3.1 hardware list, §4.1 video task, §4.2 detector input, §4.6.5 keyframe filename + size math, §4.7 sample config). The actual capture has been BGR888 packed (3 B/pixel) since the 2026-05-02 switch (`isc_capture.c:65-74`). Fixed §4.2.2 inline 2026-05-20 because M1 reads against it; the rest needs a sweep — including recalc of keyframe size (720×480 × 3 = 1.04 MB, not 1.38 MB) and on-disk recording layout. Defer to a doc-only pass before M5.

- **MCC-file modifications maintenance risk.** A from-scratch MCC regen on 2026-05-15 confirmed **four** local modifications get clobbered. The 2026-05-20 USB-host regen added a fifth. Re-apply after every regen:

  1. **`plib_csi.c` — `CSI_Analog_Init` refactor.** MCC emits per-lane HS-RX init inline with three bugs: Lane 1 in the 2-lane branch is missing the bit-rate write, Lane 2's lane-select code is `0x24` instead of `0x64`, and the 4-lane else-if branch skips Lane 1 entirely. Replace with the `csi_phy_hs_rx_init(lane_code, bit_rate)` helper and call it for lanes 0/1/2/3 from a flat `if (nlanes >= …)` chain.

  2. **`plib_xlcdc.c` `XLCDC_EnableClocks` — `PMC_PLL_ACR`.** Set to datasheet-optimal `0x12023010` for the LVDSPLL with our 24 MHz reference (in the `fIN ∈ [20 MHz, 32 MHz]` band). Specifically: `LOOP_FILTER=0x12, LOCK_THR=0x2, UTMIBG=1, UTMIVR=1, CONTROL=0x10`. MCC's default emits a different (less-optimal) analog config (`LOOP_FILTER=0x1B, LOCK_THR=0x4, no UTMI bits`). Doesn't affect functional behavior in our use (50 Hz refresh works either way), but is the manufacturer-recommended jitter/lock optimum.

  3. **`plib_csi2dc.c` `CSI2DC_Configure_VideoPipe` — `| CSI2DC_VPCFGR_RMS_1`.** Required by the RGB888-packed capture path: byte-stream packs 4 BGR pixels across 12 bytes per CSI-2 RMS spec (Table 49.27). Without it, capture writes wrong-format bytes and the display shows garbage. *(Earlier journal note claimed this was now MCC's default — incorrect; this regen confirmed MCC still emits without RMS_1.)*

  4. **`drv_image_sensor.h` enum shim.** MCC's regen *deletes* this file when the image-sensor component is disabled. Restore the 30-line shim at `default/src/config/default/vision/drivers/image_sensor/drv_image_sensor.h` defining `DRV_IMAGE_SENSOR_RAW_BAYER…JPEG` and `DRV_IMAGE_SENSOR_8_BIT…40_BIT` enums (numeric values must match the original — `drv_isc.c` derives `bits_per_pixel = 4 - inputBits`). Without it, `drv_isc.c` and `configuration.h` won't compile.

  5. **`initialization.c` `DRV_USB_VBUSPowerEnable` — per-pin VBUS calls.** MCC emits a single `VBUS_AH_*_Set/Clear()` call expecting one pin named `VBUS_AH`, but our config has VBUS on two pins (PC27 + PC31, one per USB port). The pin-macro generator produces `VBUS_AH_PC27_PowerEnable_*` and `VBUS_AH_PC31_PowerEnable_*` separately and no unified wrapper, so the MCC-emitted code fails to compile. Replace lines 162-163 (Set branch) and 169-170 (Clear branch) with explicit calls to both per-pin macros — `VBUS_AH_PC27_PowerEnable_Set(); VBUS_AH_PC31_PowerEnable_Set();` and the matching Clear pair. The MCC comment in the function ("name it to 'VBUS_AH'") acknowledges the single-pin assumption.

  Previously listed but now resolved or moot:
  - ~~`plib_xlcdc.c` LVDSPLL multiplier~~ — MCC now emits the chosen `MUL/FRACR/DIVPMC` for our 50 Hz target once the XLCDC driver MCC config was set correctly. Manual override no longer needed.
  - ~~`plib_lvdsc.c` `LVDSC_CFGR.DEN_POL`~~ — Latest Harmony gfx library intentionally omits the DEN_POL field. File reverted to MCC default; not load-bearing.

  Recovery plan: re-apply all four (small, self-contained diffs). Long-term options are (a) file MCC bugs, (b) shim into our own files, (c) live with periodic re-application.

- **`log_csi_status` was removed** (Phase 5 restructure — tc358743 no longer auto-enables stream). If we ever want to re-query TC358743 CSI_STATUS/CSI_ERR bits, re-add the helper. The register addresses and masks are still defined in the file.

- **ISC_Capture framebuffer sized for max 1920×1080 × 2 (~16 MB)** — wastes DDR at our current 720×480 use, but trivial at 256 MB total DDR and gives headroom for different sources. Reconsider if DDR becomes tight later.

- **Retry semantics on source loss.** `app_coordinate_capture` uses a `capture_attempted` flag to prevent retry spam. On unlock it resets, so the next lock triggers a fresh Configure+Start. Untested for fast lock/unlock cycles — if the Wii toggles power, watch for state-machine glitches.

- **Frame counter wraps at UINT32_MAX.** `g_frame_count` is 32-bit; at 60 fps it wraps after ~2.3 years continuous run. Not an immediate issue.

---

## Session log

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
