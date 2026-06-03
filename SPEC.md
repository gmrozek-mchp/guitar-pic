# Guitar Hero Bot — System Specification

> Top-level overview. For implementation detail of any single subproject, follow the pointers in §3. Each subproject owns its own spec; this document does not duplicate them.

---

## 1. Mission

Build an autonomous robot that plays Guitar Hero / Rock Band on a real Wii guitar controller by watching the game video, detecting notes in real time, and pressing physical buttons in sync with the music. The system must run end-to-end without a host PC in the runtime path, produce reference-quality detector output that lighter detectors can be trained against, and provide an on-device operator UI for calibration, mode control, and replay.

## 2. Current architecture

Three pieces, one runtime brain:

```
Wii ──HDMI──► ElectronWarp ──HDMI──► TC358743 ──CSI-2──┐
                                                       ▼
   ┌──────────────────────  marvin (SAM9X75)  ────────────┐
   │  HDMI capture → CV detect → timing pipeline → cmds   │
   │           ↑              detector-state bus          │
   │           │                       ▲                  │
   │           │  10.1″ LVDS + maxtouch │                  │
   │           │       (operator UI)    │                  │
   │           └────────── SD card ─────┘  (recording)    │
   └────────┬──────────────────────────┬───────────────────┘
            │ USB CDC host (cmds)      │ USB CDC host (ADC)
            ▼                          │
   ┌──────────────────────────────────┐│
   │   fretboard (PIC32CM6408)        │┘
   │   ADC scan ↔ open-drain GPIO     │
   └────────┬─────────────────────────┘
            │ GPIO
            ▼
   Wii guitar controller ──► Wii (closes the loop)

                ◇ off-band: dev PC running fret-tuner
                  (calibration / detector tuning / replay)
```

**Per-tier role summary:**

- **marvin** — SAM9X75 Curiosity host. Owns HDMI capture, the *reference* CV note detector, ingest of fretboard ADC samples, the chord/strum timing pipeline, command emission to fretboard, the on-device LVDS+touch operator UI, and reference-data recording to SD. The runtime brain.
- **fretboard** — PIC32CM6408PL10048 MCU on a sensor/actuator board. Streams 5-channel phototransistor ADC up to marvin and converts incoming bitmask commands into open-drain GPIO presses on a Wii guitar controller. Has a standalone fallback mode where it runs its own chord FIFO without marvin in the loop.
- **fret-tuner** — Off-band Python tool (FastAPI + browser UI, Linux/macOS PC). Used at the bench for sensor calibration, detector-algorithm tuning, and replay analysis of recordings produced by marvin. Not in the runtime path.

## 3. Subproject specs (where detail lives)

| Subproject | Spec | Journal |
|---|---|---|
| marvin | [`firmware/marvin/docs/spec.md`](firmware/marvin/docs/spec.md) | [`firmware/marvin/docs/journal.md`](firmware/marvin/docs/journal.md) |
| fretboard | [`firmware/fretboard/SPEC.md`](firmware/fretboard/SPEC.md) | — |
| fret-tuner | [`tools/fret-tuner/SPEC.md`](tools/fret-tuner/SPEC.md) | — |
| marvin-perf | [`tools/marvin-perf/`](tools/marvin-perf/) — perf-log decoder + live/offline web viewer | — |
| edge-ai | [`tools/edge-ai/docs/SPEC.md`](tools/edge-ai/docs/SPEC.md) — design proposal: distill marvin's gameplay commands into a small ML model running on fretboard. Offline development first; not yet implemented. | — |

marvin's spec also has deeper-dive documents for its capture and display paths ([`capture_pipeline.md`](firmware/marvin/docs/capture_pipeline.md), [`display_path.md`](firmware/marvin/docs/display_path.md)).

## 4. Repository map

```
guitar-pic/
├── README.md                    # short landing page
├── SPEC.md                      # this document
├── CLAUDE.md                    # workflow rules for Claude Code sessions
├── firmware/
│   ├── marvin/                  # SAM9X75 host firmware
│   ├── fretboard/               # PIC32CM6408 sensor/actuator MCU firmware
│   └── sam9x75_curiosity_emirror/   # Microchip reference project (template only)
├── tools/
│   ├── fret-tuner/              # Python dev/calibration tool
│   └── marvin-perf/             # marvin perf-log decoder + web viewer
├── hardware/
│   ├── actuators/               # voice coil / electromagnet / DIY solenoid design + test protocols
│   ├── 3d-models/               # OpenSCAD + STL for printed actuator parts
│   ├── Sensor-LCD5/             # KiCad project for fretboard sensor/actuator PCB
│   └── kicad/                   # shared symbol/footprint libraries
└── docs/
    ├── SAM9X7-Series-Data-Sheet-DS60001813.pdf
    └── archive/                 # historical docs from the original PC/Elgato/PS2 architecture
```

## 5. Hardware platforms (high-level)

