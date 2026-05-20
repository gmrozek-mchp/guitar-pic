# Electromagnet + Neodymium Magnet Actuation Design

## Overview

This document describes an alternative actuation approach using electromagnets to attract small neodymium magnets attached to keyboard switch keys. This approach offers potentially quieter operation than traditional solenoids.

**Principle:** An electromagnet mounted below each key attracts a permanent neodymium magnet glued to the key, pulling it down to actuate the switch.

---

## Design Concept

### Unpressed State (4mm gap)

```
         ┌─────────────────────────┐
         │      Keyboard Key       │
         │                         │
         │    ┌───┐                │
         │    │N S│ ← Neo magnet   │
         │    └─┬─┘   (4mm×2mm)    │
         └──────┼──────────────────┘
                │
                │  4mm air gap
                │
    ════════════╪═══════════════════ Mounting plate
                │
         ┌──────┴──────┐
         │  ░░░░░░░░░  │ ← Coil windings
         │  ░┌─────┐░  │
         │  ░│Core │░  │ ← Soft iron core
         │  ░└─────┘░  │
         │  ░░░░░░░░░  │
         └─────────────┘
              Electromagnet
              (not energized)
```

### Pressed State (1mm gap)

```
         ┌─────────────────────────┐
         │      Keyboard Key       │
         │    ┌───┐                │
         │    │N S│                │
         └────┴─┬─┴────────────────┘
                │  1mm gap
    ════════════╪═══════════════════
         ┌──────┴──────┐
         │  ░░░░░░░░░  │
         │  ░┌─────┐░  │ ← Energized - attracts magnet
         │  ░│Core │░  │
         │  ░└─────┘░  │
         │  ░░░░░░░░░  │
         └─────────────┘
```

---

## Physics

### Force on Permanent Magnet in Electromagnetic Field

The force between an electromagnet and a permanent magnet depends on:

1. **Electromagnet field strength** - Proportional to N × I (turns × current)
2. **Permanent magnet strength** - Determined by size, grade (N42, N52, etc.)
3. **Distance** - Force drops rapidly with distance (~1/r² to 1/r³)
4. **Geometry** - Alignment, pole orientation

### Approximate Force Equation

For a cylindrical electromagnet attracting an axially-magnetized disc magnet:

```
              μ₀ × (N × I)² × A_core × (Br × V_mag)
F  ≈  k × ────────────────────────────────────────────
                        g² × L_coil

Where:
  μ₀ = 4π × 10⁻⁷ H/m
  N = number of turns
  I = current (A)
  A_core = core cross-section area (m²)
  Br = magnet remanence (~1.2T for N42)
  V_mag = magnet volume (m³)
  g = air gap (m)
  L_coil = coil length (m)
  k = geometry factor (0.5-2, empirically determined)
```

### Force vs Distance Behavior

```
Force
  │
  │                                    
  │                              *
  │                           *
  │                        *
  │                     *
  │                  *
  │              *
  │         *
  │    *  *
  │ *
  └──────────────────────────────────► Gap distance
    0   1   2   3   4   5   6mm
    
    Force drops rapidly with distance
    But increases dramatically as gap closes
```

---

## Component Specifications

### Neodymium Magnet (on key)

**Selected: 4mm diameter × 2mm thick N42 disc**

| Property | Value |
|----------|-------|
| Diameter | 4mm |
| Thickness | 2mm |
| Grade | N42 |
| Material | NdFeB (Neodymium) |
| Remanence (Br) | ~1.28-1.32 T |
| Coercivity (Hc) | ~955 kA/m |
| Surface field | ~0.35-0.45 T |
| Pull force (to steel plate) | ~450g at contact |
| Max operating temp | 80°C |
| Coating | Nickel (Ni-Cu-Ni) |

**Alternative sizes to test:**
- 5mm × 2mm (more force, heavier)
- 4mm × 3mm (more force, taller)
- 6mm × 2mm (more force, wider)

### Electromagnet Coil

**Design target: 50g force at 4mm gap**

