# Guitar Hero Bot

An autonomous system that plays Guitar Hero on PlayStation 2 or Nintendo Wii by observing the game screen and mechanically actuating a real guitar controller.

## Project Overview

This project combines computer vision, real-time processing, and embedded systems to create a machine that can play Guitar Hero automatically:

1. **Video Capture**: Captures console video output via USB capture device (Elgato Game Capture HD)
2. **Vision Processing**: Detects notes, chords, and game state using OpenCV
3. **Timing Scheduler**: Calculates when to press buttons accounting for system latency
4. **Microcontroller**: PIC microcontroller controls actuators with precise timing
5. **Mechanical Actuation**: Actuators press fret buttons and strum bar; servos control whammy and tilt

## Repository Structure

```
guitar-pic/
├── SPEC.md                 # Detailed project specification
├── README.md               # This file
├── docs/
│   ├── hardware-bom.md     # Bill of materials
│   ├── wiring-diagram.md   # Electrical connections
│   ├── calibration.md      # Timing calibration guide
│   ├── diy-solenoid-design.md      # DIY solenoid actuator design
│   ├── electromagnet-design.md     # Electromagnet actuator design
│   ├── electromagnet-test-protocol.md
│   ├── voice-coil-design.md        # Voice coil actuator design
│   └── voice-coil-test-protocol.md
├── hardware/
│   └── 3d-models/          # OpenSCAD files for 3D printed parts
│       ├── voice-coil-parts.scad
│       └── voice-coil-parts-dimensions.md
├── vision/                 # Python vision system
│   ├── requirements.txt    # Python dependencies
│   ├── main.py            # Main entry point
│   ├── capture.py         # Video capture module
│   ├── detector.py        # Note detection
│   ├── scheduler.py       # Timing and scheduling
│   └── serial_comm.py     # MCU communication
└── tests/                 # Unit tests
    ├── test_detection.py  # Vision tests
    └── test_serial.py     # Protocol tests
```

## Quick Start

### Prerequisites

- Python 3.8+
- OpenCV (`pip install opencv-python`)
- pyserial (`pip install pyserial`)
- USB video capture device (Elgato Game Capture HD recommended)
- PIC microcontroller + programmer (TBD)

### Vision System Setup

```bash
cd vision
pip install -r requirements.txt

# List available devices
python main.py --list-devices

# Run with debug display
python main.py -d -v 0

# Calibration mode
python main.py -c
```

## Hardware Requirements

| Component | Purpose | Quantity |
|-----------|---------|----------|
| PIC microcontroller | Main controller (TBD) | 1 |
| Actuators | Fret buttons (see actuator docs) | 5-7 |
| Actuators | Strum bar | 2 |
| Servo motors | Whammy + Tilt | 2 |
| Driver ICs | Actuator driver | 1+ |
| 12V 5A PSU | Power supply | 1 |
| Elgato Game Capture HD | Video input | 1 |
| PS2 or Wii Guitar | Controller | 1 |

See `docs/hardware-bom.md` for complete list.

## Actuator Options

Three actuator designs are documented for testing:

1. **Voice Coil** - Quietest, fastest, proportional control
2. **Electromagnet** - Very quiet, simple construction  
3. **DIY Solenoid** - Proven technology, louder

See `docs/voice-coil-design.md`, `docs/electromagnet-design.md`, `docs/diy-solenoid-design.md`.

## Development Phases

- [ ] **Phase 1**: Basic note detection + single solenoid test
- [ ] **Phase 2**: Full 5-fret control + Easy difficulty
- [ ] **Phase 3**: Star power + whammy + Medium/Hard
- [ ] **Phase 4**: Expert difficulty optimization

## Testing

```bash
cd tests
pytest -v
```

## Documentation

- [SPEC.md](SPEC.md) - Full project specification
- [docs/hardware-bom.md](docs/hardware-bom.md) - Parts list
- [docs/wiring-diagram.md](docs/wiring-diagram.md) - Wiring guide
- [docs/calibration.md](docs/calibration.md) - Timing calibration
- [docs/voice-coil-design.md](docs/voice-coil-design.md) - Voice coil actuator design
- [docs/electromagnet-design.md](docs/electromagnet-design.md) - Electromagnet actuator design
- [docs/diy-solenoid-design.md](docs/diy-solenoid-design.md) - DIY solenoid design

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- Guitar Hero is a trademark of Activision
- Wii and Wiimote are trademarks of Nintendo
- This is a hobby/educational project for learning computer vision and embedded systems
