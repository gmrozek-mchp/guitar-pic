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

In short: marvin is the runtime brain *and* the reference-detector data source for the project.

### 1.2 What marvin is *not*

- **Not the actuator.** Physical fret/strum GPIO drive lives on the fretboard MCU. marvin sends bitmask commands; fretboard converts them to button presses.
- **Not a sensor / detection-only device.** marvin owns timing and command emission. It is not a passive vision peripheral that some other host orchestrates.
- **Not a router for a PC.** The previous PC-based architecture (Elgato + OpenCV + USB to a Pi) is retired. marvin replaces the PC in the runtime path.
- **Not a generic camera platform.** The video source is a fixed HDMI signal from a game console. Suggestions like "just use a USB webcam" or "output to HDMI directly" miss the point.

### 1.3 North-star one-paragraph summary

A SAM9X75 captures Wii HDMI video at 720×480 / 1280×720 @ 60 Hz over a TC358743 → MIPI CSI-2 → ISC path into DDR (BGRX32, zero-copy). It runs reference-quality CV note detection on those frames, fuses with ADC-based detection ingested from a fretboard MCU over UART, schedules chord/strum commands through a low-latency timing pipeline, sends commands back to the fretboard, drives an operator UI on a 10.1″ LVDS panel, and exports a compressed reference-data stream so that lighter-weight detectors (today: fretboard's phototransistors; future: an Edge AI MCU) can be trained against marvin's ground truth.

### 1.4 What's done, what's next

| Subsystem | Status |
|---|---|
| HDMI capture (TC358743 → CSI-2 → ISC → DDR, BGRX32) | ✅ working at 480p60 and 720p60. See [`capture_pipeline.md`](capture_pipeline.md). |
| Display path (DDR → XLCDC OVR1 → LVDSC → panel) | ✅ working, pillarboxed/letterboxed. See [`display_path.md`](display_path.md). |
| FreeRTOS scheduler, video task, OSAL I²C | ✅ landed (commits `5a63704`, `6b85d5e`, `e28c497`). |
| Lightweight log shim | ✅ landed (commit `a79042f`). |
| CV detection pipeline | 🚧 not started. §4.2. |
| Fretboard UART link | 🚧 not started. §4.3. |
| Timing pipeline (chord FIFO, strum) | 🚧 not started. §4.4. |
| Operator UI (Legato vs custom) | 🚧 init-only Legato; full UI not started. §4.5, open Q5. |
| Reference-data recording & export | 🚧 not started. §4.6, open Q1/Q2. |
| System services (config, time, watchdog) | 🚧 partial (logging done). §4.7. |

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
   │   ISC ──► DDR (BGRX32) ──► CV detect ──┐                   │   │
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
                  │ UART (frets/strum cmds)    │ UART (ADC      │   │
                  │ bitmask                    │ stream)        │   │
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
| HDMI capture into memory | marvin | TC358743 + CSI-2 + ISC; 720×480 / 1280×720 @ 60 Hz, BGRX32. |
| Reference-quality CV note detection | marvin | Ground truth; multiple algorithms may coexist on the bus. |
| ADC-based note detection | fretboard → marvin | fretboard scans 5 phototransistors @ 500 Hz, streams raw values; marvin runs the detector logic. |
| Detector-state fusion / arbitration | marvin | One canonical detector-state bus; the active source feeds the timing pipeline. |
| Chord-window FIFO + strum scheduling | marvin | Open Q4: stays on marvin (centralized) vs migrates to fretboard. Default: marvin. |
| Fret/strum GPIO output | fretboard | Open-drain pin drive only; receives a bitmask over UART. |
| Operator UI (live view, calibration, modes, logs) | marvin | 10.1″ LVDS + maxtouch. Open Q5: Legato vs custom. |
| Reference-data recording & export | marvin | Compressed/derived format; transport TBD (open Q1/Q2). |
| Calibration / tuning UI authoring | dev PC (fret-tuner) | Optional; not required at runtime. Open Q7: long-term fate. |
| Training data ingest / replay analysis | dev PC | Off-band consumer of marvin's reference-data export. |

### 2.3 Data flow at a glance

1. **Frame ready** — ISC writes a BGRX32 frame to DDR; the video task notifies subscribers.
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
| **fretboard board** (PIC32CM6408PL10048) | Sensor + actuator MCU | UART (FLEXCOM TBD on marvin, SERCOM1 PB00/PB01 on fretboard, 115 200 Bd default). |
| **Wii guitar controller** | Physical input target | Open-drain GPIO on fretboard, not directly on marvin. |
| **(optional) dev PC** | Calibration / training-data ingest / replay viewer | SD card swap (primary) or USB CDC for live debug; no runtime dependency. |

Pin assignments live in `firmware/marvin/default/src/config/default/pin_configurations.csv`. The capture pipeline doc ([`capture_pipeline.md`](capture_pipeline.md)) is authoritative for CSI / ISC register state; the display path doc ([`display_path.md`](display_path.md)) is authoritative for XLCDC overlay wiring.

### 3.2 SAM9X75 resources used / unused

| Peripheral | Status | Notes |
|---|---|---|
| MIPI CSI-2 RX + ISC + CSI2DC | ✅ in use | 2 lanes, 972 Mbps/lane, BGRX32 to DDR. |
| XLCDC + LVDSC | ✅ in use | OVR1 layer, ARGB_8888 byte order matching BGRX32, pillarbox/letterbox. |
| FLEXCOM6 (I²C) | ✅ in use | TC358743 control. |
| FLEXCOM4 (UART, DBGU/console) | ✅ in use | `printf` retarget; lightweight log shim. |
| FreeRTOS (Harmony OSAL) | ✅ in use | Scheduler running; video task split out (commit `6b85d5e`). |
| TC0 (SYS_TIME) | ✅ in use | OSAL synchronous I²C requires it. |
| GMAC (Ethernet) | ⚪ unused | Reserved for future live-stream ref-data export (not MVP). |
| SDMMC | 🚧 to be enabled | MVP transport for reference-data recording (§4.6). |
| USB device | ⚪ unused | Possible future CDC for dev convenience; not required. |
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
- `adc_fretboard` — runs on top of raw ADC samples streamed from the fretboard MCU (§4.3); emits `detector_state_t` events at the fretboard's 500 Hz rate.

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

### 4.3 Fretboard link 🚧

UART between marvin and the fretboard MCU, bidirectional, framed.

- **Marvin → fretboard:** command bitmask (which frets to assert + strum direction). Same intent as fret-tuner's existing 1-byte bitmask; precise framing TBD in §5.
- **Fretboard → marvin:** raw ADC stream (5 channels × 16-bit) at 500 Hz, plus enable/SW0 state. Reuses the fret-tuner 12-byte ADC frame format if practical.
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
        ├── 000000.bgrx        // raw BGRX32 keyframe (no encoder), filename = frame_epoch
        ├── 000060.bgrx
        └── ...
```

- **`state.bin`** — append-only, fixed-size `detector_state_t` records (§4.2.3). At 60 Hz × 2 detectors × ~28 B = ~3.4 KB/s. Trivial.
- **`commands.bin`** — fret/strum bitmask + direction + emit timestamp + frame_epoch, ~16 B per emit. Sparse.
- **`adc_raw.bin`** — fretboard 12-byte frames at 500 Hz with `frame_epoch` annotation = ~6 KB/s. Modest.
- **Keyframes** — raw BGRX32 dump every N captured frames. At 720×480 BGRX = 1.38 MB/keyframe. Default cadence: **1 keyframe per second** (60-frame stride) → 1.38 MB/s sustained. Configurable (every 30 / 60 / 120 / 600 frames). At 1 fps a 5-minute session is ~415 MB — comfortable on any modern SD card.
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
  "capture": { "width": 720, "height": 480, "fps": 60, "format": "BGRX32" },
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

A FreeRTOS task (`record_task`) owns SD writes. State / command / ADC streams are small and write through directly. Keyframes are larger — a single keyframe at 1.38 MB cannot block the video pipeline, so the record task receives a **frame buffer pointer** (the same DDR buffer the ISC writes into; refcounted by the video task) and writes from DDR while the next frame is being captured into another buffer. We need ≥ 3 capture frame buffers (one capturing, one displaying, one being recorded) when recording is active; today the video task already has multiple frames pre-allocated — exact count is verified during implementation.

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