| Parameter | Value |
|-----------|-------|
| Core material | Soft iron rod (low carbon steel) |
| Core diameter | 5mm |
| Core length | 15mm (extends 3mm above coil) |
| Coil inner diameter | 6mm |
| Coil outer diameter | 12-14mm |
| Coil length | 10-12mm |
| Wire gauge | 28 AWG magnet wire |
| Number of turns | 200-250 |
| Resistance | 4-6Ω |
| Operating voltage | 12V |
| Operating current | 2-3A |
| Power (pulsed) | 24-36W peak |

### Coil Winding Details

```
Cross-section:

           ┌─────────────────┐
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ Layer 7 (outer)
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ 
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
           │▓▓▓▓┌───────┐▓▓▓▓│
Core ────► │▓▓▓▓│ Iron  │▓▓▓▓│ 5mm diameter
           │▓▓▓▓│ Core  │▓▓▓▓│
           │▓▓▓▓└───────┘▓▓▓▓│
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ Layer 1 (inner)
           └─────────────────┘
           
           ◄─────14mm──────►
```

| Dimension | Value |
|-----------|-------|
| Wire diameter (28 AWG) | 0.32mm |
| Wire + enamel | ~0.35mm |
| Turns per layer | ~30 (10mm ÷ 0.35mm) |
| Number of layers | 7-8 |
| Total turns | ~220 |
| Wire length | ~50m |
| Resistance (28 AWG @ 0.213Ω/m) | ~10Ω |

**Note:** With 10Ω resistance at 12V = 1.2A. For more force, use thicker wire (26 AWG) to allow higher current.

### Revised Design: Higher Current

| Parameter | 28 AWG Design | 26 AWG Design |
|-----------|---------------|---------------|
| Wire gauge | 28 AWG | 26 AWG |
| Resistance | ~10Ω | ~5Ω |
| Current at 12V | 1.2A | 2.4A |
| Force multiplier | 1× | ~4× |
| Coil OD | 14mm | 16mm |

**Recommendation:** Use **26 AWG** for more force margin.

---

## Estimated Performance

### Force vs Gap (26 AWG design, 12V, 2.4A)

| Gap (mm) | Estimated Force | Sufficient for 50g switch? |
|----------|-----------------|---------------------------|
| 4.0 | 25-45g | Marginal |
| 3.5 | 35-60g | Yes |
| 3.0 | 50-85g | Yes |
| 2.5 | 70-120g | Yes |
| 2.0 | 100-170g | Yes |
| 1.0 | 200-350g | Yes (strong) |

**Note:** These are estimates. Actual values depend on many factors and should be verified by testing.

### Response Time

| Phase | Time |
|-------|------|
| Electrical (L/R time constant) | ~2-5ms |
| Mechanical (magnet acceleration) | ~5-15ms |
| **Total response** | **~10-20ms** |

Fast enough for Expert difficulty (needs <30ms).

---

## Construction Guide

### Materials (per electromagnet)

| Item | Specification | Qty | Cost |
|------|---------------|-----|------|
| Iron rod | 5mm dia, soft iron | 15mm | $0.30 |
| Magnet wire | 26 AWG, ~50m | - | $0.60 |
| Bobbin | 3D printed or plastic tube | 1 | $0.30 |
| Neo magnet | 4mm×2mm N42 disc | 1 | $0.50 |
| **Total per unit** | | | **~$1.70** |

### Assembly Steps

1. **Prepare core**
   - Cut soft iron rod to 15mm length
   - File ends smooth
   - Clean with alcohol

2. **Prepare bobbin**
   - 3D print bobbin (ID: 5.5mm, OD: 8mm, length: 12mm)
   - Or use plastic/paper tube
   - Add flanges to retain wire

3. **Insert core**
   - Slide core into bobbin
   - Position so 2-3mm extends above coil
   - This extension focuses field toward magnet

4. **Wind coil**
   - Start winding at one end
   - Wind 200-250 turns of 26 AWG
   - Keep tight and even
   - Secure ends with tape

5. **Test electrically**
   - Measure resistance (expect 4-6Ω)
   - Check for shorts to core

6. **Attach magnet to key**
   - Clean key surface with alcohol
   - Apply small drop of cyanoacrylate (super glue)
   - Center 4mm×2mm neo magnet
   - Ensure N or S pole faces down (toward coil)
   - Allow to cure

