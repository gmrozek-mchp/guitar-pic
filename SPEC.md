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

### Option A: Video Capture (PC-based)

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

### Option B: Light Sensor Detection (Standalone PIC)

```
┌─────────────────┐
│  PS2 or Wii     │
│    Console      │
│  Guitar Hero    │────▶  TV/Monitor
│  Game           │           │
│                 │           │ (light from screen)
└────────▲────────┘           ▼
         │              ┌─────────────────────────────────────────┐
         │              │     Light Sensor Array (on screen)      │
         │              │  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐    │
         │              │  │ PT │ │ PT │ │ PT │ │ PT │ │ PT │    │
         │              │  │ G  │ │ R  │ │ Y  │ │ B  │ │ O  │    │
         │              │  └──┬─┘ └──┬─┘ └──┬─┘ └──┬─┘ └──┬─┘    │
         │              └─────┼──────┼──────┼──────┼──────┼───────┘
         │                    │      │      │      │      │
         │                    └──────┴──────┴──────┴──────┘
         │                                  │
         │                                  │ Analog (5 wires)
         │                                  ▼
         │              ┌─────────────────────────────────────────┐
         │              │           Control System                │
         │              │  ┌─────────────┐    ┌───────────────┐   │
         │              │  │ PIC ADC     │───▶│ Motor         │   │
         │              │  │ + Logic     │    │ Drivers       │   │
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

**Architecture Comparison:**

| Aspect | Option A: Video Capture | Option B: Light Sensors |
|--------|-------------------------|-------------------------|
| Detection latency | 50-70ms | <1ms |
| Requires PC | Yes | No |
| Complexity | High (OpenCV, threading) | Low (ADC thresholds) |
| Cost | ~$150-200 total | ~$100-140 total |
| Star power detection | Yes (CV analysis) | Limited (brightness only) |
| Portability | Tethered to PC | Fully standalone |

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

---

#### Alternative: Light Sensor Detection

An experimental approach using low-resolution light sensors mounted directly on the screen to detect notes, eliminating video capture entirely.

**Concept:**
```
Screen (Guitar Hero highway)
┌────────────────────────────────────────────────────────┐
│                                                        │
│     ●        ●        ●        ●        ●             │ ← Notes scroll down
│     │        │        │        │        │             │
│  [Green] [Red]  [Yellow][Blue] [Orange]               │
│     │        │        │        │        │             │
│     ▼        ▼        ▼        ▼        ▼             │
│  ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐            │ ← Sensors at strike line
│  │ PT  │ │ PT  │ │ PT  │ │ PT  │ │ PT  │            │   (phototransistors)
│  └─────┘ └─────┘ └─────┘ └─────┘ └─────┘            │
└────────────────────────────────────────────────────────┘
        │        │        │        │        │
        └────────┴────────┴────────┴────────┴──► PIC ADC (5 channels)
```

**Key Insight:** Position = Color. Each lane corresponds to one fret button. Sensors don't need to detect color—they only detect brightness change. The X-position determines which button to press.

**Sensor Options:**

| Type | Part Example | Response Time | Cost | Notes |
|------|--------------|---------------|------|-------|
| Phototransistor (THT) | TEPT5700 | <15μs | $0.50 | Recommended, simple analog |
| Phototransistor (SMD) | TEMT6000X01 | <15μs | $0.45 | SMD alternative, same family |
| Ambient Light Sensor | VEML7700 | 25ms | $2 | I²C, built-in ADC |
| Photodiode | BPW34 | <1μs | $1 | Needs amplifier circuit |
| RGB Color Sensor | TCS3400 | 50ms | $3-4 | Optional, detects star power glow (replaces discontinued TCS34725) |

**Recommended: TEPT5700 Phototransistor**

| Spec | Value |
|------|-------|
| Type | NPN silicon phototransistor |
| Spectral sensitivity | 440-800nm (covers all note colors) |
| Rise/fall time | <15μs |
| Package | 5mm clear dome |
| Interface | Analog voltage (simple resistor divider) |
| Cost | ~$0.50 each |

**Circuit (per sensor):**
```
    VCC (3.3V or 5V)
         │
         ├───────────► PIC ADC pin
         │
        ┌┴┐
        │ │ 10kΩ
        └┬┘
         │
         ▼ Collector
       ┌───┐
       │PT │ TEPT5700
       └─┬─┘
         │ Emitter
         │
        GND
