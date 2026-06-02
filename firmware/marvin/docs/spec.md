# marvin — System Specification

> **Read this first** for any non-trivial marvin task. The journal ([`journal.md`](journal.md)) is the running diary of decisions and work-in-progress; this spec is the durable description of *what marvin is*. When the two disagree, fix the spec.

**Status:** in-progress draft. Sections that are settled are marked ✅; sections that depend on open architectural questions are marked 🚧 and link to the relevant entries in §9.

---

## 1. Purpose & scope

### 1.1 What marvin is

marvin is the firmware running on a **SAM9X75 Curiosity** board. Its job, in the guitar-playing-robot system, is to:

1. Capture live HDMI video of a Guitar Hero / Rock Band game from a Nintendo Wii.
2. Run computer-vision note detection on the captured frames.
3. Decide *what* to play and *when* (chord grouping, fret-early timing, strum scheduling).
4. Send the corresponding fret/strum commands to the actuator controller (the **fretboard** MCU).
5. Provide an on-device operator UI (10.1″ LVDS panel + maxtouch capacitive touch) for live view, calibration, mode selection, logs.
6. Produce **reference data** — annotated frames + detector state — for off-board use (training low-fidelity detectors, side-by-side detector comparison, replay).
7. **Observe game state from the screen** — recognize current menu / song-select / gameplay / pause / score-summary contexts via CV, expose high-level control verbs ("start single-player playthrough of song X", "advance menu", "exit to main menu") that translate into fret/strum command sequences sent through the same fretboard link.

In short: marvin is the runtime brain *and* the reference-detector data source for the project.

### 1.2 What marvin is *not*

- **Not the actuator.** Physical fret/strum GPIO drive lives on the fretboard MCU. marvin sends bitmask commands; fretboard converts them to button presses.
- **Not a sensor / detection-only device.** marvin owns timing and command emission. It is not a passive vision peripheral that some other host orchestrates.
- **Not a router for a PC.** The previous PC-based architecture (Elgato + OpenCV + USB to a Pi) is retired. marvin replaces the PC in the runtime path.
- **Not a generic camera platform.** The video source is a fixed HDMI signal from a game console. Suggestions like "just use a USB webcam" or "output to HDMI directly" miss the point.

### 1.3 North-star one-paragraph summary

