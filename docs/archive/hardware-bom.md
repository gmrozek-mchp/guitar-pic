# Hardware Bill of Materials

## Overview

This document tracks all hardware components needed for the Guitar Hero Bot project.

---

## Core Components

### Video Capture

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| Elgato Game Capture HD | Component input, USB 2.0 | 1 | Needed | eBay/Amazon | $50-80 |
| PS2 Component Cables | YPbPr + Audio (5 RCA) | 1 | Needed | Amazon | $10-15 |

**Capture Device Specs:**
- Model: Elgato Game Capture HD (original, not HD60)
- Input: Component video (Y/Pb/Pr) + analog audio
- Output: USB 2.0 to PC
- Resolution: Up to 1080i, we use 480p from PS2
- Latency: ~50-70ms capture delay
- Power: External adapter (included)

### Microcontroller

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| PIC18F4550 | 40-pin DIP or dev board | 1 | Needed | Microchip Direct | $10-30 |
| Crystal | 20MHz (if not using internal osc) | 1 | Needed | - | $1 |
| Capacitors | 22pF for crystal | 2 | Needed | - | $0.50 |
| USB-B Connector | For programming/communication | 1 | Needed | - | $2 |

### Actuators - Fret Buttons & Strum Bar

**Selected: DIY Miniature Solenoids**

Custom-wound solenoids for low-force, quiet actuation. Two solenoids per switch for balanced force.

See detailed design: [diy-solenoid-design.md](diy-solenoid-design.md)

| Spec | Value |
|------|-------|
| Configuration | 2 solenoids × 7 switches = 14 total |
| Force per solenoid | 25g |
| Voltage | 12V DC |
| Coil resistance | ~6Ω |
| Wire | 24 AWG, ~230 turns each |

### DIY Solenoid Materials

| Item | Specification | Quantity | Source | Est. Cost |
|------|--------------|----------|--------|-----------|
| Magnet wire | 24 AWG enameled copper, 200m spool | 1 | Amazon/eBay | $12-15 |
| Steel bolts (core) | M4×20mm | 30 | Hardware store | $5 |
| Steel bolts (plunger) | M4×10mm | 20 | Hardware store | $3 |
| Compression springs | 4mm OD × 10mm | 20 | Amazon | $5 |
| 3D printed bobbins | Custom design (see docs) | 14 | Self | $1 |
| Flyback diodes | 1N4007 | 14 | Electronics supplier | $2 |
| Heat shrink tubing | Assorted | 1 pack | Amazon | $3 |

### Dampening Materials

| Item | Purpose | Quantity | Source | Est. Cost |
|------|---------|----------|--------|-----------|
| Silicone bumper pads | Soft plunger stop | 20 | Amazon | $3 |
| Rubber grommets | Mount isolation | 30 | Hardware store | $3 |
| Foam sheet | Acoustic enclosure | 1 | Craft store | $2 |

**Total solenoid materials: ~$38-42**

---

### Alternative: Electromagnet + Neodymium Magnet Approach

An experimental quieter approach - test before committing to full build.

See detailed design: [electromagnet-design.md](electromagnet-design.md)
See test protocol: [electromagnet-test-protocol.md](electromagnet-test-protocol.md)

#### Test Materials (validate concept first)

| Item | Specification | Quantity | Source | Est. Cost |
|------|--------------|----------|--------|-----------|
| Neodymium magnets | 4mm×2mm N42 disc (10 pack) | 1 | Amazon | $5 |
| Neodymium magnets | 5mm×2mm N42 disc (10 pack) | 1 | Amazon | $5 |
| Iron/steel bolts | M5×30mm | 5 | Hardware store | $3 |
| Magnet wire | 26 AWG, 50m spool | 1 | Amazon | $8 |
| **Test materials total** | | | | **~$20** |

#### Full Build Materials (if test passes)

| Item | Specification | Quantity | Source | Est. Cost |
|------|--------------|----------|--------|-----------|
| Neodymium magnets | 4mm×2mm N42 disc | 10 | Amazon | $5 |
| Magnet wire | 26 AWG, 100m spool | 1 | Amazon | $10 |
| Soft iron rod | 5mm dia × 200mm | 2 | Specialty metals | $8 |
| 3D printed bobbins | Custom design | 7 | Self | $1 |
| Flyback diodes | 1N5819 Schottky | 10 | Electronics | $2 |
| **Full build total** | | | | **~$26** |

**Note:** Electromagnet approach requires higher current (~2.4A each) - may need upgraded power supply (12V 15-20A) and MOSFET drivers instead of ULN2803A.

---

### Alternative: DIY Voice Coil Actuators

The quietest option - uses Lorentz force (current in magnetic field) for smooth, silent actuation.

See detailed design: [voice-coil-design.md](voice-coil-design.md)
See test protocol: [voice-coil-test-protocol.md](voice-coil-test-protocol.md)

#### Test Materials (validate concept first)

| Item | Specification | Quantity | Source | Est. Cost |
|------|--------------|----------|--------|-----------|
| N42 block magnets | 10×5×3mm (10 pack) | 1 | Amazon | $6 |
| Steel sheet | 2mm thick, 50×50mm | 1 | Hardware store | $3 |
| Magnet wire | 30 AWG, 25m spool | 1 | Amazon | $6 |
| 3D printed parts | Bobbin + housing | 1 set | Self | $1 |
| Cherry MX switch | For fit testing | 1 | Spare/Amazon | $1 |
| **Test materials total** | | | | **~$17** |