```

**SMD Alternative: TEMT6000X01 Phototransistor**

Surface-mount equivalent from the same Vishay family. Identical spectral characteristics and circuit topology, in a solderable 1206 package.

| Spec | TEPT5700 (THT) | TEMT6000X01 (SMD) |
|------|----------------|-------------------|
| Package | 5mm through-hole dome | 1206 SMD (4 × 2 × 1.05mm) |
| Spectral sensitivity | 440-800nm | 440-800nm |
| Peak wavelength | 570nm | 570nm |
| Half angle (FOV) | ±50° | ±60° |
| Photocurrent @ 100 lux | 75μA | 50μA |
| Dark current (max) | 50nA | 50nA |
| AEC-Q101 | No | Yes |
| Cost | ~$0.50 | ~$0.45 |

Use 15kΩ load resistor (instead of 10kΩ) to compensate for lower photocurrent and maintain equivalent ADC voltage swing.

**Circuit (per sensor, TEMT6000X01):**
```
    VCC (3.3V or 5V)
         │
         ├───────────► PIC ADC pin
         │
        ┌┴┐
        │ │ 15kΩ
        └┬┘
         │
         ▼ Collector
       ┌───┐
       │PT │ TEMT6000X01
       └─┬─┘
         │ Emitter
         │
        GND
```

**When to choose SMD:** If building a custom sensor PCB with precise lane spacing, the TEMT6000X01 enables tighter mechanical tolerances and a more compact mounting solution. For quick prototyping, the through-hole TEPT5700 is easier to work with.

**Sensor Placement Options:**

| Placement | Look-ahead | Pros | Cons |
|-----------|------------|------|------|
| At strike line | 0ms | Simplest logic, detect = press | Zero margin for error |
| 1" above strike | ~80-100ms | Time to prepare, compensate | Need scroll speed calibration |
| 2" above strike | ~150-200ms | Maximum warning | Higher calibration complexity |

**Recommended:** Place sensors ~1 inch above the strike line for ~100ms look-ahead time.

**Comparison: Video Capture vs Light Sensors**

| Factor | Video Capture | Light Sensors |
|--------|---------------|---------------|
| Detection latency | 50-70ms | <1ms |
| Processing | PC + OpenCV | PIC ADC only |
| Total system latency | 70-125ms | 15-25ms |
| Hardware cost | $50-100 | ~$10 |
| Complexity | High | Low |
| Calibration | Per-game color tuning | Per-TV brightness threshold |
| Portability | Requires PC | Standalone PIC possible |
| Sustain detection | Tail tracking in CV | Extended high reading |
| Star power detection | Color/glow analysis | Brightness spike (or RGB sensor) |

**Challenges:**

1. **Mounting precision** - Sensors must align precisely with note lanes
   - Solution: 3D-printed mounting frame sized to TV bezel
   - Alternative: Adjustable suction cup mounts with fine positioning

2. **Ambient light interference** - Room lighting affects readings
   - Solution: Light tubes/hoods over each sensor (5-10mm tubes)
   - Solution: Differential detection (compare to baseline)

3. **TV brightness variation** - Different displays have different output
   - Solution: Calibration routine at startup
   - Measure "background" and "note present" levels per lane

4. **Chord detection** - Multiple simultaneous notes
   - Not a problem: Each lane has independent sensor

5. **Sustain notes** - Long held notes with tails
   - Detect: Extended period of high brightness after initial spike
   - Release: Brightness returns to background level

**Calibration Procedure:**

1. Start song, pause immediately (highway visible, no notes at strike line)
2. Read all 5 sensors → store as `background[]`
3. Resume, let notes pass each lane
4. Track maximum reading per lane → store as `note_peak[]`
5. Set threshold = `(background + note_peak) / 2` per lane

**Detection Logic (PIC firmware):**

```c
#define NUM_LANES 5
#define DEBOUNCE_MS 20

uint16_t background[NUM_LANES];
uint16_t threshold[NUM_LANES];
uint8_t note_state[NUM_LANES];  // 0 = idle, 1 = detected