| Component | Role | Where to look |
|---|---|---|
| SAM9X75 Curiosity | marvin host | [marvin spec §3](firmware/marvin/docs/spec.md) |
| Waveshare HDMI→CSI-2 adapter (TC358743) | HDMI bridge into marvin | [marvin spec §3](firmware/marvin/docs/spec.md), [capture pipeline](firmware/marvin/docs/capture_pipeline.md) |
| 10.1″ 1280×800 LVDS panel + maxtouch | marvin operator UI surface | [marvin display path](firmware/marvin/docs/display_path.md) |
| Sensor/actuator PCB (Sensor-LCD5) | fretboard board: 5 phototransistors + GPIO out | [`hardware/Sensor-LCD5/`](hardware/Sensor-LCD5/) |
| PIC32CM6408PL10048 | fretboard MCU | [fretboard spec](firmware/fretboard/SPEC.md) |
| Actuator mechanism (TBD: voice coil / electromagnet / DIY solenoid) | physical fret + strum drive | [`hardware/actuators/`](hardware/actuators/) and [`hardware/3d-models/`](hardware/3d-models/) |
| ElectronWarp | component → HDMI converter for Wii | external commercial part |

The actuator choice is intentionally still open — `hardware/actuators/` contains design + test-protocol docs for three candidates.

## 6. Cross-cutting decisions

- **Architecture pivot.** The original PC-hosted / Elgato-USB-capture / PS2 / single-PIC-actuator design is retired. Source documents are preserved at [`docs/archive/`](docs/archive/) for design-rationale reference; they are *not* maintained.
- **`frame_epoch` is the master sync token.** A 32-bit counter incremented by marvin's video task on every captured ISC frame. Every detector-state record, emitted command, and recording artifact — including data from off-board observers — keys off this token so multi-source data can be aligned post-hoc. Detail in [marvin spec §4.6](firmware/marvin/docs/spec.md).
- **Reference data persists on marvin's SD card.** Detector-state + sparse raw BGR888 keyframes (default 1 keyframe/sec) + raw ADC stream + emitted commands. Off-board detectors record their own data keyed by `frame_epoch` and align offline.
- **Centralized timing on marvin, with a fretboard-takeover fallback.** marvin runs the chord-window FIFO and strum scheduler by default; an operator-mode toggle hands timing back to fretboard's standalone `fret_button.c` while marvin still records observations.
- **Operating modes are independent toggles**, not a global state machine: `detect_enable`, `marvin_timing_enable`, `actuate_enable`, `record_enable`. Named modes (idle / calibrate / dry-run / play / replay / record) are presets over them.

## 7. Project-wide phasing

| Status | Item |
|---|---|
| ✅ | marvin HDMI capture → DDR (BGR888 packed) at 480p60 and 720p60 |
| ✅ | marvin DDR → 10.1″ LVDS panel display (HEO, per-frame swap, pillarboxed/letterboxed) |
| ✅ | marvin FreeRTOS scheduler, video task, static task priorities, analytics |
| ✅ | fretboard standalone play (phototransistors + open-drain GPIO) |
| ✅ | fret-tuner used at the bench for detector tuning |
| ✅ | marvin perf-log USB CDC export — live RTOS analytics + pixel strip viewer (`tools/marvin-perf`) |
| ✅ | **M1** — marvin reference detector v0 (`cv_marvin_v1` running, `detector_state_t` bus active) |
| ✅ | **M2** — fretboard ↔ marvin link (USB CDC host over EDBG; commands flowing) |
| ✅ | **M3** — end-to-end play (timing pipeline + fretboard actuation; Expert and Easy tested) |
| 🚧 | **M4** — recording-to-SD (detector-state + keyframes + ADC + commands) |
| 🚧 | **M5** — operator UI v0; manual-control surface (8 buttons) done; full live-view + mode-toggle UI not started |
| 🚧 | **M6**+ — calibration UI, replay, fretboard-takeover validation, game-state controller |

Detail (definitions of done, demos) in [marvin spec §8](firmware/marvin/docs/spec.md).

## 8. What this is *not*

To anchor against the original architecture (preserved in `docs/archive/`):

- **Not PC-hosted.** marvin is the runtime brain; no host PC is required for play.
- **Not USB-capture / Elgato.** Video is captured directly off HDMI via a TC358743 → MIPI CSI-2 path into the SAM9X75's ISC.
- **Not PS2.** The supported console is the Nintendo Wii (with an ElectronWarp component → HDMI converter).
- **Not a single-MCU actuator.** Sensing and timing live on marvin; fretboard is a sensor/actuator MCU, not a solo controller (with the explicit exception of the standalone-fallback mode).
- **Not OpenCV / Python at runtime.** CV runs natively on the SAM9X75 (Cortex-A5). Python (`fret-tuner`) is a bench tool only.

## 9. License & acknowledgments

MIT License — see `LICENSE` if present.

Hobby / educational project for learning computer vision and embedded systems. Guitar Hero and Rock Band are trademarks of Activision and Harmonix respectively. Wii is a trademark of Nintendo.