#### Full Build Materials (if test passes)

| Item | Specification | Quantity | Source | Est. Cost |
|------|--------------|----------|--------|-----------|
| N42 block magnets | 10×5×3mm (50 pack) | 1 | Amazon | $12 |
| Steel sheet | 2mm thick, 100×100mm | 1 | Hardware store | $5 |
| Magnet wire | 30 AWG, 100m spool | 1 | Amazon | $8 |
| L9110S H-bridge modules | Dual channel | 4 | Amazon | $6 |
| 3D printed parts | 7× bobbin + housing | 7 sets | Self | $2 |
| **Full build total** | | | | **~$33** |

**Note:** Voice coil uses 5V at ~0.5-1A per actuator. Standard 5V supply sufficient, but needs H-bridge drivers for bidirectional control.

---

### Actuators - Whammy & Tilt

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| Servo Motor | SG90 or MG90S | 2 | Needed | Amazon | $6 |

### Driver Electronics

**For DIY Solenoid approach (lower current, unidirectional):**

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| ULN2803A | 8-channel Darlington driver | 2 | Needed | Electronics | $3 |

**For Electromagnet approach (higher current ~2.4A each, unidirectional):**

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| IRLZ44N MOSFETs | Logic-level N-channel | 8 | Needed | Electronics | $8 |
| 1N5819 Schottky diodes | Flyback protection | 8 | Needed | Electronics | $2 |
| Resistors | 100Ω gate resistors | 8 | Needed | Electronics | $0.50 |

**For Voice Coil approach (moderate current ~1A, bidirectional):**

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| L9110S H-bridge modules | Dual channel, 1.5A | 4 | Needed | Amazon | $6 |

*Note: L9110S provides bidirectional control (push/pull) needed for voice coils. Alternative: DRV8833 for higher current capacity.*

### Power Supply

**For DIY Solenoid approach:**

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| DC Power Supply | 12V 5A | 1 | Needed | Amazon | $15 |
| Voltage Regulator | 5V 2A (LM7805 or buck) | 1 | Needed | - | $3 |
| Capacitors | 100µF, 10µF for regulation | 4 | Needed | - | $1 |

**For Electromagnet approach (higher current):**

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| DC Power Supply | 12V 15-20A | 1 | Needed | Amazon | $25-35 |
| Voltage Regulator | 5V 2A (LM7805 or buck) | 1 | Needed | - | $3 |
| Capacitors | 100µF, 10µF for regulation | 4 | Needed | - | $1 |

### Mechanical & Mounting

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| 3D Printed Brackets | Custom design | Set | Needed | Self/Service | $10 |
| Screws/Standoffs | M3 assorted | Pack | Needed | - | $5 |
| Wires | 22AWG hookup wire | Spool | Needed | - | $5 |
| Connectors | JST or Dupont | Pack | Needed | - | $5 |

### Game Equipment

| Item | Specification | Quantity | Status | Source | Est. Cost |
|------|--------------|----------|--------|--------|-----------|
| PS2 Guitar Controller | Any Guitar Hero model | 1 | Needed | eBay/Thrift | $20-40 |
| PS2 Console | Working, with AV cables | 1 | Needed | - | - |
| Guitar Hero Game | Any PS2 version | 1 | Needed | - | - |

---

## Cost Summary

| Category | Estimated Cost |
|----------|----------------|
| Video Capture | $60-95 |
| Microcontroller | $15 |
| DIY Solenoid materials | $38-42 |
| Servos (whammy/tilt) | $6 |
| Driver Electronics | $10 |
| Power Supply | $19 |
| Mechanical/Mounting | $25 |
| Game Equipment | $20-40 |
| **Total** | **$193-252** |

---

## Notes

- Prices are estimates and may vary
- Consider buying extra solenoids for testing/spares
- PS2 guitar controllers are becoming harder to find
- Alternative: Build for Clone Hero on PC (eliminates capture card need)

---

## Procurement Checklist

### Video & Control
- [ ] Order Elgato Game Capture HD
- [ ] Order PS2 component cables (YPbPr)
- [ ] Order/source PIC microcontroller (PIC18F4550)
- [ ] Order 20MHz crystal + 22pF capacitors

### DIY Solenoid Materials
- [ ] Order 24 AWG magnet wire (200m spool)
- [ ] Order M4×20mm steel bolts (30 pack)
- [ ] Order M4×10mm steel bolts (20 pack)
- [ ] Order small compression springs (20 pack)
- [ ] 3D print bobbins (14 units)
- [ ] Order 1N4007 flyback diodes (20 pack)

### Other Actuators & Electronics
- [ ] Order SG90/MG90S servos (2x) for whammy/tilt
- [ ] Order ULN2803A driver ICs (2x)
- [ ] Order 12V 5A power supply
- [ ] Order 5V voltage regulator

### Dampening & Mounting
- [ ] Order silicone bumper pads
- [ ] Order rubber grommets
- [ ] Order foam sheet for acoustic dampening
- [ ] Gather mounting hardware (screws, standoffs)
- [ ] 3D print or fabricate solenoid mounts

### Game Equipment
- [ ] Source PS2 guitar controller
- [ ] Verify PS2 console + Guitar Hero game
