# docs/archive/

Preserved historical documentation from the project's original architecture (≤ 2026-04). The system has since been re-architected; see the top-level [`SPEC.md`](../../SPEC.md) and the subproject specs for current state.

**Files here are kept for design-rationale reference and are not maintained.** Specifically: hardware part numbers, wiring diagrams, calibration steps, and timing assumptions in these documents reflect the *original* PC-hosted / Elgato-USB-capture / PS2 / single-PIC-actuator design — not the current SAM9X75 (marvin) + PIC32CM6408 (fretboard) split.

## What's here

| File | Era | What it describes |
|---|---|---|
| [`SPEC-original.md`](SPEC-original.md) | 2026-01 → 2026-04 | The original full project spec. PS2/Wii console choice, Elgato HDMI-USB capture into a PC running OpenCV, PIC microcontroller driving solenoid actuators. Carries a 2026-05-20 supersession banner pointing at the live subproject specs. |
| [`README-original.md`](README-original.md) | 2026-01 → 2026-04 | The original landing-page README. Quick-start instructions point at the (now deleted) `vision/` Python pipeline. |
| [`hardware-bom.md`](hardware-bom.md) | 2026-01 era | Bill of materials for the original architecture: PIC, solenoids, drivers, 12 V PSU, Elgato capture, PS2/Wii guitar. Useful as a reference for actuator-side parts (drivers, PSU sizing); the host-side parts (capture device, PC) are obsolete. |
| [`wiring-diagram.md`](wiring-diagram.md) | 2026-01 era | Electrical interconnects for the PC ↔ PIC ↔ console era. Pinouts and signal levels are valid as physical-electrical reference; topology is obsolete. |
| [`calibration.md`](calibration.md) | 2026-01 era | Timing-calibration procedure assuming the PC-based scheduler in the now-deleted `vision/` folder. Marvin handles timing on-device; calibration UI for the new architecture lives in marvin (TBD, marvin spec §4.5). |

## Why these are preserved (not deleted)

- The actuator-side design rationale (driver topology, PSU sizing, mechanical mounting, phototransistor placement) is still relevant to the current architecture.
- Latency budgets, calibration math, and detection-threshold approaches in the original docs informed the timing constants now in fret-tuner / fretboard / marvin specs.
- Historical context is occasionally useful when revisiting a design choice.

## Related deletions

The original Python vision pipeline (`vision/`) and its tests (`tests/`) referenced from `README-original.md` were deleted from the working tree on 2026-05-20. They remain recoverable from git history for anyone who wants to compare the original detection algorithms against marvin's reference detector.
