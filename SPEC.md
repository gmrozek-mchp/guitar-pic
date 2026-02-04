# Guitar Hero Bot - Project Specification

## Overview

An autonomous system that plays Guitar Hero on PlayStation 2 or Nintendo Wii by observing the game screen via video capture, detecting note patterns in real-time using computer vision, and mechanically actuating a physical guitar controller using solenoids and servos controlled by a Microchip PIC microcontroller.

## Goals

- **Primary**: Build a working system that can complete Guitar Hero songs automatically
- **Initial Target**: Easy difficulty with basic note hitting
- **Ultimate Target**: Expert difficulty with full features (notes, star power, whammy)

---

## Platform Options

### PlayStation 2 (Original Choice)
- Widely available, cheap hardware
- Component video output (480p)
- Wired guitar controller (simple)

### Nintendo Wii (Alternative)
- Better video output options (component 480p or HDMI adapter)
- Larger game library (GH3, World Tour, GH5, Warriors of Rock)
- Guitar connects wirelessly to Wiimote

**Current Selection**: PS2 (may migrate to Wii)

---

## System Architecture

```
┌─────────────────┐     ┌─────────────────────────────────────────┐
│  PS2 or Wii     │     │            Vision System                │
│    Console      │     │  ┌─────────────┐    ┌───────────────┐   │
│  Guitar Hero    │────▶│  │ Video       │───▶│ PC/SBC        │   │
│  Game           │     │  │ Capture     │    │ (OpenCV)      │   │
│                 │     │  └─────────────┘    └───────┬───────┘   │
└────────▲────────┘     └─────────────────────────────┼───────────┘
         │                                            │
         │                                            │ Serial/USB
         │                                            ▼
         │              ┌─────────────────────────────────────────┐
         │              │           Control System                │
         │              │  ┌─────────────┐    ┌───────────────┐   │
         │              │  │ PIC         │───▶│ Motor         │   │
         │              │  │ Micro       │    │ Drivers       │   │
         │              │  └─────────────┘    └───────┬───────┘   │
         │              └─────────────────────────────┼───────────┘
         │                                            │
         │                                            ▼
         │              ┌─────────────────────────────────────────┐
         │              │        Mechanical Actuators             │
         │              │  ┌────────┐ ┌────────┐ ┌────────┐       │
         │              │  │Solenoid│ │Strum   │ │Whammy  │       │
         │              │  │x5 Frets│ │Actuator│ │Servo   │       │
         │              │  └────┬───┘ └────┬───┘ └────┬───┘       │
         │              └──────┼──────────┼──────────┼────────────┘
         │                     │          │          │
         │                     ▼          ▼          ▼
         │              ┌─────────────────────────────────────────┐
         │              │     PS2 or Wii Guitar Controller        │
         └──────────────│  (Physical buttons actuated by machine) │
                        └─────────────────────────────────────────┘
```

---

## Hardware Components

### 1. Video Capture

Capture the console's video output for computer vision processing.

**Selected Device: Elgato Game Capture HD**

| Spec | Value |
|------|-------|
| Model | Elgato Game Capture HD |
| Input | Component video (Y/Pb/Pr) |
| Connection | USB 2.0 |
| Resolution | 480p |
| Capture Latency | ~50-70ms |
| Cost | $50-80 (used) |

**Connection (PS2):**
```
PS2 ──[Component Cables]──► Elgato Game Capture HD ──[USB 2.0]──► PC
      (5 RCA: Y/Pb/Pr +         (external power
       L/R audio)                adapter required)
```

**Connection (Wii):**
```
Wii ──[Component Cables]──► Elgato Game Capture HD ──[USB 2.0]──► PC
      (5 RCA: Y/Pb/Pr +         (external power
       L/R audio)                adapter required)
```

**Required Cables:**
- PS2 Component Cable (YPbPr + Audio) - $10-15
- **OR** Wii Component Cable (YPbPr + Audio) - $10-15

**PS2 Setup:**
- Enable 480p progressive scan in Guitar Hero options if available
- Ensure component output is selected in PS2 system settings

