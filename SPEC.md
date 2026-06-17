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
            │ FLEXCOM2 UART (cmds)     │ FLEXCOM2 UART (ADC)
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

The diagram shows today's single-board UART path. The architecture is moving to a
**multi-node T1S bus** (§6, [T1S/PoDL link](docs/t1s-podl-link.md)) where sensing and
actuation are separate node *classes* — see "Node classes" below.

**Per-tier role summary:**

- **marvin** — SAM9X75 Curiosity host. Owns HDMI capture, the *reference* CV note detector, ingest of detector-node samples, the chord/strum timing pipeline, command emission to the active guitar node, the on-device LVDS+touch operator UI, and reference-data recording to SD. Selects the active detector and the active guitar among whatever is on the bus. The runtime brain.
- **fretboard** — PIC32CM6408PL10048 MCU. A **T1S detector node** that also drives: it streams 5-channel phototransistor ADC (note brightness at the strike line) up to marvin, and its on-device model infers the button bitmask and sends it **directly to the `guitar` node over T1S** (peer-to-peer actuation). It has no local Wii-guitar outputs (the standalone open-drain GPIO path was removed when the actuator role moved to the `guitar` node). T1S-only as of 2026-06-17.
- **guitar** — PIC32CM PL10 MCU. A **guitar (actuator) node**: receives marvin's button bitmask and drives a Wii guitar controller via open-drain GPIO. New subproject; the actuation half of today's fretboard firmware, on its own node. Multiple guitar variants may coexist on the bus.
- **fret-tuner** — Off-band Python tool (FastAPI + browser UI, Linux/macOS PC). Used at the bench for sensor calibration, detector-algorithm tuning, and replay analysis of recordings produced by marvin. Not in the runtime path.

**Node classes (T1S bus direction).** marvin is the PLCA coordinator (id 0); followers are typed, and marvin selects the active one of each class:

| Class | Role | Node(s) | T1S id / MAC | marvin selection |
|---|---|---|---|---|
| Detector | observe game state → stream to marvin | `fretboard` (photo-ADC); future variants | 1 / `02:…:01` | active detector (`Detector_SetActive`) |
| Guitar (actuator) | receive bitmask → drive a Wii guitar | `guitar` (new); future variants | 2 / `02:…:02` | active guitar (planned) |

Detector nodes feed marvin's detector-state bus (each maps to a `detector_id`); guitar nodes are command TX targets. Both classes scale by adding a node-table row. marvin's own `cv_marvin_v1` is a detector too (internal, not a bus node).

## 3. Subproject specs (where detail lives)

| Subproject | Spec | Journal |
|---|---|---|
| marvin | [`firmware/marvin/docs/spec.md`](firmware/marvin/docs/spec.md) | [`firmware/marvin/docs/journal.md`](firmware/marvin/docs/journal.md) |
| fretboard | [`firmware/fretboard/SPEC.md`](firmware/fretboard/SPEC.md) — phototransistor **detector** node (re-scoping from sensor/actuator; actuator role moving to `guitar`). | [`firmware/fretboard/docs/journal.md`](firmware/fretboard/docs/journal.md) |
| guitar | [`firmware/guitar/SPEC.md`](firmware/guitar/SPEC.md) — Wii-guitar **actuator** node (PIC32CM PL10, T1S PLCA follower id 2). Working: receives marvin's command over T1S and actuates. | [`firmware/guitar/docs/journal.md`](firmware/guitar/docs/journal.md) |
| fret-tuner | [`tools/fret-tuner/SPEC.md`](tools/fret-tuner/SPEC.md) | — |
| marvin-perf | [`tools/marvin-perf/`](tools/marvin-perf/) — perf-log decoder + live/offline web viewer | — |
| edge-ai | [`tools/edge-ai/docs/SPEC.md`](tools/edge-ai/docs/SPEC.md) — design proposal: distill marvin's gameplay commands into a small ML model running on fretboard. Offline development first; Phase 1 data pipeline in progress. | [`tools/edge-ai/docs/journal.md`](tools/edge-ai/docs/journal.md) |
| gameplay | marvin spec [§4.8](firmware/marvin/docs/spec.md) + [`firmware/marvin/docs/gh3_navigation.md`](firmware/marvin/docs/gh3_navigation.md) — GH3 game-state observer/controller offline prototype; algorithms proven against the screen corpus, then ported to the marvin firmware `gameplay_engine`. | [`tools/gameplay/docs/journal.md`](tools/gameplay/docs/journal.md) |
| fauxmote | [`firmware/fauxmote/SPEC.md`](firmware/fauxmote/SPEC.md) — *parallel proof-of-concept:* ESP32 (Adafruit Feather V2) firmware that emulates a Wiimote + guitar extension over Bluetooth to a real Wii, an alternative to the fretboard's physical button-pressing. Not yet in the runtime path; fretboard stays authoritative. | [`firmware/fauxmote/docs/journal.md`](firmware/fauxmote/docs/journal.md) |

