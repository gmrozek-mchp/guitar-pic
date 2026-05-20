# hardware/

Physical-build artifacts for the guitar-playing robot — PCB, mechanical, and actuator design.

| Folder | What's there |
|---|---|
| [`actuators/`](actuators/) | Design + test-protocol docs for three candidate fret/strum actuator types. The actuator choice is intentionally still open. |
| [`3d-models/`](3d-models/) | OpenSCAD sources and STLs for printed actuator parts. |
| [`Sensor-LCD5/`](Sensor-LCD5/) | KiCad project for the fretboard sensor/actuator PCB (5 phototransistors + open-drain GPIO outputs to a Wii guitar controller). |
| [`kicad/`](kicad/) | Shared KiCad symbol and footprint libraries used by `Sensor-LCD5/` and any future boards. |

## Actuator candidates

Three options are documented under `actuators/`. Each has a design doc and a test-protocol doc.

| Type | Pros | Tradeoffs |
|---|---|---|
| **Voice coil** ([design](actuators/voice-coil-design.md), [test](actuators/voice-coil-test-protocol.md)) | Quietest, fastest, supports proportional control | Most complex to build; custom-wound coil + magnet |
| **Electromagnet** ([design](actuators/electromagnet-design.md), [test](actuators/electromagnet-test-protocol.md)) | Very quiet, simple construction, no moving driver coil | Lower force per watt than a solenoid |
| **DIY solenoid** ([design](actuators/diy-solenoid-design.md)) | Well-understood, predictable behavior | Loudest of the three |

Test protocols measure response time, force, and acoustic noise so the candidates can be compared apples-to-apples before a final pick.

## How this folder relates to firmware

The actuator design docs describe physical drive characteristics (force, response time, current draw). They do not describe the software command path — that lives in the firmware specs:

- The actuator's GPIO-side interface (open-drain pin assignment, pulse durations) is defined by [`firmware/fretboard/SPEC.md`](../firmware/fretboard/SPEC.md).
- The chord and strum scheduling that produces those pulses is described by [`firmware/marvin/docs/spec.md`](../firmware/marvin/docs/spec.md) §4.4.