**Wii Setup:**
- Set display mode to 480p (EDTV/HDTV) in Wii System Settings
- Wii outputs 480p progressive via component cables
- Alternative: Wii2HDMI adapter + HDMI capture (may add latency)

### 2. PIC Microcontroller

Controls all mechanical actuators with precise timing.

**Requirements**:
- Sub-millisecond timing precision for note accuracy
- Minimum 8 digital I/O pins:
  - 5 for fret solenoids (green, red, yellow, blue, orange)
  - 2 for strum bar (up, down)
  - 1 PWM for whammy servo
  - 1 for tilt mechanism (optional)
- UART or USB communication with host PC
- 3.3V or 5V logic level

**Candidate Microcontrollers**:

| Model | Features | Pros | Cons |
|-------|----------|------|------|
| PIC18F4550 | USB native, 40 pins | Direct USB, no FTDI needed | 8-bit, older architecture |
| PIC18F26K22 | 28-pin, dual UART | Small, affordable | Needs USB adapter |
| PIC24FJ64GB002 | 16-bit, USB OTG | More processing power | Higher complexity |

### 3. Mechanical Actuators

#### Fret Buttons (x5) & Strum Bar (x2)

**Selected: DIY Miniature Solenoids (Dual per Switch)**

Custom-wound solenoids optimized for low force, low noise actuation of keyboard switches.

| Spec | Value |
|------|-------|
| Type | DIY wound solenoid |
| Configuration | 2 solenoids per switch (balanced actuation) |
| Force per solenoid | 25g |
| Total force per switch | 50g (matches keyboard switch) |
| Voltage | 12V DC |
| Stroke | 3-4mm |
| Response time | <20ms |
| Coil resistance | ~6Ω |
| Wire | 24 AWG magnet wire, ~230 turns |

**Detailed design:** See [docs/diy-solenoid-design.md](docs/diy-solenoid-design.md)

**Dual Solenoid Layout:**

```
      Solenoid A              Switch              Solenoid B
          │                     │                     │
     ┌────┴────┐           ┌────┴────┐           ┌────┴────┐
     │ [coil]  │           │  stem   │           │ [coil]  │
     └────┬────┘           └────┬────┘           └────┬────┘
          │                     │                     │
          └─────────────────────┼─────────────────────┘
                                ▼
                    25g + 25g = 50g balanced force
```

**Advantages of DIY approach:**
- Lower force = quieter operation
- Dual solenoids = even actuation pressure
- Custom sized for keyboard switches
- Lower cost (~$30 for all 14 solenoids)
- Full control over noise dampening

**Noise Reduction:**
- Soft silicone plunger stops
- Rubber grommet mounting isolation
- Optional foam acoustic enclosure

---

#### Alternative: Electromagnet + Neodymium Magnet Actuation

An experimental approach using electromagnets to attract permanent magnets on the keys.

**Concept:**
```
         Key with neo magnet          
         ┌─────────────────┐
         │    ┌───┐        │
         │    │N S│ ← 4mm×2mm neo magnet glued to key
         │    └─┬─┘        │
         └──────┼──────────┘
                │ 4mm air gap (unpressed)
    ════════════╪═══════════════════
         ┌──────┴──────┐
         │ Electromagnet│ ← Energize to pull key down
         │    (coil)    │
         └─────────────┘
```

| Spec | Value |
|------|-------|
| Type | Electromagnet pulling neo magnet |
| Neo magnet | 4mm×2mm N42 disc |
| Coil | 26 AWG, ~200 turns, 5mm iron core |
| Air gap | 4mm (unpressed) to 1mm (pressed) |
| Operating voltage | 12V |
| Current | ~2.4A |
| Estimated force at 4mm | 25-45g (needs testing) |

**Advantages:**
- **Quietest option** - No moving plunger, no impact sound
- Near-silent actuation
- Simple construction
- Lower cost (~$18 for all 7)

**Risks:**
- Force at 4mm gap may be marginal
- Requires testing before committing

**Documentation:**
- Design details: [docs/electromagnet-design.md](docs/electromagnet-design.md)
- Test protocol: [docs/electromagnet-test-protocol.md](docs/electromagnet-test-protocol.md)