### Core Extension Diagram

```
         Neo magnet on key
              [N][S]
                │
                │ 4mm gap
                │
    ────────────┼──────────── Mounting surface
         ┌──────┴──────┐
         │      │      │
         │   ┌──┴──┐   │
         │   │Core │   │ ← Core extends 2-3mm
         │   │     │   │   above coil top
         │   │     │   │
         │   └─────┘   │
         │   ▓▓▓▓▓▓▓   │ ← Coil surrounds lower part
         │   ▓▓▓▓▓▓▓   │
         └─────────────┘
         
    Core extension focuses field
    and increases force at distance
```

---

## Electrical Design

### Single Electromagnet Circuit

```
         12V
          │
          │
    ┌─────┴─────┐
    │    EMag   │
    │   (~5Ω)   │
    └─────┬─────┘
          │
          ├────────┐
          │        │
    ┌─────┴─────┐  │
    │  Flyback  │  │  ← Schottky diode (1N5819)
    │   Diode   │  │    for faster decay
    └─────┬─────┘  │
          │        │
          └────────┤
                   │
            ┌──────┴──────┐
            │   MOSFET    │ ← Logic-level (IRLZ44N)
            │ or Driver   │
            └──────┬──────┘
                   │
                   │
            ┌──────┴──────┐
            │  PIC GPIO   │
            └─────────────┘
```

### Driver Recommendations

| Option | Channels | Max Current | Notes |
|--------|----------|-------------|-------|
| IRLZ44N MOSFETs | 1 each | 40A | Need 7 MOSFETs + flyback diodes |
| ULN2803A | 8 | 500mA | **Insufficient** - only 500mA |
| L298N module | 2 | 2A | Could work, need multiple |
| Individual MOSFETs | Best for this current level |

**Recommended:** Individual **IRLZ44N** MOSFETs with **1N5819** Schottky flyback diodes.

### Power Requirements

| Scenario | Current | Power |
|----------|---------|-------|
| 1 electromagnet | 2.4A | 29W |
| 3 simultaneous (chord) | 7.2A | 86W |
| All 7 (worst case) | 16.8A | 202W |

**Power supply recommendation:** 12V 15-20A switching supply

(In practice, not all actuate simultaneously for long, so 10A supply may suffice)

---

## Noise Characteristics

### Why This Approach Is Quiet

| Noise Source | Solenoid | Electromagnet+Magnet |
|--------------|----------|----------------------|
| Plunger impact | LOUD click | None - no plunger |
| Core/armature contact | Click | Soft - magnet touches key stop |
| Mechanical vibration | Moderate | Minimal |
| Electrical buzz | Some | Some |

**Expected noise:** Near-silent actuation. Only sound is the keyboard switch click itself.

### Potential Noise Sources

1. **Magnetostriction** - Core may vibrate slightly under AC or PWM
   - Mitigation: Use DC, not PWM for hold
   
2. **Key bottoming out** - Key hits switch bottom
   - Mitigation: This is unavoidable, but softer than solenoid click

---

## Comparison: Electromagnet vs DIY Solenoid

| Factor | DIY Solenoid | Electromagnet + Neo |
|--------|--------------|---------------------|
| Noise | Soft thud | Near silent |
| Complexity | Medium | Medium |
| Force at 4mm | Good (30-50g) | Marginal (25-45g) |
| Cost per unit | ~$1.50 | ~$1.70 |
| Moving parts | Plunger + spring | None in actuator |
| Response time | 10-20ms | 10-20ms |
| Power | ~2A each | ~2.4A each |
| Reliability | Proven | Needs testing |

---

## Known Risks and Mitigations

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Insufficient force at 4mm | Medium | Use stronger magnet, more current |
| Magnet falls off key | Low | Use proper adhesive, roughen surface |
| Adjacent magnet interference | Low | Space adequately (>15mm) |
| Core retains magnetism | Low | Use soft iron, demagnetize if needed |
| Overheating | Medium | Pulsed operation, adequate wire gauge |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 0.1 | 2026-01-28 | Initial design |