A SAM9X75 captures Wii HDMI video at 720×480 / 1280×720 @ 60 Hz over a TC358743 → MIPI CSI-2 → ISC path into DDR (BGR888 packed, 3 B/pixel). It runs reference-quality CV note detection on those frames, fuses with ADC-based detection ingested from a fretboard MCU over USB CDC, schedules chord/strum commands through a low-latency timing pipeline, sends commands back to the fretboard, drives an operator UI on a 10.1″ LVDS panel, and exports a compressed reference-data stream so that lighter-weight detectors (today: fretboard's phototransistors; future: an Edge AI MCU) can be trained against marvin's ground truth.

### 1.4 What's done, what's next

| Subsystem | Status |
|---|---|
| HDMI capture (TC358743 → CSI-2 → ISC → DDR, BGR888 packed) | ✅ working at 480p60 and 720p60. See [`capture_pipeline.md`](capture_pipeline.md). |
| Display path (DDR → XLCDC HEO → LVDSC → panel) | ✅ working, pillarboxed/letterboxed, per-frame pointer swap. See [`display_path.md`](display_path.md). |
| FreeRTOS scheduler, video task, OSAL I²C | ✅ landed. |
| Lightweight log shim, FreeRTOS analytics, task priorities | ✅ done. |
| CV detection pipeline | 🚧 M1 complete: `cv_marvin_v1` running on captured frames, `detector_state_t` bus active. No actuation path through detector yet. §4.2. |
| Fretboard link (USB CDC host over EDBG) | ✅ working; commands flowing, gameplay tested. §4.3. |
| Timing pipeline (chord FIFO, strum) | ✅ working; end-to-end gameplay tested on Expert and Easy. §4.4. |
| Operator UI (Legato) | 🚧 Manual-control surface (8 buttons, Legato Composer) done; full calibration/log UI not started. §4.5, open Q5. |
| Reference-data recording & export (SD) | 🚧 SD recording not started. Perf-log USB CDC export (separate dev-tooling path) complete at 2.77 MB/s. §4.6, open Q1/Q2. |
| System services (config, time, watchdog) | 🚧 Partial: logging, FreeRTOS analytics, static task priorities done; config persistence and watchdog not started. §4.7. |
| Game-state awareness & high-level game control | 🚧 not started. §4.8, open Q10/Q11. |

---

## 2. System context

### 2.1 Three-tier system

```
┌──────────────────────────┐    HDMI 480p60 / 720p60
│  Nintendo Wii            │────────────────────────────────────────┐
│  (Guitar Hero / RB)      │                                        │
└──────────────────────────┘                                        │
                                                                    │
                       ┌──────────────────────┐                     │
                       │ ElectronWarp         │                     │
                       │ (component → HDMI)   │                     │
                       └──────────┬───────────┘                     │
                                  │ HDMI                            │
                                  ▼                                 │
                       ┌──────────────────────┐                     │
                       │ Waveshare TC358743   │                     │
                       │ HDMI → MIPI CSI-2    │                     │
                       └──────────┬───────────┘                     │
                                  │ CSI-2 (2 lanes, 972 Mbps/lane)  │
                                  ▼                                 │
   ┌────────────────────────────────────────────────────────────┐   │
   │                    marvin (SAM9X75)                        │   │
   │                                                            │   │
   │   ISC ──► DDR (BGR888) ──► CV detect ──┐                   │   │
   │                              │         │                   │   │
   │                              ▼         ▼                   │   │
   │                          XLCDC ──► LVDS panel              │   │
   │                                        ▲                   │   │
   │              ┌─────────────────────────┴───────┐           │   │
   │              │ detector-state bus              │           │   │
   │              │  ├─ CV detector(s)              │           │   │
   │              │  └─ fretboard-ADC detector ◄──┐ │           │   │
   │              ▼                               │ │           │   │
   │         timing pipeline ──► fret/strum cmds  │ │           │   │
   │              │                               │ │           │   │
   │              ▼                               │ │           │   │
   │      reference-data export                   │ │           │   │
   │       (Ethernet / SD / USB CDC — §4.6)       │ │           │   │
   └──────────────┬────────────────────────────┬──┴─┘           │   │
                  │ USB CDC host               │ USB CDC host   │   │
                  │ (frets/strum cmds)         │ (ADC stream)   │   │
                  ▼                            │                │   │
   ┌────────────────────────────────────┐      │                │   │
   │      fretboard (PIC32CM6408)       │──────┘                │   │
   │   ADC scan ──► UART stream         │                       │   │
   │   cmd RX ──► open-drain GPIO       │                       │   │
   │   (frets PA01–07, strums PA00/03)  │                       │   │
   └─────────────────┬──────────────────┘                       │   │
                     │ open-drain GPIO                          │   │
                     ▼                                          │   │
   ┌────────────────────────────────────┐                       │   │
   │ Wii Guitar Hero controller         │                       │   │
   │ (5 frets + strum bar)              │◄──────────────────────┘   │
   └────────────────┬───────────────────┘                           │
                    │ controller cable                              │
                    ▼                                               │
            (back to Wii) ◄────────────────────────────────────────┘

                        ┌─────────────────────────────────┐
                        │ optional dev PC (fret-tuner)    │
                        │ ─ calibration / tuning UI       │
                        │ ─ replay / detector comparison  │
                        │ ─ training-data ingest          │
                        └─────────────────────────────────┘
                          (off-band; not in runtime path)
```

### 2.2 Who owns what

| Concern | Owner | Notes |
|---|---|---|
| HDMI capture into memory | marvin | TC358743 + CSI-2 + ISC; 720×480 / 1280×720 @ 60 Hz, BGR888 packed (3 B/pixel). |
| Reference-quality CV note detection | marvin | Ground truth; multiple algorithms may coexist on the bus. |
| ADC-based note detection | fretboard → marvin | fretboard scans 5 phototransistors @ 240 Hz, streams raw values; marvin runs the detector logic. |
| Detector-state fusion / arbitration | marvin | One canonical detector-state bus; the active source feeds the timing pipeline. |
| Chord-window FIFO + strum scheduling | marvin | Open Q4: stays on marvin (centralized) vs migrates to fretboard. Default: marvin. |
| Fret/strum GPIO output | fretboard | Open-drain pin drive only; receives a bitmask over UART. |
| Operator UI (live view, calibration, modes, logs) | marvin | 10.1″ LVDS + maxtouch. Open Q5: Legato vs custom. |
| Reference-data recording & export | marvin | Compressed/derived format; transport TBD (open Q1/Q2). |
| Game-state observation (menu / song-select / gameplay / pause / score) | marvin | CV on captured frames; emits typed game-state events on its own bus. §4.8. |
| Game catalog + menu metadata (song list, menu graph, recognized icons) | marvin | Compile-time tables and/or SD-card JSON. Drives the recognizer and the navigator. §4.8. |
| High-level game control (e.g., "start single-player song X") | marvin | Translates verbs into fret/strum sequences sent over the same fretboard link as gameplay commands. §4.8. |
| Calibration / tuning UI authoring | dev PC (fret-tuner) | Optional; not required at runtime. Open Q7: long-term fate. |
| Training data ingest / replay analysis | dev PC | Off-band consumer of marvin's reference-data export. |

### 2.3 Data flow at a glance

1. **Frame ready** — ISC writes a BGR888 packed frame to DDR; the video task notifies subscribers.
2. **CV detect** — one or more CV detectors read the frame and emit detector-state events.
3. **ADC ingest** — a UART task receives raw ADC samples from the fretboard and runs an ADC detector that emits detector-state events on the same bus.
4. **Arbitrate** — the timing pipeline picks the active detector (or fuses), maintains the chord-accumulation window, and pushes finished chords onto its FIFO.
5. **Schedule** — the FIFO emits fret-assert and strum-pulse commands at the correct ticks.
6. **Send** — commands are encoded and transmitted to the fretboard over UART.
7. **Display** — the captured frame is composited with overlays (sensor crosshairs, detector outputs, chord state, latency markers) and shown on the LVDS panel.
8. **Record** (optional) — when recording is enabled, a derived/compressed reference-data record (frame epoch + detector state + raw ADC + emitted commands) is buffered and exported via the chosen transport.

### 2.4 Latency budget (target, end-to-end)

| Stage | Budget | Notes |
|---|---|---|
| HDMI source → DDR (capture) | ≤ 16.7 ms | One frame @ 60 Hz; bounded by capture pipeline. |
| Detect on frame | ≤ 16.7 ms | One frame budget for CV; ADC path is separate and faster. |
| Timing pipeline → command emit | latency dominated by `STRUM_DELAY_MS` (≈ 200 ms) — intentional, not waste; compensates for sensor placement above strike line. |
| UART command → fretboard GPIO | ≤ 5 ms | Short fixed-format frame at ≥ 115 200 Bd. |
| Total user-visible latency | ≈ 200–250 ms | Tunable via `STRUM_DELAY_MS`, set per-game by calibration. |

The dominant fixed delay (`STRUM_DELAY_MS`) exists by design — the camera sees notes before they reach the strike line. Capture + detect + UART jitter is what we actually budget against.

---

## 3. Hardware platform

### 3.1 Boards and links

| Board | Role | Connection to marvin |
|---|---|---|
| **SAM9X75 Curiosity** | marvin host (CPU, DDR, peripherals, panel/touch ports) | — |
| **Waveshare HDMI → CSI-2 adapter** (TC358743) | HDMI → MIPI CSI-2 bridge | I²C (FLEXCOM6, PA24/PA25, 400 kHz, addr `0x0F`) for control; 2-lane CSI-2 RX for data; PC15 PWD, PC19 RESET (currently unused — software reset over I²C). |
| **Microchip 10.1″ 1280×800 LVDS panel + maxtouch** | Operator UI | LVDSC pair from XLCDC; I²C for maxtouch (existing Harmony driver). |
| **fretboard board** (PIC32CM6408PL10048) | Sensor + actuator MCU | USB CDC host (marvin is USB host to fretboard's on-board EDBG composite, VID=0x03EB PID=0x2175; CDC ACM bridged to SERCOM1 PB00/PB01 at 115 200 Bd). |
| **Wii guitar controller** | Physical input target | Open-drain GPIO on fretboard, not directly on marvin. |
| **(optional) dev PC** | Calibration / training-data ingest / replay viewer | SD card swap (primary) or USB CDC for live debug; no runtime dependency. |

Pin assignments live in `firmware/marvin/default/src/config/default/pin_configurations.csv`. The capture pipeline doc ([`capture_pipeline.md`](capture_pipeline.md)) is authoritative for CSI / ISC register state; the display path doc ([`display_path.md`](display_path.md)) is authoritative for XLCDC overlay wiring.

### 3.2 SAM9X75 resources used / unused

| Peripheral | Status | Notes |
|---|---|---|
| MIPI CSI-2 RX + ISC + CSI2DC | ✅ in use | 2 lanes, 972 Mbps/lane; BGR888 packed to DDR (CSI2DC RMS=1, 3 B/pixel). |
| XLCDC + LVDSC | ✅ in use | HEO layer, RGB\_888\_PACKED, per-frame pointer swap, pillarbox/letterbox. |
| FLEXCOM6 (I²C) | ✅ in use | TC358743 control. |
| FLEXCOM4 (UART, DBGU/console) | ✅ in use | `printf` retarget; lightweight log shim. |
| FreeRTOS (Harmony OSAL) | ✅ in use | Scheduler running; video task split out (commit `6b85d5e`). |
| TC0 (SYS_TIME) | ✅ in use | OSAL synchronous I²C requires it. |
| GMAC (Ethernet) | ⚪ unused | Reserved for future live-stream ref-data export (not MVP). |
| SDMMC | 🚧 to be enabled | MVP transport for reference-data recording (§4.6). |
| USB host (EHCI + OHCI) | ✅ in use | Fretboard link — marvin is USB host to fretboard EDBG CDC. |
| USB device (UDPHS) | ✅ in use | Perf-log CDC ACM sink; marvin presents as USB device to dev PC, streams perf records at up to 2.77 MB/s. |
| Maxtouch I²C | 🚧 to be enabled | Operator UI (§4.5). |
| Watchdog | 🚧 not configured | System services (§4.7). |
| Free FLEXCOMs | several available | Fretboard UART link (§4.3) needs one. |

---

## 4. Subsystems

### 4.1 Video capture & display ✅

Working end-to-end. See [`capture_pipeline.md`](capture_pipeline.md) and [`display_path.md`](display_path.md) for register-level detail. The video task (`video.c`, FreeRTOS) owns the capture pipeline and a frame queue that other tasks subscribe to. Display intent and capture intent are decoupled (commit `e28c497`); a software scaler can be inserted between capture and display when source ≠ panel native.

**What this subsystem owns:**
- TC358743 init, source detection, EDID, format negotiation.
- ISC DMA configuration and frame buffer rotation in DDR.
- XLCDC overlay window placement (pillarbox/letterbox) for the active source.
- Frame-ready notification to subscribers (CV detect, recording, UI overlay).

**What it doesn't own:**
- CV processing (§4.2).
- Recording / export (§4.6).
- UI widgets / chrome (§4.5) — only the pixel composition for the live-view layer.

### 4.2 CV detection pipeline + detector-state bus 🚧

#### 4.2.1 Role

This is what makes marvin the *reference detector*. It runs computer-vision note detection on captured frames and publishes a structured detector-state record per frame onto a shared bus. Other detectors (fretboard ADC; future Edge-AI unit) publish onto the same bus. The timing pipeline (§4.4) consumes the bus.

#### 4.2.2 Detector model

A **detector** is anything that turns observation data into a `detector_state_t` event. marvin will host at least:

- `cv_marvin_v1` — the reference vision detector. Reads RGB888-packed (3 B/pixel) frames from the video frame queue; emits one `detector_state_t` per frame.
- `adc_fretboard` — runs on top of raw ADC samples streamed from the fretboard MCU (§4.3); emits `detector_state_t` events at the fretboard's 240 Hz rate.

Future detectors may include alternate CV algorithms running in parallel for comparison, or off-board detectors (Edge-AI MCU) feeding state in over UART/Ethernet.

#### 4.2.3 detector_state record (canonical shape)

```
typedef struct {
    uint32_t frame_epoch;     // master sync token (§4.6.4)
    uint64_t timestamp_us;    // monotonic, marvin-local
    uint8_t  detector_id;     // 0=cv_marvin_v1, 1=adc_fretboard, ...
    uint8_t  reserved[3];
    struct {
        uint8_t  pressed;     // 0/1 hard call
        uint16_t confidence;  // 0..65535, detector-defined
        uint16_t raw_value;   // detector-defined: ADC sample, pixel mean, etc.
    } fret[5];                // FRET_GREEN..FRET_ORANGE
} detector_state_t;
```

Fields are fixed-width, naturally aligned, little-endian — this is also the on-disk record layout for the state log (§4.6.5). `frame_epoch` is the master clock for cross-detector and cross-recording sync.

#### 4.2.4 Bus mechanics

- A single FreeRTOS queue (`xDetectorStateQueue`) carries `detector_state_t` records, one entry per detector emit.
- The timing pipeline task (§4.4) is the sole consumer.
- The recording task (§4.6) snoops the queue via a side-channel: each producer publishes simultaneously to the timing-pipeline queue and to a recording stream buffer. (Single producer→multiple consumers is implemented by tee'ing inside each detector, not by a broker; keeps queue semantics blocking and simple.)
- Detectors run on their own FreeRTOS tasks (one per detector instance) so a slow detector cannot stall the frame pipeline.

#### 4.2.5 Reference-detector responsibilities

`cv_marvin_v1` is a *first-class* subsystem, not a debug aid. Its detector state is the ground-truth label for any reference-data recording (§4.6). Implications:

- Algorithm changes are versioned (`cv_marvin_v1`, `cv_marvin_v2`, …). The version is stamped into recordings so training data stays attributable.
- Calibration (sensor/strike-line ROIs in pixel space) is stored in the system config (§4.7) and reproducibly applied at startup.
- The detector must run within one frame-time (≤ 16.7 ms) on Cortex-A5 — concrete algorithm choice is a future decision, but the budget is set.

#### 4.2.6 Open questions surfaced here

- Initial CV algorithm — pixel-mean threshold per ROI, edge-trough, optical-flow-based, or learned. (Defer; bring up the simplest first to validate end-to-end.)
- ADC-detector logic location — port `fretboard/fret_button.c`'s detection (hysteresis edges) into marvin (`adc_fretboard` runs on raw samples), or have fretboard pre-process and stream edges. (Default: marvin runs the detector on raw samples; fretboard stays dumb.)

### 4.3 Fretboard link ✅

USB CDC host between marvin and the fretboard MCU. Marvin acts as USB host to the fretboard's on-board EDBG debugger (composite device exposing CDC ACM). EDBG bridges the CDC ACM interface to the fretboard's SERCOM1 UART at 500 000 Bd.

- **Marvin → fretboard:** command bitmask (which frets to assert + strum direction). 1-byte bitmask, no framing.
- **Fretboard → marvin:** raw ADC stream (5 channels × 16-bit) at 240 Hz, in a 12-byte start/end-bracketed frame. Marvin's `fretboard_link` issues a continuously-rearming `USB_HOST_CDC_Read` and a sibling `fretboard_rx_task` parses each valid frame and republishes it as a `PERF_REC_FRETBOARD_RAW` perf-log record (default-disabled, host enables via `PERF_CMD_SET_TYPE_MASK`). This is the on-board capture path until the SD-card recorder lands; it's also the substrate for the future `adc_fretboard` detector (§4.2).
- **Standalone fallback:** when marvin's timing pipeline is disabled (see §4.4 and §6), the fretboard runs its own existing chord FIFO (`fret_button.c`) and continues to stream ADC + emitted-command telemetry to marvin for capture/display.

### 4.4 Timing pipeline 🚧

Centralized on marvin by default. Owns:

- Chord-accumulation window (`CHORD_WINDOW_MS` ≈ 20–40 ms).
- Pending-chord FIFO with per-chord `press_at` / `strum_at` ticks.
- Per-fret release scheduling.
- Strum direction alternation and pulse generation.
- `STRUM_DELAY_MS` / `FRET_EARLY_MS` / `STRUM_PULSE_MS` constants — initial values port from `tools/fret-tuner/SPEC.md` and `firmware/fretboard/SPEC.md`.

**Disable / handoff mode:** an operator-mode toggle (§6) deactivates marvin's pipeline. fretboard then runs its own existing pipeline; marvin still ingests ADC + emitted-command telemetry for recording/display, but does not emit commands.

### 4.5 UI / operator surface 🚧

10.1″ 1280×800 LVDS panel + maxtouch capacitive touch. Functional surfaces required:

- Live game view (capture composited with detector overlays — sensor crosshairs, detection events, latency markers).
- Calibration / tuning (per-fret ROI placement, threshold values, timing constants).
- Mode + run controls (idle, calibrate, dry-run, play, replay, record).
- Logs / history / stats (recent commands, miss/hit counts, dropped frames).

**UI framework — open Q5.** Legato is already pulled in for capture init and could absorb the operator UI directly (heavy but in-tree). Alternative: a lightweight custom widget layer over GFX2D / direct framebuffer composition. Decision deferred until we attempt the first non-trivial screen (calibration overlay).

### 4.6 Reference-data recording & export 🚧

#### 4.6.1 Goals (in priority order)

1. **Edge-AI training data** — produce labelled frames + ground-truth detector state suitable for training low-fidelity detectors (fretboard's photo-detector logic; future Edge-AI MCU).
2. **Replay / sanity check** — let the user (offline) scrub through a recorded session to investigate misses.
3. **Multi-detector sync** — independent detectors (fretboard ADC, future Edge-AI) record their own data alongside marvin's; everyone sync's on a shared frame epoch.

Live off-device streaming for side-by-side comparison is *not* on the MVP critical path.

#### 4.6.2 Transport — SD card

Recordings are persisted to an SD card via SDMMC + a simple filesystem (FAT32 via Harmony's FILE_SYSTEM service). The card is removed and read on a workstation; no live network path is required for MVP.

Bandwidth ceiling: SDMMC sustained write ≥ 5 MB/s on Class-10 cards is reliably achievable. The recording format below is sized to land well below that.

#### 4.6.3 Format — detector-state + sparse keyframes

A recording is a **directory** on the SD card, containing:

```
recordings/
└── 2026-05-20T14-32-08/
    ├── manifest.json          // session metadata, calibration snapshot, detector versions
    ├── state.bin              // dense detector_state_t records, all detectors, all frames
    ├── commands.bin           // emitted fret/strum commands with frame_epoch
    ├── adc_raw.bin            // raw fretboard ADC stream with frame_epoch interleave
    └── keyframes/
        ├── 000000.bgr         // raw BGR888 keyframe (no encoder), filename = frame_epoch
        ├── 000060.bgr
        └── ...
```

- **`state.bin`** — append-only, fixed-size `detector_state_t` records (§4.2.3). At 60 Hz × 2 detectors × ~28 B = ~3.4 KB/s. Trivial.
- **`commands.bin`** — fret/strum bitmask + direction + emit timestamp + frame_epoch, ~16 B per emit. Sparse.
- **`adc_raw.bin`** — fretboard 12-byte frames at 240 Hz with `frame_epoch` annotation = ~2.9 KB/s. Modest. Until the SD recorder lands, the same data is available live as `PERF_REC_FRETBOARD_RAW` records (28 B framed) in any marvin-perf capture (default-disabled; enable with `set-mask`).
- **Keyframes** — raw BGR888 packed dump every N captured frames. At 720×480 × 3 B = 1.04 MB/keyframe. Default cadence: **1 keyframe per second** (60-frame stride) → 1.04 MB/s sustained. Configurable (every 30 / 60 / 120 / 600 frames). At 1 fps a 5-minute session is ~312 MB — comfortable on any modern SD card.
- No software JPEG / video encode. SAM9X75 has no hardware JPEG; CPU encode at 60 fps is infeasible. Keyframes stay raw; offline tools can transcode if desired.

Filename convention: directory name is ISO-8601 timestamp at session start (UTC); keyframe filenames are zero-padded `frame_epoch` so chronological order matches lexical order.

#### 4.6.4 Sync token — `frame_epoch`

A 32-bit counter incremented by the video task on every captured ISC frame (rolls over after ~830 days @ 60 Hz). Every record in a session — `state.bin`, `commands.bin`, `adc_raw.bin`, keyframe filenames, off-board detector data — is keyed by `frame_epoch`. This is the master clock for the entire system, including any later-added detectors that record independently and need to align their data with marvin's.

For records that don't fall on a frame boundary (ADC samples emitted between video frames), the most-recent `frame_epoch` is used and the sub-frame offset is captured by `timestamp_us`.

#### 4.6.5 manifest.json

Minimum fields:

```json
{
  "schema_version": 1,
  "session_started_utc": "2026-05-20T14:32:08Z",
  "marvin_firmware_git": "<hash>",
  "capture": { "width": 720, "height": 480, "fps": 60, "format": "BGR888" },
  "keyframe_stride_frames": 60,
  "detectors": [
    { "id": "cv_marvin_v1", "version": "1.0.0", "calibration": { ... } },
    { "id": "adc_fretboard", "version": "1.0.0", "thresholds": { ... } }
  ],
  "timing": { "chord_window_ms": 20, "strum_delay_ms": 220, "fret_early_ms": 50, "strum_pulse_ms": 50 },
  "ended_utc": null
}
```

Written at session start; `ended_utc` is patched on a clean stop.

#### 4.6.6 On-device buffering

A FreeRTOS task (`record_task`) owns SD writes. State / command / ADC streams are small and write through directly. Keyframes are larger — a single keyframe at 1.04 MB cannot block the video pipeline, so the record task receives a **frame buffer pointer** (the same DDR buffer the ISC writes into; refcounted by the video task) and writes from DDR while the next frame is being captured into another buffer. We need ≥ 3 capture frame buffers (one capturing, one displaying, one being recorded) when recording is active; today the video task already has multiple frames pre-allocated — exact count is verified during implementation.

If the SD write FIFO falls behind (rare; only a sustained-throughput problem), the recorder drops keyframes (logs a `RECORD_DROP` event into `state.bin`'s metadata channel) but **never** drops detector-state records.

#### 4.6.7 Off-board detectors recording independently

When a future Edge-AI detection unit is brought online, it can:

- Subscribe to marvin's `frame_epoch` over a chosen link (UART/Ethernet/etc.).
- Record its own observations (e.g. its own sensor data + its own detector output) in its own format, *keyed by marvin's `frame_epoch`*.
- Be aligned post-hoc against marvin's reference recording during training.

The spec does not constrain how off-board detectors store their data — only the sync contract (`frame_epoch` timeline) is canonical.

### 4.7 System services 🚧

Cross-cutting services not owned by any one subsystem:

- **Logging** ✅ severity-filtered printf shim (commit `a79042f`).
- **Time** ✅ TC0 / SYS_TIME for OSAL.
- **Config persistence** 🚧 calibration values, mode toggles, last-used recording stride. Storage TBD (a config file on SD vs internal flash).
- **Watchdog** 🚧 not yet enabled.
- **OTA** ⚪ out of scope for now.

### 4.8 Game-state awareness & control 🚧

Note on numbering: this subsystem is logically a peer of §4.2–§4.6 (a video-frame consumer that emits typed events plus a controller that issues actuator commands). It lives at §4.8 only to avoid renumbering existing sections referenced from the journal and milestones.

#### 4.8.1 Role

Two related capabilities, both grounded in CV on the captured video stream:

1. **Game-state observer.** Watch the screen; recognize what context the game is currently in — main menu, song-select, difficulty-select, in-game (gameplay), pause, score summary, etc. Surface that as typed events that the operator UI can render and the orchestrator below can act on.
2. **Game controller.** Expose high-level verbs ("start a single-player playthrough of song X on Hard", "go to the main menu", "select Practice mode"). Translate each verb into a sequence of fret/strum commands using the recognized current state and a known menu graph, monitor the screen for the expected state transitions, retry or back out on mismatch.

This is *not* the gameplay note-detection path (§4.2). cv_marvin_v1 plays notes during gameplay; the game-state module decides *what to play* at the session level (which song, which difficulty, which mode) and gets us there from any starting screen.

#### 4.8.2 Module shape

- Owns its own FreeRTOS task (`game_task`).
- Subscribes to the video frame queue via the same multi-subscriber API used by detectors (§4.2.4); it is a video-frame consumer, **not** a detector. Its events do not flow on `xDetectorStateQueue`.
- Emits typed events on a separate, narrow bus (`xGameStateQueue`) — current_state changes, ambiguity warnings, error states.
- Orchestrator state for high-level verbs (current goal, expected next state, retry counter) lives inside `game_task`.
- Issues control commands by writing fret/strum bitmasks into the same fretboard-link path used by gameplay. Whether this routes through the timing pipeline (§4.4) or bypasses it is open Q11.

#### 4.8.3 Game catalog & menu metadata

The recognizer and the navigator both depend on per-game data:

- **Recognizer templates.** Per-state visual signatures used by the CV recognizer — could be small reference images (template matching), color histograms, or text regions for OCR. Format chosen with the recognizer algorithm (open Q10).
- **Menu graph.** Nodes = recognized game states; edges = button sequences that move between them (e.g., "main_menu → song_select" = `[STRUM_DOWN, STRUM_DOWN, GREEN]`). Used by the navigator to plan a verb.
- **Song catalog.** Per-supported-game list of songs with the menu coordinates needed to select each one (which difficulty submenu, ordinal position in the list, etc.). Possibly augmented with metadata (BPM, length, expected difficulty score) for UI display and for reference-data labeling.

Storage: compile-time tables for the per-game graph + a JSON or similar on the SD card for songlists and any user-editable bits. Exact split deferred until first concrete game is added.

Scope today: target is **one game** — Guitar Hero (Wii) — to validate the design. Adding a second game is a metadata addition (new template set + new menu graph + new song catalog), not a structural change.

#### 4.8.4 Interaction with other subsystems

- **Detection (§4.2):** independent. cv_marvin_v1 runs only when the game-state observer reports `gameplay`. Outside gameplay, cv_marvin_v1 may be disabled to free CPU.
- **Timing pipeline (§4.4):** during gameplay the timing pipeline drives the actuators from detector-state. Outside gameplay it's idle, and the game-state controller drives actuators directly. Coordination and arbitration between the two is open Q11.
- **Operator UI (§4.5):** consumes game-state events to display the current screen the game is on; offers verbs as buttons / song-list pickers.
- **Recording (§4.6):** game-state transitions are useful session metadata. Either annotated into `state.bin` as a side channel or a sibling file (`game_state.bin`). To be decided when the recorder lands.
- **Fretboard link (§4.3):** shared command path. The fretboard MCU does not know whether a press is "gameplay note" or "menu navigation" — they're the same wire bits.

#### 4.8.5 Open questions surfaced here

See §9 for tracking entries. In summary:

- **Q10** — Recognizer algorithm: template matching vs simple OCR vs region/color heuristics vs a small CNN. Trade-off is robustness vs CPU cost vs metadata authoring effort.
- **Q11** — Command-path arbitration between game-state controller and timing pipeline. Default is "they don't run at the same time" (gameplay vs menu), but the boundary needs to be explicit.

---

## 5. External interfaces 🚧

To be expanded once §4.3 (fretboard wire protocol) is settled. Will document:

- Marvin ↔ fretboard UART framing, byte-for-byte.
- Recording on-disk binary layouts (the `_t` structs from §4.2.3 and §4.6 in canonical form, including endianness).
- USB-CDC dev console framing (if any), Ethernet ref-data stream framing (if/when added).

## 6. Operating modes 🚧

Default model: independent toggles (matches `tools/fret-tuner/SPEC.md`'s detect/actuate split). Confirmed direction; concrete toggle list:

| Toggle | Effect |
|---|---|
| `detect_enable` | CV + ADC detectors run; detector-state bus is active. |
| `marvin_timing_enable` | marvin's chord FIFO / strum scheduler runs. When off, fretboard runs its own pipeline (§4.4 fallback). |
| `actuate_enable` | Commands are actually sent to fretboard. When off, marvin computes commands but suppresses them (dry-run). |
| `record_enable` | Recording task writes to SD. |
| `game_observe_enable` | Game-state observer task runs; `xGameStateQueue` is active. |
| `game_control_enable` | Game-state controller may issue actuator commands for menu navigation / session setup. Suppressed during `gameplay` state when timing pipeline is driving. |

A handful of named composite modes (idle, calibrate, dry-run, play, replay, record) are presets over these toggles. Replay specifically requires loading a prior recording from SD and playing detector-state events back through the timing pipeline against the original keyframes — UI affordance for that comes later.

## 7. Build & deploy 🚧

To be filled. Topics: MCC-clobbered files (which paths are safe to edit by hand vs. regenerate), `user.cmake` boundaries, flashing workflow, sequence of MCC steps to reproduce the project.

## 8. Phasing & milestones 🚧

Proposed order; each is a buildable demo:

1. **M1 — Reference detector v0** (§4.2). One CV detector running on captured frames, publishing `detector_state_t` to the bus. No actuation. Proves the bus and the detection task structure.
2. **M2 — Fretboard link** (§4.3). UART up; ADC stream ingested; `adc_fretboard` detector publishing onto the bus alongside `cv_marvin_v1`.
3. **M3 — Timing pipeline + actuation** (§4.4). Chord FIFO + strum scheduling; commands sent over UART; fretboard actuates. End-to-end play possible.
4. **M4 — Recording to SD** (§4.6). SDMMC + FAT mount; record task writes state/commands/ADC/keyframes during a session.
5. **M5 — Operator UI v0** (§4.5). Live view with overlays; mode toggles. UI framework decided here.
6. **M6 — Calibration UI** (§4.5). Per-fret ROI placement + threshold tuning on the device.
7. **M7 — Replay** (§6). Load a recording from SD, replay through the timing pipeline.
8. **M8 — Standalone-fretboard fallback** (§4.4). Marvin-disabled-pipeline mode validated.
9. **M9 — Game-state observer v0** (§4.8). Recognize main_menu / song_select / gameplay / pause / score states; surface as `xGameStateQueue` events. No control yet.
10. **M10 — Game-state control v0** (§4.8). High-level verbs ("start single-player song X") drive menu navigation through the same fretboard link.

Live-stream Ethernet, Edge-AI integration, and config-on-flash are post-M8.

## 9. Open questions

| # | Question | Status |
|---|---|---|
| Q1 | Reference-data format details — keyframe stride and per-fret patch sizes if patches are added later. | Direction set (§4.6.3); exact stride defaults to 60 frames, revisit after first training-data consumer exists. |
| Q2 | Reference-data transport beyond SD — Ethernet live stream timing. | Deferred post-M8. |
| Q3 | Detector-state bus shape — typed messages on a queue (§4.2.4) is the working choice; revisit if multi-consumer latency becomes a problem. | Working choice. |
| Q4 | Timing pipeline location — marvin centralized with fretboard fallback. | Settled; implementation in M3 + M8. |
| Q5 | UI framework — Legato vs custom on GFX2D. | Open. Decide at M5. |
| Q6 | Operating-mode model — independent toggles. | Settled (§6). |
| Q7 | Fret-tuner's long-term fate — survives as off-band dev/calibration tool. | Settled; not in runtime path. |
| Q8 | Initial CV algorithm choice for `cv_marvin_v1`. | Open. Decide at M1. |
| Q9 | Config persistence location (SD file vs internal flash). | Open. Decide at M4 alongside SDMMC bring-up. |
| Q10 | Game-state recognizer algorithm — template matching vs OCR vs color/region heuristics vs small CNN. | Open. Decide at M9; revisit if first algorithm misclassifies on real game UI. |
| Q11 | Command-path arbitration between game-state controller and timing pipeline (§4.4 vs §4.8). Default working assumption: mutually exclusive (controller runs only outside `gameplay` state); may need richer arbitration if a game has gameplay-screen menus or pause overlays we want to drive. | Open. Decide at M10. |