void detect_notes(void) {
    for (int lane = 0; lane < NUM_LANES; lane++) {
        uint16_t reading = read_adc(lane);
        
        if (reading > threshold[lane] && note_state[lane] == 0) {
            // Rising edge: note detected
            note_state[lane] = 1;
            press_fret(lane);
            strum();
        } else if (reading < threshold[lane] && note_state[lane] == 1) {
            // Falling edge: note passed
            note_state[lane] = 0;
            release_fret(lane);
        }
    }
}
```

**Bill of Materials (Light Sensor Approach):**

| Item | Qty | Est. Cost |
|------|-----|-----------|
| TEPT5700 (THT) *or* TEMT6000X01 (SMD) | 5 | $2.50 |
| 10kΩ (THT) or 15kΩ (SMD) resistors | 5 | $0.50 |
| Light tubes (3D printed or vinyl) | 5 | $2 |
| Mounting frame (3D printed) | 1 | $5 |
| Wiring, connectors | - | $3 |
| **Total** | | **~$13** |

**Advantages:**
- **Dramatically lower latency** - Sub-millisecond detection vs 50-70ms capture
- **No PC required** - Entire system can run on PIC alone
- **Simpler software** - Threshold comparison vs computer vision
- **Lower cost** - $13 vs $50-100 for capture card
- **More reliable** - No frame drops, buffer issues, or USB timing problems

**Risks:**
- Untested approach - requires prototype validation
- Screen positioning may be finicky
- May not detect star power phrases (unless using RGB sensor)

**Recommendation:** Build a single-lane test rig (~$5) to validate detection reliability before committing to full 5-lane build.

---

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

**Concept (Dual-Gap Configuration):**
```
                    ┌─────────────────┐
                    │  Custom keycap  │
                    │  ┌───────────┐  │
                    │  │░░ Coil ░░░│  │ ← Voice coil (moves with keycap)
                    │  └─────┬─────┘  │
                    └────────┼────────┘
                             │
       ══════════════════════╪══════════════════════ Mounting plate
                             │
    ┌────────────┐    4mm    │    4mm    ┌────────────┐
    │     N      │◄── gap ──►║◄── gap ──►│     N      │
    │ ┌────────┐ │           ║           │ ┌────────┐ │
    │ │ magnet │ │    ┌──────╨──────┐    │ │ magnet │ │
    │ └────────┘ │    │ coil passes │    │ └────────┘ │
    │     S      │    │   through   │    │     S      │
    └────────────┘    │  both gaps  │    └────────────┘
                      └──────┬──────┘
                             │
                      ┌──────┴──────┐
                      │   Switch    │ ← Standard Cherry MX
                      └─────────────┘

    Side view (coil moving through dual gaps):
    
         Magnet 1A    ║ Coil ║    Magnet 1B
         ┌──────┐     ║      ║     ┌──────┐
         │  N   │     ║ wire ║     │  N   │
         │  ↓   │ gap ║  ⊙   ║ gap │  ↓   │
         │  S   │ 4mm ║      ║ 4mm │  S   │
         └──────┘     ║      ║     └──────┘
                      ║      ║
         Magnet 2A    ║      ║    Magnet 2B
         ┌──────┐     ║      ║     ┌──────┐
         │  N   │     ║ wire ║     │  N   │
         │  ↑   │ gap ║  ⊗   ║ gap │  ↑   │
         │  S   │ 4mm ║      ║ 4mm │  S   │
         └──────┘     ║      ║     └──────┘
                      ╚══════╝
              Coil passes through 2 gaps
              → 2× active wire length per turn
```

**Magnet Options:**

All magnets share face dimensions: 1/2" × 1/4" (12.7 × 6.35mm), magnetized through thickness.

| Grade | Thickness | Dimensions (mm) | Br | Typical Cost (×4) |
|-------|-----------|-----------------|-------|-------------------|
| N52 | 1/8" | 12.7 × 6.35 × 3.18 | 14,800 G | ~$4.00 |
| N52 | 1/16" | 12.7 × 6.35 × 1.59 | 14,800 G | ~$3.00 |
| N42 | 1/8" | 12.7 × 6.35 × 3.18 | 13,200 G | ~$3.00 |
| N42 | 1/16" | 12.7 × 6.35 × 1.59 | 13,200 G | ~$2.50 |

**Gap Field Calculation:**

With N-S opposing configuration (attracting), fields reinforce. For 4mm gap (2mm from each surface to center):

```
Surface field estimation:
  B_surface ∝ Br × t / √(t² + r²)
  where r = effective face radius ≈ √(L×W/π) ≈ 5.1mm

