# Guitar Hero Bot

An autonomous robot that plays Guitar Hero / Rock Band on a Nintendo Wii by watching the game video, detecting notes in real time, and pressing the buttons on a real Wii guitar controller.

The system is built around a SAM9X75 host (**marvin**) that captures HDMI directly off the Wii, runs reference computer-vision note detection, schedules chord and strum timing, and sends fret/strum commands over UART to a small actuator MCU (**fretboard**) that drives the controller's buttons via open-drain GPIO. A separate Python tool (**fret-tuner**) is used at the bench for calibration and detector tuning, but is not in the runtime path.

For the full system overview see [SPEC.md](SPEC.md).

## Where to start

| If you want to… | Read |
|---|---|
| Understand the whole system | [SPEC.md](SPEC.md) |
| Work on the SAM9X75 host (capture, CV, timing, UI) | [firmware/marvin/docs/spec.md](firmware/marvin/docs/spec.md) + [journal](firmware/marvin/docs/journal.md) |
| Work on the actuator MCU (sensors, GPIO output) | [firmware/fretboard/SPEC.md](firmware/fretboard/SPEC.md) |
| Tune detection algorithms at the bench | [tools/fret-tuner/SPEC.md](tools/fret-tuner/SPEC.md) |
| Work on actuator mechanical / PCB / 3D-print parts | [hardware/](hardware/) |

## Repository layout

```
guitar-pic/
├── README.md            # this file
├── SPEC.md              # system spec
├── CLAUDE.md            # workflow rules for Claude Code sessions
├── firmware/
│   ├── marvin/          # SAM9X75 host firmware
│   ├── fretboard/       # PIC32CM6408 sensor/actuator MCU
│   └── sam9x75_curiosity_emirror/   # Microchip reference (template only)
├── tools/fret-tuner/    # Python dev/calibration tool
├── hardware/            # PCB, mechanical, actuator design
└── docs/
    ├── SAM9X7-Series-Data-Sheet-DS60001813.pdf
    └── archive/         # historical docs from the original architecture
```

## Status

Working today: HDMI capture and live display through marvin (480p60 and 720p60), and standalone play on fretboard with its own light sensors and chord scheduling. In progress: marvin reference detector, marvin↔fretboard link, end-to-end play through marvin, and SD-card recording of reference data. Phasing detail in [SPEC.md §7](SPEC.md).

## License & acknowledgments

MIT License. Hobby / educational project. Guitar Hero and Rock Band are trademarks of Activision and Harmonix; Wii is a trademark of Nintendo.