**Recommendation:** Build test rig (~$15-20) to validate force before full build.

---

#### Alternative: DIY Voice Coil Actuator

A custom voice coil that fits around a standard keyboard switch, using Lorentz force for the quietest possible actuation.

**Concept:**
```
         ┌─────────────────┐
         │  Custom keycap  │
         │  ┌───────────┐  │
         │  │░░ Coil ░░░│  │ ← Voice coil (moves with keycap)
         │  └─────┬─────┘  │
         └────────┼────────┘
                  │
    ══════════════╪═══════════════ Mounting plate
           ┌──────┴──────┐
           │ N ║ gap ║ N │ ← Ring magnets (fixed)
           │ S ║     ║ S │
           │ ════════════ │ ← Steel yoke
           └──────┬───────┘
           ┌──────┴──────┐
           │   Switch    │ ← Standard Cherry MX
           └─────────────┘
```

| Spec | Value |
|------|-------|
| Type | Voice coil (Lorentz force) |
| Magnets | 4× N42 10×5×3mm blocks |
| Coil | 30 AWG, 60 turns, 4Ω |
| Gap | 3.5mm |
| Operating voltage | 5V |
| Current for 50g | ~0.5A |
| Response time | <5ms |
| Estimated cost | ~$33 for all 7 |

**Advantages:**
- **Absolutely quietest** - No snap, no impact, smooth proportional force
- **Fastest response** - Low moving mass (<5ms)
- **Proportional control** - Force linear with current
- **Bidirectional** - Can push or pull
- **Highest cool factor** - Speaker technology for buttons

**Risks:**
- Most complex construction
- Requires precise magnet alignment
- Higher assembly time

**Documentation:**
- Design details: [docs/voice-coil-design.md](docs/voice-coil-design.md)
- Test protocol: [docs/voice-coil-test-protocol.md](docs/voice-coil-test-protocol.md)

**Recommendation:** Build one test unit (~$17) to validate before full build.

---

#### Actuator Comparison Summary

| Factor | DIY Solenoid | Electromagnet | Voice Coil |
|--------|--------------|---------------|------------|
| Noise | Soft thud | Very quiet | **Quietest** |
| Response | 15-20ms | 15-20ms | **<5ms** |
| Force control | On/off | On/off | **Proportional** |
| Complexity | Medium | Medium | Higher |
| Cost (×7) | ~$38 | ~$26 | ~$33 |
| Test cost | N/A | ~$20 | ~$17 |
| Cool factor | Medium | High | **Highest** |

**Recommended test order:**
1. Voice coil (quietest, if it works)
2. Electromagnet (simpler, still quiet)
3. DIY solenoid (proven, louder)

---

#### Strum Bar

**Selected: Dual JF-0530B 12V Solenoids (dampened)**

Same solenoid model as fret buttons, one for each direction:
- Strum Down: Push solenoid actuates downward strum
- Strum Up: Push solenoid actuates upward strum

**Requirements**:
- Must support alternating strums at 10+ Hz for Expert difficulty
- Same dampening treatment as fret solenoids
- Quick return via spring (built into push-pull design)

#### Whammy Bar

Oscillating motion during sustained notes.

- **Type**: Standard hobby servo (SG90 or MG90S)
- **Range**: 45-90 degrees
- **Control**: PWM from PIC (50Hz, 1-2ms pulse)
- **Speed**: Variable oscillation 1-5 Hz
- **Noise**: Quiet (servo whine only)

#### Star Power Tilt

Activate star power by tilting the guitar.

**Options**:
1. **Physical tilt**: Servo/motor tilts entire guitar ~30 degrees
2. **Sensor bypass**: Inject signal to controller's accelerometer circuit

**Recommendation**: Physical tilt for Phase 1, sensor bypass for optimization.

---

## Wii Guitar Controller Reference

If migrating to Wii platform, the guitar controller connects as a Wiimote extension.

### Controller Variants

