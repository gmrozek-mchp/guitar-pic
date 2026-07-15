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

A SAM9X75 captures Wii HDMI video at 720×480 / 1280×720 @ 60 Hz over a TC358743 → MIPI CSI-2 → ISC path into DDR (BGR888 packed, 3 B/pixel). It runs reference-quality CV note detection on those frames, fuses with ADC-based detection ingested from a fretboard MCU over a FLEXCOM1 UART link (or T1S multi-node bus), schedules chord/strum commands through a low-latency timing pipeline, sends commands to the guitar actuator node, drives an operator UI on a 10.1″ LVDS panel, and exports a compressed reference-data stream so that lighter-weight detectors (today: fretboard's phototransistors; future: an Edge AI MCU) can be trained against marvin's ground truth.

### 1.4 What's done, what's next

| Subsystem | Status |
|---|---|
| HDMI capture (TC358743 → CSI-2 → ISC → DDR, BGR888 packed) | ✅ working at 480p60 and 720p60. See [`capture_pipeline.md`](capture_pipeline.md). |
| Display path (DDR → XLCDC HEO → LVDSC → panel) | ✅ working, pillarboxed/letterboxed, per-frame pointer swap. See [`display_path.md`](display_path.md). |
| FreeRTOS scheduler, video task, OSAL I²C | ✅ landed. |
| Lightweight log shim, FreeRTOS analytics, task priorities | ✅ done. |
| CV detection pipeline | 🚧 M1 complete: `cv_marvin_v1` running on captured frames, `detector_state_t` bus active. No actuation path through detector yet. §4.2. |
| Fretboard/guitar link (T1S default, FLEXCOM1 USART fallback) | ✅ working; **T1S is the shipping default** (`user.cmake` hard-sets `MARVIN_FRETBOARD_TRANSPORT=T1S`) — marvin PLCA coordinator, fretboard node 1, guitar node 2, validated 2026-06-17. FLEXCOM1 UART @ 500 kbaud (validated on Curiosity Hybrid) is the fallback transport; in the T1S build FLEXCOM1 instead hosts the fauxmote link (§4.3, §4.9). §4.3. |
| Timing pipeline (chord FIFO, strum) | ✅ working; end-to-end gameplay tested on Expert and Easy. §4.4. |
| Operator UI (Legato) | 🚧 Multi-screen surface live: dashboard, song-select (+ album-art detail), album-art, navigation, splash, and live-video screens, plus custom widgets (`ui/screens/*`, `ui/widgets/*`). Calibration and log/history screens not started. §4.5, open Q5. |
| Reference-data recording & export (SD) | 🚧 SD recording not started. Perf-log USB CDC export (separate dev-tooling path) complete at 2.77 MB/s. §4.6, open Q1/Q2. |
| System services (config, time, watchdog) | 🚧 Partial: logging, FreeRTOS analytics, static task priorities done; config persistence and watchdog not started. §4.7. |
| Game-state awareness & high-level game control | 🚧 M9 Phases 1+2 firmware-complete (screen classifier + section-select/song readers; MPLAB build confirmed, pending hardware test). M9 Phase 3 (number/score readers) not yet started. M10 controller/navigator in progress in firmware (`game/game_controller.c`: menu-step planner, `nav_to_main_menu`, retry/timeout, CV-plays loop; driven by the `play` console command + dashboard button), pending hardware validation. §4.8. |

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
                  │ T1S bus (default)          │ T1S bus        │   │
                  │ (frets/strum cmds)         │ (ADC stream)   │   │
                  │ [FLEXCOM1 UART fallback]   │                │   │
                  ▼                            │                │   │
   ┌────────────────────────────────────┐      │                │   │
   │      fretboard (PIC32CM6408)       │──────┘                │   │
   │   ADC scan ──► UART stream         │                       │   │
   │   cmd RX ──► guitar node via T1S   │                       │   │
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
3. **ADC ingest** — a link task receives raw ADC samples from the fretboard (over T1S by default, UART fallback) and runs an ADC detector that emits detector-state events on the same bus.
4. **Arbitrate** — the timing pipeline picks the active detector (or fuses), maintains the chord-accumulation window, and pushes finished chords onto its FIFO.
5. **Schedule** — the FIFO emits fret-assert and strum-pulse commands at the correct ticks.
6. **Send** — commands are encoded and transmitted to the guitar/fretboard node over the T1S bus (FLEXCOM1 UART fallback).
7. **Display** — the captured frame is composited with overlays (sensor crosshairs, detector outputs, chord state, latency markers) and shown on the LVDS panel.
8. **Record** (optional) — when recording is enabled, a derived/compressed reference-data record (frame epoch + detector state + raw ADC + emitted commands) is buffered and exported via the chosen transport.

### 2.4 Latency budget (target, end-to-end)

| Stage | Budget | Notes |
|---|---|---|
| HDMI source → DDR (capture) | ≤ 16.7 ms | One frame @ 60 Hz; bounded by capture pipeline. |
| Detect on frame | ≤ 16.7 ms | One frame budget for CV; ADC path is separate and faster. |
| Timing pipeline → command emit | latency dominated by the detector **observation lead** (`observation_lead_ms` ≈ 200–250 ms) — intentional, not waste; compensates for sensor placement above strike line. |
| UART command → fretboard GPIO | ≤ 5 ms | Short fixed-format frame at ≥ 115 200 Bd. |
| Total user-visible latency | ≈ 200–250 ms | Tunable via `observation_lead_ms`, set per-detector/per-game by calibration. |

The dominant fixed delay (the detector's `observation_lead_ms`) exists by design — the camera sees notes before they reach the strike line, so the detector stamps a strike-line time (§4.2.3) that far ahead and the pipeline schedules to it. Capture + detect + UART jitter is what we actually budget against.

---

## 3. Hardware platform

### 3.1 Boards and links

| Board | Role | Connection to marvin |
|---|---|---|
| **SAM9X75 Curiosity** | marvin host (CPU, DDR, peripherals, panel/touch ports) | — |
| **Waveshare HDMI → CSI-2 adapter** (TC358743) | HDMI → MIPI CSI-2 bridge | I²C (FLEXCOM8 TWI, PB4/PB5, 400 kHz, addr `0x0F`) for control; 2-lane CSI-2 RX for data; PC15 PWD, PC19 RESET (currently unused — software reset over I²C). |
| **Microchip 10.1″ 1280×800 LVDS panel + maxtouch** | Operator UI | LVDSC pair from XLCDC; I²C for maxtouch (existing Harmony driver). |
| **fretboard board** (PIC32CM6408PL10048) | Detector MCU; streams to marvin & commands guitar over T1S | T1S (node id=1) by default; FLEXCOM1 USART is the fallback (marvin PA28 `GUITAR_TX` / PA29 `GUITAR_RX` ↔ fretboard SERCOM1 PB00 TX / PB01 RX, 500 000 Bd 8N1, ring-buffer mode). In the T1S build FLEXCOM1 carries the fauxmote link instead (§4.3, §4.9). |
| **Wii guitar controller** | Physical input target | Open-drain GPIO on fretboard, not directly on marvin. |
| **(optional) dev PC** | Calibration / training-data ingest / replay viewer + operator console | SD card swap (primary), USB CDC for perf-log, and the FLEXCOM2 serial console (115 200 8N1, via an FTDI channel) for interactive control (§4.9). No runtime dependency. |

Pin assignments live in `firmware/marvin/default/src/config/default/pin_configurations.csv`. The capture pipeline doc ([`capture_pipeline.md`](capture_pipeline.md)) is authoritative for CSI / ISC register state; the display path doc ([`display_path.md`](display_path.md)) is authoritative for XLCDC overlay wiring.

### 3.2 SAM9X75 resources used / unused

| Peripheral | Status | Notes |
|---|---|---|
| MIPI CSI-2 RX + ISC + CSI2DC | ✅ in use | 2 lanes, 972 Mbps/lane; BGR888 packed to DDR (CSI2DC RMS=1, 3 B/pixel). |
| XLCDC + LVDSC | ✅ in use | HEO layer, RGB\_888\_PACKED, per-frame pointer swap, pillarbox/letterbox. |
| FLEXCOM8 (I²C/TWI) | ✅ in use | TC358743 control + display MIPI I²C, shared bus on PB4/PB5 (`DRV_I2C_INDEX_0`). Was FLEXCOM6/PA24-PA25 on the original Curiosity board; moved during the Hybrid port. |
| DBGU (UART) | ✅ in use | `printf` retarget (`printf → xc32_monitor → DBGU`); lightweight log shim. Log/diagnostic chatter only — kept off the console channel. |
| FLEXCOM1 (USART) | ✅ in use | Fretboard-link UART fallback — 500 000 Bd 8N1, ring-buffer mode (§4.3). In the shipping T1S build it hosts the fauxmote link instead (§4.3, §4.9). |
| FLEXCOM2 (USART) | ✅ in use | Operator command console — 115 200 Bd 8N1, ring-buffer mode (§4.9). |
| FreeRTOS (Harmony OSAL) | ✅ in use | Scheduler running; video task split out (commit `6b85d5e`). |
| TC0 (SYS_TIME) | ✅ in use | OSAL synchronous I²C requires it. |
| GMAC (Ethernet) | ⚪ unused | Reserved for future live-stream ref-data export (not MVP). |
| SDMMC | ✅ in use | Card mounted at `/mnt/marvin` (`storage/storage.c`, `app.c`); `sd` console command live; results / catalog / album-art loaders build on it. The §4.6 recording *writer* is still not started. |
| USB host (EHCI + OHCI) | ⚪ removed | Was the fretboard link on the original Curiosity board; the Curiosity Hybrid has no host-capable port, so the link moved to a direct UART (§4.3). |
| USB device (UDPHS) | ✅ in use | Perf-log CDC ACM sink; marvin presents as USB device to dev PC, streams perf records at up to 2.77 MB/s. |
| Maxtouch I²C | 🚧 driver patched | Bounded-init / headless-fallback fix implemented (MCC re-apply patch #10); operator UI surface (§4.5) not yet wired up. |
| Watchdog | 🚧 not configured | System services (§4.7). |
| Free FLEXCOMs | several available | FLEXCOM1 = fretboard-link UART fallback / fauxmote link in the T1S build (§4.3), FLEXCOM2 = console (§4.9), FLEXCOM8 = I²C; others remain free. |

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
    uint32_t strike_at_ms;    // timestamp_us/1000 + detector observation lead;
                              //   when this observation reaches the strike line.
                              //   The timing pipeline (§4.4) schedules in this base.
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

- A single FreeRTOS queue (`s_bus_queue`) carries `detector_state_t` records, one entry per detector emit. The timing pipeline task (§4.4) is the sole consumer.
- **Source arbitration is on the push side, in the detector module.** Detectors emit via `Detector_Publish`, which forwards a record onto the bus only if that detector is the *active* one (`Detector_SetActive`); a source switch drains the queue. So the bus carries a single source's stream and the consumer never filters. Non-active detectors still run and feed the recording/UI paths.
- The recording task (§4.6) does **not** read this bus — recording is a separate side-channel: each detector emits a `perf_rec_detector_t` (via `PerfLog_EmitDetector`) regardless of active state, so *all enabled* detectors' ground truth is captured for side-by-side training data.
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

**Transport.** The shipping default is the **10BASE-T1S bus** (`user.cmake` hard-sets `MARVIN_FRETBOARD_TRANSPORT=T1S`; see [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md)): marvin is the PLCA coordinator and the guitar/fretboard nodes are followers. The **FLEXCOM1 USART** below is the fallback transport, selected by building with `MARVIN_FRETBOARD_TRANSPORT=UART`. The 1-byte command and 17-byte ADC frame formats are identical on either transport. In the T1S build FLEXCOM1 is free and instead hosts the **fauxmote link** (see below and §4.9).

Direct **FLEXCOM1 USART** link between marvin and the fretboard MCU — a plain UART wire, no USB. marvin's `PA28` (FLEXCOM1_IO0, `GUITAR_TX`) and `PA29` (FLEXCOM1_IO1, `GUITAR_RX`) connect to the fretboard's SERCOM1 (`PB01` RX / `PB00` TX). Both ends run **500 000 baud, 8N1**. The FLEXCOM1 USART runs in Harmony ring-buffer mode so RX never drops bytes between reads. (Was FLEXCOM2 until the console moved onto FLEXCOM2 — see §4.9.)

- **Marvin → fretboard:** command bitmask (which frets to assert + strum direction). 1-byte bitmask, no framing. `fretboard_link_task` writes the latest mask via `FLEXCOM1_USART_Write`.
- **Fretboard → marvin:** raw ADC stream (5 channels × 16-bit) at 240 Hz, in a 17-byte start/end-bracketed frame (`0x03 … 0xFC`, with `sample_seq` + `applied_mask`). The FLEXCOM1 RX ring fills continuously from its ISR; a persistent read-threshold notification wakes `fretboard_rx_task`, which drains the ring, resyncs on the frame markers, and republishes each valid frame as a `PERF_REC_FRETBOARD_RAW` perf-log record (default-disabled, host enables via `PERF_CMD_SET_TYPE_MASK`). This is the on-board capture path until the SD-card recorder lands; it's also the substrate for the future `adc_fretboard` detector (§4.2).
- **Standalone fallback:** when marvin's timing pipeline is disabled (see §4.4 and §6), the fretboard runs its own existing chord FIFO (`fret_button.c`) and continues to stream ADC + emitted-command telemetry to marvin for capture/display.
- **Fauxmote link (T1S build only):** with the guitar node on T1S, FLEXCOM1 is free, so in the T1S build marvin brings up a link to the fauxmote board (ESP32 Wiimote emulator, §7 top-level SPEC) on FLEXCOM1 (`net/fauxmote/fauxmote_link.c`, `Fauxmote_Initialize`). It mirrors every gameplay bitmask — `FretboardLink_Send` taps `Fauxmote_SendGuitarMask` — and is inspected/controlled via the `fauxmote` console command (§4.9).

### 4.4 Timing pipeline 🚧

Centralized on marvin by default. It is the **decide layer** — a peer of the detector (§4.2) and the actuator link (§4.3), not part of either — and lives in the game-control subsystem (`game/timing_pipeline.c`) alongside `game_controller` (§4.8), the other autonomous command producer. The two are mutually exclusive (note-highway gameplay vs. menu navigation); coordinating them is open **Q11**. It schedules **in the strike-line time base** — it acts on each detector record's `strike_at_ms` (§4.2.3) rather than holding an observation-delay constant of its own; the observation lead now lives with the detector. Owns:

- Chord-accumulation window (`CHORD_WINDOW_MS` ≈ 20–40 ms).
- Pending-chord FIFO with per-chord `press_at` / `strum_at` ticks (both derived from `strike_at_ms`).
- Per-fret release scheduling.
- Strum direction alternation and pulse generation.
- `FRET_EARLY_MS` / `STRUM_PULSE_MS` musical-scheduling constants — initial values port from `tools/fret-tuner/SPEC.md` and `firmware/fretboard/SPEC.md`. (The former `STRUM_DELAY_MS` is now the detector's per-config `observation_lead_ms`.)
- **Actuator advance:** subtracts the active actuator node's declared mechanical advance (`FRETBOARD_ACTUATOR_ADVANCE_MS`, 0 for the open-drain guitar node) when deciding when to emit, so a solenoid rig's rise time lands the effect on the strike line. The wire byte stays a bare "assert now" mask (Option A; see journal 2026-07-15).

**Disable / handoff mode:** an operator-mode toggle (§6) deactivates marvin's pipeline. fretboard then runs its own existing pipeline; marvin still ingests ADC + emitted-command telemetry for recording/display, but does not emit commands.

### 4.5 UI / operator surface 🚧

10.1″ 1280×800 LVDS panel + maxtouch capacitive touch. Functional surfaces required:

- Live game view (capture composited with detector overlays — sensor crosshairs, detection events, latency markers).
- Calibration / tuning (per-fret ROI placement, threshold values, timing constants).
- Mode + run controls (idle, calibrate, dry-run, play, replay, record).
- Logs / history / stats (recent commands, miss/hit counts, dropped frames).
- Song picker with album artwork and per-player results (§4.8.6, §4.8.7) — artwork is decoded via Legato's already-enabled JPEG/PNG decoders.

**Built today:** a multi-screen UI is live — dashboard, song-select (with an album-art detail view), album-art, navigation, splash, and live-video screens (`ui/screens/*`), backed by custom widgets (`ui/widgets/*`: `button_aa`, `song_list`, `panel_aa`). The calibration/tuning and log/history/stats surfaces above remain not started.

**UI framework — open Q5.** Legato is already pulled in for capture init and could absorb the operator UI directly (heavy but in-tree). Alternative: a lightweight custom widget layer over GFX2D / direct framebuffer composition. Decision deferred until we attempt the first non-trivial screen (calibration overlay).

**Presentation layer (decided 2026-06-25).** For the *presentation* half of Q5: keep **Legato as the renderer**, add a thin marvin **compositor over the GFX Canvas component** — pre-render panels into static non-cached RAM surfaces and multiplex the two free LCDC overlay layers (`OVR1`/`OVR2`) across them for instant reveal/slide without redrawing what's behind. Authoritative design in [`ui_compositor.md`](ui_compositor.md). This does not dictate per-screen *authoring* (MGS vs. custom widgets).

**Fonts — DejaVu Sans Mono only (decided 2026-06-30).** For licensing reasons the UI uses **only the DejaVu Sans Mono family** (`DejaVuSansMono_*`, `DejaVuSansMonoBold_*`). Any new text — labels, widgets, dynamic strings — must use a DejaVu Sans Mono face; do **not** introduce other faces (e.g. the figma-imported Menlo / NotoSans assets MGS may pull in). The DejaVu mono faces are subsetted with `0x20–0x7E`, `0xA0–0xFF`, and `0x2605` (★), so Latin-1 plus the tier star are available. Some legacy MGS-imported faces remain in the generated font set and are a known cleanup item — migrate them to DejaVu over time.

### 4.6 Reference-data recording & export 🚧

#### 4.6.1 Goals (in priority order)

1. **Edge-AI training data** — produce labelled frames + ground-truth detector state suitable for training low-fidelity detectors (fretboard's photo-detector logic; future Edge-AI MCU).
2. **Replay / sanity check** — let the user (offline) scrub through a recorded session to investigate misses.
3. **Multi-detector sync** — independent detectors (fretboard ADC, future Edge-AI) record their own data alongside marvin's; everyone sync's on a shared frame epoch.

Live off-device streaming for side-by-side comparison is *not* on the MVP critical path.

#### 4.6.2 Transport — SD card

Recordings are persisted to an SD card via SDMMC + a simple filesystem (FAT32 via Harmony's FILE_SYSTEM service). The card is removed and read on a workstation; no live network path is required for MVP.

Bandwidth ceiling: SDMMC sustained write ≥ 5 MB/s on Class-10 cards is reliably achievable. The recording format below is sized to land well below that.

The card is also the runtime store for everything that changes independently of the firmware image — the song catalog, album artwork, and per-player results — alongside recordings. (Config persistence lives on QSPI flash, not the card — see §4.7.) Canonical layout (single reference for all subsystems):

```
/marvin/
├── games/
│   └── gh3-wii/
│       ├── songs.csv           // song catalog labels (§4.8.3)
│       └── art/
│           ├── small/<setlist>-<NN>.{jpg,png}   // album artwork, named by recognizer key (§4.8.7)
│           └── large/<setlist>-<NN>.{jpg,png}   // one size tier per fixed-size DDR cache
├── players/
│   └── results.csv             // append-only per-player performance records (§4.8.6)
└── recordings/
    └── <session>/              // §4.6.3
```

Recognizer/menu-graph data is **not** on the card — it stays compile-time in flash (§4.8.3), so recognition has no card-absent or stale-data failure mode.

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
- **`adc_raw.bin`** — fretboard 17-byte frames at 240 Hz with `frame_epoch` annotation = ~4.1 KB/s. Modest. Until the SD recorder lands, the same data is available live as `PERF_REC_FRETBOARD_RAW` records (28 B framed) in any marvin-perf capture (default-disabled; enable with `set-mask`).
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

#### 4.6.8 On-demand full-frame snapshot (USB CDC) ✅

Independent of the SD recording path above (M4, not started), marvin can export a single full-resolution frame on demand over the existing perf-log USB CDC link. The host sends `PERF_CMD_SNAPSHOT`; the perf-drain task copies the current frame once into a staging buffer and streams it back as a top-to-bottom run of full-width `PERF_STRIP_SNAPSHOT` band records (each ≤ `PERF_STRIP_MAX_BYTES`), all sharing one `frame_epoch`, the last flagged `PERF_STRIP_FLAG_LAST`. Bands are written straight to the sink (bypassing the small strip pool) and paced by the wire; emit is not gated by the STRIP type mask. `marvin-perf snapshot` reassembles and saves the frame (`.bgr` + sidecar `.json`, plus `.png` via Pillow). This is the offline-study on-ramp for the game-state work (§4.8) — no in-runtime role.

### 4.7 System services 🚧

Cross-cutting services not owned by any one subsystem:

- **Logging** ✅ severity-filtered printf shim (commit `a79042f`).
- **Time** ✅ TC0 / SYS_TIME for OSAL.
- **Config persistence** ✅ implemented on **QSPI NOR flash**, not the SD card (resolves Q9). `flash/settings.c` maintains a power-fail-safe 256-byte-slot ring-log (`MVST` magic + CRC), exposing `Settings_Load/Get/Save/SetBacklight`. Today it persists the backlight brightness, which is restored at splash (`ui_manager.c`); the `settings` console command dumps/saves/wipes/stress-tests the store. Loads at first use, before the scheduler if needed, and survives with no card present; absent/invalid record → compiled-in defaults. Additional fields (calibration values, mode toggles, recording stride) extend the same record.
- **Watchdog** 🚧 not yet enabled.
- **OTA** ⚪ out of scope for now.

### 4.8 Game-state awareness & control 🚧

Note on numbering: this subsystem is logically a peer of §4.2–§4.6 (a video-frame consumer that emits typed events plus a controller that issues actuator commands). It lives at §4.8 only to avoid renumbering existing sections referenced from the journal and milestones.

#### 4.8.1 Role

Two related capabilities, both grounded in CV on the captured video stream:

1. **Game-state observer.** Watch the screen; recognize what context the game is currently in — main menu, song-select, difficulty-select, in-game (gameplay), pause, score summary, etc. Surface that as typed events that the operator UI can render and the orchestrator below can act on.
2. **Game controller.** Expose high-level verbs ("start a single-player playthrough of song X on Hard", "go to the main menu", "select Practice mode"). Translate each verb into a sequence of fret/strum commands using the recognized current state and a known menu graph, monitor the screen for the expected state transitions, retry or back out on mismatch.

This is *not* the gameplay note-detection path (§4.2). cv_marvin_v1 plays notes during gameplay; the game-state module decides *what to play* at the session level (which song, which difficulty, which mode) and gets us there from any starting screen.

Recognition is bootstrapped offline: the on-demand snapshot path (§4.6.8) collects a corpus of real GH3 screens, detection algorithms are prototyped host-side in Python (`tools/gameplay/`) against that corpus, and only the proven GH3-specific logic is ported into the firmware `gameplay_engine` module. Per Q10, the approach favors fixed-region/color/glyph matching over general CV — GH3's screens, fonts, and layouts are static.

**M9 progress:** Phase 1 (screen-context classifier: main_menu / song_select / gameplay / pause / score) and Phase 2 (section-select and song-name readers) are prototyped and ported to firmware (`gameplay_engine.c`); the MPLAB build is confirmed. Phase 3 (number/score readers) is not yet started. The firmware module is pending hardware validation.

**M10 progress:** the game controller (capability 2 above) is in progress in firmware — `game/game_controller.c` implements a menu-step planner (`nav_to_main_menu`, per-step wait/retry/timeout, and a CV-plays-until-song-end loop), driven by the `play` console command and the dashboard's play button. Pending hardware validation.

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

**Storage split (decided).** The line is drawn by *what the data is coupled to*, not by convenience:

- **Compile-time, in flash:** recognizer templates/signatures (centroids, menu baselines, song bitmap templates, thresholds) **and** the menu graph. These are *algorithm-coupled* — a template is only valid paired with the exact grid geometry, sampling, and normalization compiled into the C recognizer, and is re-learned in lockstep when the algorithm changes. They are generated from the host corpus by `tools/gameplay/gameplay/export_c.py` into `gameplay_metadata.h` and consumed through pointers by `gameplay_classify.c` / `gameplay_select.c`. **Recognition never reads the SD card** — so a missing or stale card can never cause a silent misclassification.
- **On the SD card (`/marvin/games/<game>/songs.csv`):** the song catalog *labels* — the human-meaningful, user-editable metadata that changes independently of the firmware. A flat CSV (header + one row per song), keyed by the stable `(setlist, index)` identifier the recognizer already emits (`gp_song_t`):

```
setlist,index,title,artist,album,bpm,length_s,year,genre,difficulty
main,4,"Rock and Roll All Nite","Kiss","Alive!",120,210,1975,"Hard Rock",
```

`setlist` is the string `main`/`bonus` (the same key the art filenames use); `title`/`artist`/`album`/`genre`/`difficulty` are CSV-quoted (may contain commas; any may be empty); `bpm`/`length_s`/`year` are integers, `0` when unknown. Rows may be sparse or out of order — only `(setlist, index)` is the key. `title`/`artist`/`album` come from the `SONGS` table in `tools/fetch_gh3_cover_art.py`, whose `--catalog` mode also fills `year`/`genre` from MusicBrainz; `bpm`/`length_s`/`difficulty` are reserved-but-blank (GH3 surfaces no per-song difficulty — the only authentic signal is the career tier, deferred).

**Why CSV (not JSON).** Same rationale that put `results.csv` (§4.8.6) on flat CSV: on-device it parses with comma-splitting + `atol` into static buffers (no malloc, no JSON tokenizer, one line at a time — fits `FF_FS_MAX_FILES=1` and the static-allocation rule), and it opens directly in pandas/Excel for editing. The on-device reader (`game/catalog.c`) lazy-loads it once into a fixed `[GP_N_SONGS]` cache (shares the `util/csv.h` splitter with `results.c`) and serves `(setlist, index) → labels` lookups from RAM with the file closed.

The catalog is *labels only* — recognition does not depend on it; a missing/stale catalog degrades to "Unknown song", never a functional break, and a `(setlist, index)` mismatch degrades gracefully rather than misclassifying.

> **Stale-catalog warning — deferred.** The intent is for `export_c.py` to emit a `metadata_version` `#define` into `gameplay_metadata.h` and carry the same stamp in the catalog so the UI can warn when the catalog predates a template-set regeneration. `export_c.py` does **not** emit that define yet, so the loader ships without the version check; it still degrades safely on every miss. Wire the stamp + warning once `export_c.py` produces it.

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

#### 4.8.6 Performance results & player profiles 🚧

Per-player gameplay results, stored on the SD card as `/marvin/players/results.csv` — a flat,
append-only CSV (one row per completed run, with a header row). Keyed to a song by the same
`(setlist, index)` the recognizer emits:

```
player,game,setlist,index,song,difficulty,part,score,accuracy_pct,notes_hit,notes_total,timestamp
greg,gh3-wii,main,4,"Slow Ride",hard,lead,123456,92.4,480,520,2026-06-23T23:14:00Z
```

**Why CSV (not JSON/YAML or a DB).** Results have two consumers — Marvin reads `results.csv` to show
high scores on-device, and the card is taken to a PC for deeper analysis. CSV is the only format that
serves both well: on-device it parses with comma-splitting + `atol` into static buffers (no malloc,
no JSON tokenizer, stream one line at a time — fits `FF_FS_MAX_FILES=1` and the static-allocation
rule); on a PC it's a one-liner in pandas/Excel/awk. JSONL is nicer for nested/evolving schemas but
costs an on-device tokenizer for no benefit here (results are flat and tabular); YAML has no good
zero-alloc embedded parser; a DB (SQLite) needs malloc + a FatFs VFS shim — overkill for an
append-only log. Schema evolution: append columns at the end (the on-device reader touches only the
columns it needs; pandas keys by header). The `song` title is duplicated alongside `(setlist,index)`
so the CSV is self-contained for analysis without joining the catalog.

Scope boundary: `results.csv` is the **flat per-run summary** only. Deep per-note / per-section
telemetry belongs in the recording subsystem (§4.6 `state.bin`), keeping this file trivially
parseable on-device.

On-device high scores: scan the file, filter by `(setlist, index[, difficulty])`, keep a fixed
top-N array in static memory. A full scan per display is fine at expected scale (hundreds–thousands
of rows ≈ tens–hundreds of KB); if it ever grows large, cap the log or maintain a small separate
high-score cache.

Dependencies and open points:

- **Score/accuracy capture depends on the number/score readers — M9 Phase 3**, not yet started. Until
  those land, only the song identity + difficulty/part are recordable. The CSV writer + on-device
  top-N reader + a `scores` console command can be built and validated now against synthetic rows,
  since they sit entirely on the proven SD/FatFs/RTC layer (§4.6.2, §4.8.7-adjacent).
- **`timestamp` is UTC from the RTC** (§4.7), confirmed persistent across power cycles. FAT has no
  timezone field, so file mtimes are also written in UTC by convention.
- **Player identity** is a simple operator-entered string for now (e.g. a `player` console command
  setting the current name); richer profiles are out of scope. Tracked as Q12.

#### 4.8.7 Album artwork 🚧

Per-song cover art for the operator UI, stored on the SD card as standard **JPEG or PNG** (so the files are viewable on any workstation) under `/marvin/games/<game>/art/`. Named by the same stable recognizer key as the catalog, so firmware derives the path directly — no `songs.json` lookup, no dependency on the catalog being present:

```
art/small/<setlist>-<NN>.jpg     // 144×144 letterboxed thumbnail (dashboard now-playing)
art/large/<setlist>-<NN>.png     // 508×208 middle strip, difficulty-colored fade baked in (song-select detail)
```

**Size tiers = size-tier subdirectories.** The two artwork sizes live in sibling subdirs (`art/small/`, `art/large/`), each mapping 1:1 to one fixed-size DDR pre-decode cache — the firmware walks one subdir per tier and all images in it share a slot dimension, so adding a third tier is just another subdir. The filename within a tier is the recognizer key (`<setlist>-<NN>`), identical across tiers; the file *format* is per-tier (small JPEG, large PNG) and the loader picks the decoder from the extension.

**Built (2026-06-30): `game/art.{c,h}`** — implements this section.

**Pre-cache at startup, not per-use.** `Art_LoadAll()` runs from the UI boot task during the splash (after the FAT mount + Legato decoder init, before the reveal): it walks each size subdir and decodes every image **once** into that tier's static, pre-allocated cache of fixed-size slots in DDR; runtime artwork access (`Art_Small`/`Art_Large(setlist,index)` → `leImage*`) is then an O(1) pointer into RAM — no card I/O, no decode, and no allocation at use time (consistent with the static-allocation rule). A missing/oversized/wrong-size/corrupt file leaves its slot empty → lookup returns NULL → UI blank; boot never fails.

- **Decoder — Legato's own runtime decoders; no new library.** JPEG and PNG are already enabled (`legato_config.h`: `LE_ENABLE_JPEG_DECODER=1`, `LE_ENABLE_PNG_DECODER=1`). With `LE_STREAMING_ENABLED=0` the flow is: read the whole compressed file into a shared scratch buffer → read true dims from the header (JPEG `SOF` / PNG `IHDR`) → describe a source `leImage` (`LE_IMAGE_FORMAT_JPEG`/`_PNG`) → `leImage_Render` it into the slot, which the decoder's `render` path writes (color-converted) with no clip-rect/active-frame dependency. Source images are authored at the exact tier WxH (no on-device scaling). **Two stock-Legato gotchas for the PNG path** (both carried as MCC re-apply patches, see journal): the PNG `_render()` is buggy (copies the compressed source + skips the channel byte-swap → garbage; patched to mirror `_draw()`, bug to file upstream), and a full 508×208 PNG decode needs `LE_VARIABLEHEAP_SIZE` raised to 2 MB (default 512 KB → intermittent lodepng OOM). The JPEG path has neither issue.
- **Cache pixel format & size — RGBA8888.** Each slot is a decoded RGBA8888 raster (alpha forced opaque after decode). **Why not RGB888:** the 2D engine (`gfx2dFormats[]`) supports only `RGB_565` and `RGBA_8888` — RGB888 is `-1` (unblittable as either source or destination), so RGB888 slots/layers render garbage; RGBA8888 is the engine's full-color mode. small = 144×144×4 ≈ 83 KB; large = 508×208×4 ≈ 423 KB. For `GP_N_SONGS`=64: small ≈ 5.3 MB + large ≈ 27 MB + a 512 KB shared decode scratch ≈ **33 MB**. These live in **cached** DDR (`.region_ram`, 224 MB region) — decoder staging the 2D engine blits onto the canvas surfaces, never LCDC-scanned directly. On-card art totals ≈ 6 MB. The **large art shows on its own RGBA8888 layer-screen** (Marvin layer 3 / OVR2, 508×208) composited over the RGB565 song-select dialog (OVR1), so only the art rect pays 32bpp bandwidth; the cover image + the TIER/title/artist labels live on that layer.
- **Large tier is a 508×208 strip with a difficulty-colored fade, baked offline.** The song-select detail window is 508×208 and shows the cover's middle horizontal band. The crop **and** the top/bottom fade are baked offline by `tools/gh3-cover-art/process_gh3_cover_art.py`, which reads each song's `difficulty` from `songs.csv` and colors the fade from an 8-tier palette: numeric `"1".."8"` map green→red with blue/purple accents in between (the `TIER_COLORS` table, shared with the song-select TIER-label schemes so art and label match), `"bonus"`/unknown = gray. Baking (vs a runtime overlay) keeps the device module to decode→store→display and the cached array display-ready; cost is a re-bake if a difficulty changes. Small tier is a clean 144×144 letterbox, no fade.

### 4.9 Operator command console ✅

An **interactive text console** over **FLEXCOM2 USART (115 200 8N1, ring-buffer mode)** — marvin's `PA13` (FLEXCOM2_IO0, `CLI_TX`) / `PA14` (FLEXCOM2_IO1, `CLI_RX`) — deliberately on a separate channel from the DBGU log/printf chatter — log output stays on DBGU, console I/O stays on FLEXCOM2, so neither pollutes the other.

- **Why not Harmony `SYS_CONSOLE`/`SYS_COMMAND`:** those are MCC-config-coupled and live in regenerated files (this board already carries ~10 re-apply patches against clobbered generated code). The console is instead a small in-tree module (`console/console.{h,c}`) in the same shape as `fretboard_link`/`perf_log`: it owns the USART plib directly, is fully statically allocated, and is kept out of MCC via `user.cmake`.
- **Library:** [embedded-cli](https://github.com/funbiscuit/embedded-cli) (vendored under `default/src/third_party/embedded-cli/`, MIT), used in static-allocation mode (a fixed `CLI_UINT` buffer → no `malloc`). Provides line editing, history, and tab-completion.
- **Mechanics:** one FreeRTOS task drains the FLEXCOM2 RX ring (woken by a 1-byte read-threshold notification, like `fretboard_rx_task`), feeds bytes to embedded-cli, and runs the dispatcher. Output is written byte-by-byte into the TX ring.
- **Commands:** the live binding table (`console.c` `register_commands`) is: `status`, `t1s`, `nodes`, `sd`, `health`, `time`, `player`, `scores`, `results`, `catalog`, `art`, `detect <cv|adc> <on|off>`, `active <cv|adc>`, `timing <on|off|gate <on|off>>`, `manual <on|off>`, `play`, `fauxmote` (T1S build only), `fret <g|r|y|b|o> <0|1>`, `strum <down|up>`, `backlight <0-100>`, `gamma <on|off>`, `qspi`, `settings` — dispatching into the existing `Detector_*`, `TimingPipeline_*`, `ManualControl_*`, `GameController_*`, storage/RTC, and settings setters. The binding table is a plain static array; adding a command is one row. Maps onto the §6 operating-mode toggles. (`record`/`game_*` observe toggles and runtime timing-constant setters are deferred until those subsystems / setters exist.)

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
4. **M4 — Recording to SD** (§4.6). SDMMC + FAT mount; record task writes state/commands/ADC/keyframes during a session. The same FAT mount is the storage substrate for config (§4.7), the song catalog (§4.8.3), album artwork (§4.8.7), and per-player results (§4.8.6) — those loaders/writers build on it (results also need M9 Phase 3).
5. **M5 — Operator UI v0** (§4.5). Live view with overlays; mode toggles. UI framework decided here.
6. **M6 — Calibration UI** (§4.5). Per-fret ROI placement + threshold tuning on the device.
7. **M7 — Replay** (§6). Load a recording from SD, replay through the timing pipeline.
8. **M8 — Standalone-fretboard fallback** (§4.4). Marvin-disabled-pipeline mode validated.
9. **M9 — Game-state observer v0** (§4.8). 🚧 Phases 1+2 ported to firmware and MPLAB build confirmed (screen classifier + section-select/song readers); pending hardware test. Phase 3 (number/score readers) not yet started. Full milestone done when all contexts recognized and surfaced as `xGameStateQueue` events.
10. **M10 — Game-state control v0** (§4.8). 🚧 Navigator/closed-loop algorithm complete in `tools/gameplay` prototype; firmware port in progress (`game/game_controller.c`: menu-step planner, `nav_to_main_menu`, retry/timeout, CV-plays loop; driven by the `play` console command + dashboard button), pending hardware validation. Done when high-level verbs ("start single-player song X") drive menu navigation through the same fretboard link.

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
| Q9 | Config persistence location (SD file vs internal flash). | Resolved: **QSPI NOR flash** — `flash/settings.c`, an `MVST`-magic 256-byte-slot ring-log (not an SD file). Backlight persisted/restored; `settings` console command (§4.7). |
| Q10 | Game-state recognizer algorithm — template matching vs OCR vs color/region heuristics vs small CNN. | Open. Decide at M9; revisit if first algorithm misclassifies on real game UI. |
| Q11 | Command-path arbitration between game-state controller and timing pipeline (§4.4 vs §4.8). Default working assumption: mutually exclusive (controller runs only outside `gameplay` state); may need richer arbitration if a game has gameplay-screen menus or pause overlays we want to drive. | Open. Decide at M10. |
| Q12 | Performance-result player identity (§4.8.6) — the player-id scheme (operator-entered string vs. selectable profiles). | Partly resolved: timestamps are UTC from the RTC (persistent across power cycles); results format is CSV. Player-id scheme still open; decide when the gameplay write path is wired (M9 Phase 3). |
| Q13 | Album-artwork target dimensions + cache pixel format (§4.8.7). | **Resolved 2026-06-30 (on hardware):** small 144×144, large 508×208 (middle strip, difficulty-colored fade baked offline); cache + display pixel format **RGBA8888** (the 2D engine can't blit RGB888). Built in `game/art.{c,h}`, decoded once into static cached-DDR caches (~33 MB) during the splash; large shows full-color on a dedicated RGBA8888 layer (OVR2) over the RGB565 dialog. Needed two MCC re-apply patches (PNG `_render` fix + 2 MB Legato heap). |