Gap field from both magnets:
  B(z=2mm) ≈ B_surface × r² / (r² + z²) per magnet
  B_gap = 2 × B(z=2mm)  (both magnets contribute)
```

**Calculated Field Strengths:**

| Magnet | Surface Field | Gap Field (theoretical) | Design Value (0.75×) |
|--------|---------------|-------------------------|----------------------|
| **N52 1/8"** | 4,170 G | 0.72 T | **0.54 T** |
| N52 1/16" | 2,340 G | 0.40 T | 0.30 T |
| N42 1/8" | 3,720 G | 0.64 T | 0.48 T |
| N42 1/16" | 2,090 G | 0.36 T | 0.27 T |

**Force Calculation (Lorentz Force):**

```
F = B × I × L

Where:
  B = gap field strength (Tesla)
  I = coil current (Amps)
  L = active wire length in field (meters)

With dual-gap configuration:
  Active length per turn = 2 × 12.7mm = 25.4mm = 0.0254m
  (Wire passes through BOTH gaps, force contributions add)

Amp-turns required:
  A·T = F / (B × L_per_turn) = 0.49N / (B × 0.0254m)
```

**Magnet Comparison for 50g Force (Dual Gap):**

| Magnet | Gap Field | Amp-turns | Turns @ 1A | Current @ 40T | Power @ 40T |
|--------|-----------|-----------|------------|---------------|-------------|
| **N52 1/8"** | 0.54 T | **36** | 36 | **0.9 A** | **~1.0 W** |
| N52 1/16" | 0.30 T | 64 | 64 | 1.6 A | ~3.3 W |
| N42 1/8" | 0.48 T | 40 | 40 | 1.0 A | ~1.3 W |
| N42 1/16" | 0.27 T | 72 | 72 | 1.8 A | ~4.2 W |

**Magnet Selection Trade-offs:**

| Factor | N52 1/8" | N52 1/16" | N42 1/8" | N42 1/16" |
|--------|----------|-----------|----------|-----------|
| Force efficiency | ★★★★★ | ★★★ | ★★★★ | ★★½ |
| Coil simplicity | ★★★★★ | ★★★ | ★★★★ | ★★★ |
| Power/heat | ★★★★★ | ★★★ | ★★★★ | ★★½ |
| Cost (×4 magnets) | ★★★ | ★★★★ | ★★★★ | ★★★★★ |
| Handling safety | ★★ | ★★★ | ★★★ | ★★★★ |
| Availability | ★★★★ | ★★★★ | ★★★★★ | ★★★★★ |

**Recommendation:** 
- **Best performance:** N52 1/8" - Highest field, lowest power, fewest turns
- **Best value:** N42 1/8" - 90% of N52 performance at lower cost, easier handling
- **Budget option:** N42 1/16" - Works but needs 2× turns or current, more heat

**Recommended Coil Designs by Magnet Choice:**

| Spec | N52 1/8" (Best) | N42 1/8" (Value) | N42 1/16" (Budget) |
|------|-----------------|------------------|---------------------|
| Magnets (×4) | N52 1/2"×1/4"×1/8" | N42 1/2"×1/4"×1/8" | N42 1/2"×1/4"×1/16" |
| Gap field | ~0.54T | ~0.48T | ~0.27T |
| Coil wire | 30 AWG | 30 AWG | 28 AWG |
| Coil turns | 40 | 45 | 80 |
| Coil layers | 2 | 2-3 | 4 |
| Resistance | ~1.3Ω | ~1.5Ω | ~1.8Ω |
| Current for 50g | 0.9A | 1.0A | 1.8A |
| Operating voltage | 5V | 5V | 5V |
| Power @ 50g | ~1.0W | ~1.3W | ~4.2W |
| Response time | <5ms | <5ms | <5ms |
| Magnet cost (×4) | $4.00 | $3.00 | $2.50 |

**Primary Recommendation: N52 1/8"** - Best efficiency, lowest heat, simplest coil.

**Alternative: N42 1/8"** - Nearly as good (90% field strength), cheaper, safer to handle. Good choice if N52 availability is an issue.

**Coil Geometry Check:**

```
Gap: 4mm
30 AWG with insulation: ~0.29mm diameter