| Model | Notes |
|-------|-------|
| GH3 Gibson Les Paul | Original, encrypted by default |
| GH World Tour | Touchbar on neck, different init |
| Third-party (Nyko, etc.) | Requires new initialization method |

### Hardware Interface

The Wii guitar connects to the Wiimote's extension port (same connector as Nunchuk/Classic Controller).

**Identification bytes** at register `0x(4)a400fa`:
```
00 00 A4 20 01 03
│
└── 00 = Guitar (01 = Drums)
```

### Data Format

6 bytes reported at address `0xa40008`:

| Byte | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
|------|-------|-------|-------|-------|-------|-------|-------|-------|
| 0 | - | - | SX (analog stick X) | | | | | |
| 1 | - | - | SY (analog stick Y) | | | | | |
| 2 | 0 | 0 | 0 | TB (touchbar) | | | | |
| 3 | 0 | 0 | 0 | WB (whammy) | | | | |
| 4 | 1 | **BD** | 1 | **B-** | 1 | **B+** | 1 | 1 |
| 5 | **BO** | **BR** | **BB** | **BG** | **BY** | **PB** | 1 | **BU** |

**Button Mapping:**
- **BG, BR, BY, BB, BO** - Fret buttons (Green, Red, Yellow, Blue, Orange)
- **BU, BD** - Strum bar (Up, Down)
- **WB** - Whammy bar (analog, 5-bit)
- **B+, B-** - Plus/Minus buttons (B- is Star Power on GHWT)
- **PB** - Pedal button (RJ11 expansion port)
- **TB** - Touchbar (GHWT only, analog)
- **SX, SY** - Analog stick

**Note:** Buttons are active LOW (0 = pressed, 1 = released)

### Touchbar Values (GHWT Only)

```
Not touching  - 0x0F
1st fret      - 0x04
1st + 2nd     - 0x07
2nd fret      - 0x0A
2nd + 3rd     - 0x0C/0D
3rd fret      - 0x12/13
3rd + 4th     - 0x14/15
4th fret      - 0x17/18
4th + 5th     - 0x1A
5th fret      - 0x1F
```

### Initialization

**GH3 Guitars (encrypted):**
```
Write 0x00 to 0x(4)a40040
```

**GHWT / Third-party (unencrypted):**
```
Write 0x55 to 0x(4)a400f0
Write 0x00 to 0x(4)a400fb
```

### Potential Direct Interface Option

Instead of mechanical actuation, the Wii guitar's extension connector could potentially be intercepted or emulated:

1. **Passive sniffing**: Monitor I²C traffic between guitar and Wiimote
2. **Man-in-the-middle**: Intercept and modify button states
3. **Full emulation**: PIC emulates guitar extension protocol to Wiimote

**Pros:** No mechanical noise, instant response, simpler hardware
**Cons:** Defeats the "cool factor" goal, not physically pressing buttons

**Recommendation:** Stick with mechanical actuation for primary build; direct interface as optional Phase 5 experiment.

