# Guitar Hero Bot

A robot that plays Guitar Hero III on a real Nintendo Wii, built on Microchip silicon. It
watches the game's video, detects notes in real time, navigates the game menus on its own, and
plays the song. A 10BASE-T1S network of Microchip MCUs adds a neural-net detector, a
head-banging puppet and a light show, all in time to the music.

- **marvin** (SAM9X75 MPU) is the brain. It captures the Wii's HDMI video, finds notes with
  computer vision, times the strums, reads the menus, and runs the 10.1″ touch UI.
- **10BASE-T1S** (LAN8651 at every node) is the backbone. One twisted pair links marvin, the
  PLCA coordinator, to six followers. The bus carries commands, peer-to-peer and broadcast
  traffic, and per-node health:
  - **fretboard** (PIC32CM PL10): phototransistors plus an on-MCU int8 neural net
  - **beatbox** (dsPIC33AK): audio FFT beat detection
  - **lemmy** (PIC32CM PL10): servo puppet
  - **lightshow** (PIC32CM PL10): WS2812 LEDs
  - **guitar** (PIC32CM PL10): button-state indicator
  - **fauxmote**: the Bluetooth bridge that carries button presses to the Wii

| Want to… | Read |
|---|---|
| Set up the booth demo | [SETUP.md](SETUP.md) |
| Demo it to visitors | [DEMO.md](DEMO.md) |
| Understand the whole system | [SPEC.md](SPEC.md) |
| Work on a subproject | its spec + journal, indexed in [SPEC.md §3](SPEC.md#3-subprojects) |

## Repository layout

```
guitar-pic/
├── README.md  SPEC.md  SETUP.md  DEMO.md  CLAUDE.md
├── media/          # photos + video for SETUP/DEMO
├── firmware/
│   ├── marvin/     # SAM9X75 host: capture, CV, timing, game control, UI   (XC32)
│   ├── fauxmote/   # ESP32 Wiimote + guitar emulator                         (ESP-IDF)
│   ├── guitar/     # PIC32CM PL10 indicator node (mirrors the button mask)   (XC32)
│   ├── fretboard/  # PIC32CM PL10 phototransistor + on-MCU neural-net detector (XC32)
│   ├── beatbox/    # dsPIC33AK audio beat detector, broadcasts beat frames   (XC-DSC)
│   ├── lemmy/      # PIC32CM PL10 head-banging puppet, 2 servos              (XC32)
│   └── lightshow/  # PIC32CM PL10 WS2812 light show                          (XC32)
├── tools/          # host-side Python: marvin-perf viewer, gameplay prototype, edge-ai, …
├── hardware/       # KiCad boards, 3D-printed enclosures, actuator design studies
└── docs/           # cross-node docs (T1S link, marvin↔fauxmote protocol), upstream bug reports, archive
```

## Status

Plays GH3 end to end with no PC in the loop. marvin starts a song from the main menu, plays
it with CV, and records the score. It also runs a 2-player **SHOWDOWN** of the robot against
a human. All seven bus nodes are on T1S. Not built yet: the SD reference-data recorder, the
calibration UI, and PoDL (power over the data pair). Per-milestone detail is in
[SPEC.md §6](SPEC.md#6-status).

## License & acknowledgments

MIT License. Hobby / educational project. Guitar Hero is a trademark of Activision; Wii is a
trademark of Nintendo.