marvin's spec also has deeper-dive documents for its capture and display paths ([`capture_pipeline.md`](firmware/marvin/docs/capture_pipeline.md), [`display_path.md`](firmware/marvin/docs/display_path.md)).

## 4. Repository map

```
guitar-pic/
├── README.md                    # short landing page
├── SPEC.md                      # this document
├── CLAUDE.md                    # workflow rules for Claude Code sessions
├── firmware/
│   ├── marvin/                  # SAM9X75 host firmware
│   ├── fretboard/               # PIC32CM6408 phototransistor detector node (re-scoping)
│   ├── guitar/                  # PIC32CM PL10 Wii-guitar actuator node (new)
│   ├── fauxmote/                # ESP32 Wiimote emulator (proof-of-concept)
│   └── sam9x75_curiosity_emirror/   # Microchip reference project (template only)
├── tools/
│   ├── fret-tuner/              # Python dev/calibration tool
│   ├── gameplay/                # GH3 game-state observer/controller offline prototype
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
| PIC32CM6408PL10048 | fretboard detector MCU | [fretboard spec](firmware/fretboard/SPEC.md) |
| PIC32CM PL10 | guitar (actuator) node MCU | [guitar spec](firmware/guitar/SPEC.md) |
| LAN8651B1 (10BASE-T1S MAC-PHY) | T1S bus link, one per node (marvin coordinator + each follower) over single-pair Ethernet + PoDL | [T1S/PoDL link](docs/t1s-podl-link.md) |
| Actuator mechanism (TBD: voice coil / electromagnet / DIY solenoid) | physical fret + strum drive | [`hardware/actuators/`](hardware/actuators/) and [`hardware/3d-models/`](hardware/3d-models/) |
| ElectronWarp | component → HDMI converter for Wii | external commercial part |

The actuator choice is intentionally still open — `hardware/actuators/` contains design + test-protocol docs for three candidates.

## 6. Cross-cutting decisions

- **Architecture pivot.** The original PC-hosted / Elgato-USB-capture / PS2 / single-PIC-actuator design is retired. Source documents are preserved at [`docs/archive/`](docs/archive/) for design-rationale reference; they are *not* maintained.
- **`frame_epoch` is the master sync token.** A 32-bit counter incremented by marvin's video task on every captured ISC frame. Every detector-state record, emitted command, and recording artifact — including data from off-board observers — keys off this token so multi-source data can be aligned post-hoc. Detail in [marvin spec §4.6](firmware/marvin/docs/spec.md).
- **Reference data persists on marvin's SD card.** Detector-state + sparse raw BGR888 keyframes (default 1 keyframe/sec) + raw ADC stream + emitted commands. Off-board detectors record their own data keyed by `frame_epoch` and align offline.
- **Centralized timing on marvin, with a fretboard-takeover fallback.** marvin runs the chord-window FIFO and strum scheduler by default; an operator-mode toggle hands timing back to fretboard's standalone `fret_button.c` while marvin still records observations.
- **Operating modes are independent toggles**, not a global state machine: `detect_enable`, `marvin_timing_enable`, `actuate_enable`, `record_enable`. Named modes (idle / calibrate / dry-run / play / replay / record) are presets over them.
- **Sensing and actuation are separate node classes on the T1S bus.** Detector nodes (e.g. `fretboard`) stream observations; guitar nodes (e.g. `guitar`) actuate. A detector may also *command* a guitar directly (peer-to-peer): the `fretboard`'s on-device model infers the bitmask and sends it to the `guitar` node, while marvin can also command the guitar (its timing pipeline). **The split is live and both ends are on T1S** (2026-06-17): marvin (PLCA coordinator) sends its command to the `guitar` node, and the `fretboard` (T1S-only) streams data to marvin + drives the guitar directly. **Open:** no active-source arbitration yet — both marvin and the fretboard can address the guitar, so only one drives at a time (marvin-side active-detector/active-guitar selection is the follow-up; the fretboard's SW0 is an interim manual arm). Addressing/node-table detail in [`docs/t1s-podl-link.md`](docs/t1s-podl-link.md) §7.1.
- **The marvin↔node link runs on 10BASE-T1S (built; PoDL planned).** A LAN8651B1 MAC-PHY at each end over SPI + the OPEN Alliance TC6 driver + a minimal L2 header (no IP stack); the existing 1-byte/17-byte frame formats ride inside the Ethernet payload unchanged. **Working today between marvin and the `guitar` node** (command + presence heartbeat + diagnostics); the fretboard detector follows. Motivation, in order: PoDL would carry power *and* data on one pair; better noise/cable tolerance; PLCA multidrop headroom; demonstrating 10BASE-T1S + PoDL inside a larger Microchip system. Bandwidth is not a driver (UART had ~12× headroom). **PoDL** (dumb, fixed-voltage, no SCCP) is design-direction only — not built; the link is separately powered for now. Detail in [`docs/t1s-podl-link.md`](docs/t1s-podl-link.md).

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
| ✅ | **M2** — fretboard ↔ marvin link (commands flowing; link rewired USB CDC host → FLEXCOM2 UART for the Curiosity Hybrid board, pending on-hardware re-validation) |
| ✅ | **M3** — end-to-end play (timing pipeline + fretboard actuation; Expert and Easy tested) |
| 🚧 | **M4** — recording-to-SD (detector-state + keyframes + ADC + commands) |
| 🚧 | **M5** — operator UI v0; manual-control surface (8 buttons) done; full live-view + mode-toggle UI not started |
| 🚧 | **M6**+ — calibration UI, replay, fretboard-takeover validation |
| 🚧 | **M9** — game-state observer: Phases 1+2 (screen classifier + section-select/song readers) ported to firmware `gameplay_engine`, MPLAB build confirmed, pending hardware test; Phase 3 (number/score readers) not started |
| 🚧 | **M10** — game-state controller: navigator/closed-loop algorithm complete in `tools/gameplay` prototype; firmware port not started |
| 🚧 | **T1S link** — marvin (PLCA coordinator) ↔ `guitar` actuator node (follower) **working** over 10BASE-T1S (LAN8651 each end): command TX + presence heartbeat + link/`nodes` diagnostics. `fretboard` node (id 1) firmware **written T1S-only** (2026-06-17): streams data to marvin **and** drives the guitar directly (its model infers the bitmask, peer-to-peer); MCC config done. Remaining: fretboard build-wiring + hardware bring-up, active-source arbitration (marvin active-detector/active-guitar selection), and dumb PoDL (power on the pair). [Detail](docs/t1s-podl-link.md) |

Detail (definitions of done, demos) in [marvin spec §8](firmware/marvin/docs/spec.md).

## 8. What this is *not*

To anchor against the original architecture (preserved in `docs/archive/`):

- **Not PC-hosted.** marvin is the runtime brain; no host PC is required for play.
- **Not USB-capture / Elgato.** Video is captured directly off HDMI via a TC358743 → MIPI CSI-2 path into the SAM9X75's ISC.
- **Not PS2.** The supported console is the Nintendo Wii (with an ElectronWarp component → HDMI converter).
- **Not a single-MCU sense+actuate node.** Sensing and timing live on marvin; sensing (detector nodes) and actuation (guitar nodes) are separating onto distinct nodes on the T1S bus, not one solo controller (with the explicit exception of fretboard's standalone-fallback mode during the transition).
- **Not OpenCV / Python at runtime.** CV runs natively on the SAM9X75 (Cortex-A5). Python (`fret-tuner`) is a bench tool only.

## 9. License & acknowledgments

MIT License — see `LICENSE` if present.

Hobby / educational project for learning computer vision and embedded systems. Guitar Hero and Rock Band are trademarks of Activision and Harmonix respectively. Wii is a trademark of Nintendo.