**Reference:** [WiiBrew - Guitar Hero Wii Guitars](https://wiibrew.org/wiki/Wiimote/Extension_Controllers/Guitar_Hero_(Wii)_Guitars)

### 4. Motor Drivers

Solenoids require more current than PIC pins can supply.

**Options**:
- Individual MOSFETs (IRLZ44N, logic-level)
- Driver ICs (ULN2803 for 8 channels)
- Motor driver boards (L298N, TB6612)

**Requirements**:
- Handle inductive load kickback (flyback diodes)
- Fast switching for rapid note sequences

### 5. Power Supply

| Component | Voltage | Current (per unit) | Total |
|-----------|---------|-------------------|-------|
| Solenoids x5 | 12V | 500mA | 2.5A peak |
| Strum solenoids x2 | 12V | 500mA | 1A peak |
| Servos x2 | 5V | 500mA | 1A peak |
| PIC + logic | 5V | 100mA | 100mA |

**Recommendation**: 12V 5A power supply with 5V regulator for logic/servos.

---

## Software Components

### 1. Vision System (Python + OpenCV)

Runs on host PC or Raspberry Pi.

#### Video Capture Module (`capture.py`)
- Initialize video capture device
- Handle frame rate and resolution settings
- Provide frame buffer for processing

#### Note Detection Module (`detector.py`)
- **Highway detection**: Locate the 5 note lanes on screen
- **Note detection**: Identify colored gems (green, red, yellow, blue, orange)
- **Position tracking**: Calculate distance from strike line
- **Chord detection**: Identify multi-note chords
- **Sustain detection**: Recognize held notes (long tails)
- **Star Power detection**: Identify glowing star power phrases
- **Meter reading**: Track star power meter fill level

#### Timing Scheduler (`scheduler.py`)
- Maintain queue of upcoming notes
- Calculate hit times based on:
  - Note position on screen
  - Scroll speed (may vary by song/difficulty)
  - System latency compensation
- Send commands to PIC at appropriate times

### 2. PIC Firmware (C)

#### Main Loop (`main.c`)
- Initialize peripherals
- Process incoming serial commands
- Dispatch to actuator control

#### Serial Communication (`serial.c`)
- UART initialization and interrupt handling
- Command parsing and validation
- Response/acknowledgment

#### Actuator Control (`actuators.c`)
- Solenoid on/off control
- Servo PWM generation
- Timing precision using hardware timers

#### Timing Module (`timing.c`)
- High-resolution timer setup
- Scheduled action execution
- Latency measurement utilities

---

## Communication Protocol

### Serial Configuration
- **Baud rate**: 115200 (or 230400 for lower latency)
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Flow control**: None (or RTS/CTS if needed)

### Command Format

```
[START][CMD][PAYLOAD][CHECKSUM][END]
```

| Field | Size | Description |
|-------|------|-------------|
| START | 1 byte | 0xAA (start marker) |
| CMD | 1 byte | Command type |
| PAYLOAD | 1-4 bytes | Command-specific data |
| CHECKSUM | 1 byte | XOR of CMD + PAYLOAD |
| END | 1 byte | 0x55 (end marker) |

### Commands

| CMD | Name | Payload | Description |
|-----|------|---------|-------------|
| 0x01 | NOTE | [frets][strum][duration_ms] | Press frets, strum, hold for duration |
| 0x02 | FRET_DOWN | [frets] | Press fret buttons (bitmask) |
| 0x03 | FRET_UP | [frets] | Release fret buttons (bitmask) |
| 0x04 | STRUM | [direction] | Strum (0=down, 1=up) |
| 0x05 | WHAMMY | [position] | Set whammy position (0-255) |
| 0x06 | WHAMMY_OSC | [speed][amplitude] | Oscillate whammy |
| 0x07 | TILT | [state] | Activate tilt (0=off, 1=on) |
| 0x10 | PING | none | Connection test |
| 0x11 | CALIBRATE | [mode] | Enter calibration mode |

### Fret Bitmask

```
Bit 0: Green  (0x01)
Bit 1: Red    (0x02)
Bit 2: Yellow (0x04)
Bit 3: Blue   (0x08)
Bit 4: Orange (0x10)
```

Example: Green + Red chord = 0x03

---

## Timing Considerations

### Latency Budget

Total system latency from note appearance to button press:

| Stage | Typical Latency | Notes |
|-------|-----------------|-------|
| Video capture (Elgato HD) | 50-70ms | Component via USB 2.0 |
| Frame processing | 10-30ms | OpenCV detection |
| Serial TX | 1-5ms | UART transmission |
| PIC processing | <1ms | Command parsing |
| Solenoid actuation | 10-20ms | Mechanical response |
| **Total** | **70-125ms** | Must be compensated |

### Compensation Strategy

1. **Measure total latency** using calibration routine
2. **Predict note timing** based on position and scroll speed
3. **Pre-send commands** to arrive at PIC before needed
4. **PIC schedules execution** at precise time using hardware timer

### Guitar Hero Timing Windows

| Rating | Window |
|--------|--------|
| Perfect | ±20ms |
| Good | ±50ms |
| OK | ±100ms |
| Miss | >100ms |

**Target**: Achieve "Good" or better on 95%+ of notes.

---

## Development Phases

### Phase 1: Foundation (Proof of Concept)

**Goals**:
- Capture PS2 video successfully
- Detect single notes (one color)
- Actuate one solenoid via PIC
- Hit a few notes in "Tutorial" or easiest song

**Deliverables**:
- [ ] Video capture working, frames accessible in Python
- [ ] Basic color detection for green notes
- [ ] PIC firmware responds to serial commands
- [ ] Single solenoid fires on command
- [ ] End-to-end test: detect note → press button

### Phase 2: Core Gameplay

**Goals**:
- Full 5-fret detection and actuation
- Chord recognition
- Strum bar working
- Play through Easy songs

**Deliverables**:
- [ ] All 5 note colors detected reliably
- [ ] Chord detection working
- [ ] 5 fret solenoids + strum bar installed
- [ ] Latency calibration implemented
- [ ] Complete "Slow Ride" on Easy with 80%+ accuracy

### Phase 3: Advanced Features

**Goals**:
- Sustain notes with whammy
- Star Power detection and activation
- Medium/Hard difficulty support

**Deliverables**:
- [ ] Sustain note detection
- [ ] Whammy servo oscillating on sustains
- [ ] Star Power phrase detection
- [ ] Tilt mechanism activates star power
- [ ] Complete songs on Hard difficulty

### Phase 4: Expert Mode

**Goals**:
- Handle fastest Expert charts
- Alternating strum patterns
- Optimized latency

**Deliverables**:
- [ ] Alt-strumming logic for fast runs
- [ ] Sub-frame timing optimization
- [ ] Complete "Through the Fire and Flames" on Expert
- [ ] 95%+ accuracy on Expert songs

---

## Testing Strategy

### Unit Tests

**Vision**:
- Test note detection with static screenshot images
- Verify color classification accuracy
- Test chord detection logic

**Firmware**:
- Verify serial command parsing
- Test timer accuracy
- Validate PWM output for servos

### Integration Tests

- Capture → Detection → Serial output pipeline
- Serial input → Actuator response timing
- Full loop with recorded gameplay video

### Hardware Tests

- Solenoid response time measurement
- Button press force verification
- Strum rate maximum testing
- Power consumption monitoring

---

## Bill of Materials (Preliminary)

| Category | Item | Quantity | Est. Cost |
|----------|------|----------|-----------|
| **Capture** | USB composite capture | 1 | $15 |
| **MCU** | PIC18F4550 or dev board | 1 | $10-30 |
| **Actuators** | Push solenoids (5V/12V) | 7 | $35 |
| **Actuators** | Servo SG90 | 2 | $6 |
| **Drivers** | ULN2803 or MOSFETs | 1 | $5 |
| **Power** | 12V 5A supply | 1 | $15 |
| **Power** | 5V regulator | 1 | $3 |
| **Misc** | Wires, connectors, PCB | - | $20 |
| **Mounting** | 3D printed brackets | - | $10 |
| **Controller** | PS2 Guitar (used) | 1 | $20-40 |
| | *OR* Wii Guitar + Wiimote | 1+1 | $30-60 |
| | | **Total** | **~$140-200** |

---

## Open Questions

1. **Game Platform**: PS2 vs Wii? (Wii has better video, more games, but wireless guitar adds complexity)
2. **PIC Model Selection**: PIC18F4550 (USB) vs PIC18F26K22 (simpler)?
3. **Vision Platform**: Full PC vs Raspberry Pi 4?
4. **Solenoid Voltage**: 5V (simpler power) vs 12V (faster response)?
5. **Mounting System**: 3D printed custom vs adjustable clamps?
6. **Latency Calibration**: Visual method vs audio sync?

---

## References

- Guitar Hero PS2 game mechanics
- PIC18F4550 datasheet
- OpenCV Python documentation
- Solenoid selection guides
- PS2 controller protocol (for potential direct interface)
- [WiiBrew - Guitar Hero Wii Guitars](https://wiibrew.org/wiki/Wiimote/Extension_Controllers/Guitar_Hero_(Wii)_Guitars) - Wii guitar extension protocol

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 0.1 | 2026-01-28 | Initial specification |
