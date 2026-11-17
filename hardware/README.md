# hardware/

Physical-build artifacts for the guitar-playing robot — PCBs, enclosures, and actuator design.

| Folder | What's there |
|---|---|
| [`Sensor-LCD5/`](Sensor-LCD5/) | KiCad project + Rev A gerbers for the fretboard sensor board: 5 TEPT5700 phototransistors with MCP6002 op-amp stages, mating to a PIC32CM PL10 Curiosity Nano header. Sensing only — no actuator outputs, no LAN8651 on board. |
| [`t1s-sink/`](t1s-sink/) | KiCad project (in progress) for a 10BASE-T1S PoDL power-sink board: coupled-inductor tap, MCP16301 buck to 5 V, MCP1804 LDOs to 3.3 V. See [`docs/t1s-podl-link.md`](../docs/t1s-podl-link.md) §6. |
| [`3d-models/`](3d-models/) | STLs for the printed enclosures and mounts (marvin, fretboard, fauxmote, lemmy, lightshow, beatbox) plus the OpenSCAD source and dimensions for the voice-coil actuator parts. |
| [`actuators/`](actuators/) | Design + test-protocol docs for three candidate physical fret/strum actuators. |
| [`kicad/`](kicad/) | Shared KiCad symbol and footprint libraries used by the boards above. |

## Actuator candidates

No physical actuator is built. The robot plays through **fauxmote** (an ESP32 emulating a Wiimote +
guitar over Bluetooth), and the `guitar` node drives open-drain GPIOs or, on the current ATE_2026
board, status LEDs. The candidates below are kept for a future button-pressing rig; none has been
chosen.

| Type | Pros | Tradeoffs |
|---|---|---|
| **Voice coil** ([design](actuators/voice-coil-design.md), [test](actuators/voice-coil-test-protocol.md)) | Quietest, fastest, supports proportional control | Most complex to build; custom-wound coil + magnet |
| **Electromagnet** ([design](actuators/electromagnet-design.md), [test](actuators/electromagnet-test-protocol.md)) | Very quiet, simple construction, no moving driver coil | Lower force per watt than a solenoid |
| **DIY solenoid** ([design](actuators/diy-solenoid-design.md)) | Well-understood, predictable behavior | Loudest of the three |

The test protocols measure response time, force, and acoustic noise so the candidates can be
compared before a pick.

## How this folder relates to firmware

The actuator docs describe physical drive characteristics (force, response time, current draw), not
the command path:

- The GPIO side of actuation (pin map, drive polarity) is in [`firmware/guitar/SPEC.md`](../firmware/guitar/SPEC.md).
- A rig's mechanical rise time is declared to marvin as `FRETBOARD_ACTUATOR_ADVANCE_MS`; chord and
  strum scheduling is in [`firmware/marvin/docs/spec.md`](../firmware/marvin/docs/spec.md).