Layers that fit in gap: 4mm / 0.29mm ≈ 13 layers
Turns per layer (6.35mm width): 6.35 / 0.29 ≈ 21 turns/layer
Max turns possible: 13 × 21 ≈ 273 turns

Required: 40 turns → easily fits in 2 layers
```

**Bill of Materials (per actuator):**

| Item | N52 1/8" | N42 1/8" | N42 1/16" |
|------|----------|----------|-----------|
| Magnets (×4) | $4.00 | $3.00 | $2.50 |
| Magnet wire (30 or 28 AWG) | $1.50 | $1.50 | $2.00 |
| Coil former (3D printed) | $0.50 | $0.50 | $0.50 |
| Mounting hardware | $1.00 | $1.00 | $1.00 |
| **Per actuator** | **$7.00** | **$6.00** | **$6.00** |
| **All 7 actuators** | **~$49** | **~$42** | **~$42** |

Note: N42 1/16" requires more wire (80 turns vs 40) and heavier gauge for higher current.

**Advantages:**
- **Absolutely quietest** - No snap, no impact, smooth proportional force
- **Fastest response** - Low moving mass (<5ms)
- **Proportional control** - Force linear with current
- **Bidirectional** - Can push or pull (reverse current)
- **Low power** - ~1W per actuator at full force
- **Highest cool factor** - Speaker technology for buttons

**Risks:**
- Most complex construction
- Requires precise magnet alignment (opposing pairs must be parallel)
- Higher assembly time
- Strong magnets are brittle and can shatter if snapped together

**Magnet Handling Safety:**
- N52 magnets are extremely strong - keep away from electronics, credit cards, pacemakers
- Magnets can pinch fingers severely if allowed to snap together
- Store with spacers; assemble with care
- Wear safety glasses (magnets can shatter)

**Documentation:**
- Design details: [docs/voice-coil-design.md](docs/voice-coil-design.md)
- Test protocol: [docs/voice-coil-test-protocol.md](docs/voice-coil-test-protocol.md)

**Recommendation:** Build one test unit (~$10) to validate force output before full build.

---

#### Actuator Comparison Summary

| Factor | DIY Solenoid | Electromagnet | Voice Coil (N52) |
|--------|--------------|---------------|------------------|
| Noise | Soft thud | Very quiet | **Quietest** |
| Response | 15-20ms | 15-20ms | **<5ms** |
| Force control | On/off | On/off | **Proportional** |
| Complexity | Medium | Medium | Higher |
| Power per actuator | ~6W | ~29W | **~1W** |
| Cost (×7) | ~$38 | ~$26 | ~$49 |
| Test cost | N/A | ~$20 | ~$10 |
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
- Choose detection method (video capture vs light sensors)
- Detect single notes (one color/lane)
- Actuate one solenoid via PIC
- Hit a few notes in "Tutorial" or easiest song

**Path A: Video Capture Deliverables**:
- [ ] Video capture working, frames accessible in Python
- [ ] Basic color detection for green notes
- [ ] PIC firmware responds to serial commands
- [ ] Single solenoid fires on command
- [ ] End-to-end test: detect note → press button

**Path B: Light Sensor Deliverables**:
- [ ] Single phototransistor test rig built
- [ ] Validate detection of note vs background on one lane
- [ ] PIC ADC reading sensor reliably
- [ ] Single solenoid fires on detection
- [ ] End-to-end test: light sensor detects note → press button

**Detection Method Decision Criteria**:
- If light sensor reliably detects notes with >95% accuracy → proceed with Path B
- If light sensor is unreliable (ambient light issues, calibration problems) → fall back to Path A

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

1. **Detection Method**: Video capture (PC + OpenCV) vs Light sensors (standalone PIC)? Light sensors offer dramatically lower latency but are unproven.
2. **Game Platform**: PS2 vs Wii? (Wii has better video, more games, but wireless guitar adds complexity)
3. **PIC Model Selection**: PIC18F4550 (USB) vs PIC18F26K22 (simpler)? Light sensor approach needs 5+ ADC channels.
4. **Vision Platform**: Full PC vs Raspberry Pi 4? (Only applies if using video capture)
5. **Solenoid Voltage**: 5V (simpler power) vs 12V (faster response)?
6. **Mounting System**: 3D printed custom vs adjustable clamps?
7. **Latency Calibration**: Visual method vs audio sync?
8. **Light Sensor Placement**: At strike line (simpler) vs above strike line (more margin)?

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
