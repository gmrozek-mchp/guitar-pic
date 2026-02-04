# Voice Coil Actuator - 3D Part Dimensions

## Overview

This document provides detailed dimensions for the 3D printed parts of the voice coil actuator. Use alongside `voice-coil-parts.scad` for generating STL files.

---

## Part 1: Bobbin (Moving Part)

The bobbin holds the voice coil and attaches to the keyboard switch stem.

### Top View

```
                    18.0mm
    ┌─────────────────────────────────────┐
    │              ┌───────────────┐      │
    │              │               │      │
    │              │   10.0mm      │      │
    │              │   inner       │      │
    │              │   opening     │      │
    │  ┌───────────┤               ├───┐  │
    │  │           │    ┌───┐      │   │  │
    │  │ wire      │    │ + │      │   │  │ 18.0mm
    │  │ slot      │    │MX │      │   │  │
    │  │ 2mm       │    │stem      │   │  │
    │  │           │    └───┘      │   │  │
    │  └───────────┤               ├───┘  │
    │              │               │      │
    │              │               │      │
    │              └───────────────┘      │
    │                                     │
    └─────────────────────────────────────┘
```

### Side View (Cross Section)

```
                    18.0mm
    ┌─────────────────────────────────────┐
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ ← 1.5mm floor
    │▓▓▓┌───────────────────────────┐▓▓▓│
    │▓▓▓│ ░░░░░░░░░░░░░░░░░░░░░░░░░ │▓▓▓│ ← 2.5mm coil groove
    │▓▓▓│ ░░░░░ COIL GROOVE ░░░░░░░ │▓▓▓│   (3.0mm wide)
    │▓▓▓└───────────────────────────┘▓▓▓│    8.0mm
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│   total
    │▓▓▓▓▓▓▓▓▓┌───────┐▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│   height
    │▓▓▓▓▓▓▓▓▓│  MX   │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ ← 4.5mm stem socket
    │▓▓▓▓▓▓▓▓▓│ stem  │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
    │▓▓▓▓▓▓▓▓▓│socket │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
    └─────────┴───────┴─────────────────┘
              4.3mm dia
              (MX cross)
```

### Detailed Dimensions

| Feature | Dimension | Tolerance |
|---------|-----------|-----------|
| Outer width | 18.0 mm | ±0.2 |
| Outer depth | 18.0 mm | ±0.2 |
| Total height | 8.0 mm | ±0.2 |
| Inner opening | 10.0 × 10.0 mm | ±0.3 |
| Wall thickness | 4.0 mm | - |
| Coil groove width | 3.0 mm | ±0.2 |
| Coil groove depth | 2.5 mm | ±0.2 |
| Stem socket diameter | 4.3 mm | +0.2/-0 |
| Stem socket depth | 4.5 mm | ±0.3 |
| Wire slot width | 2.0 mm | ±0.2 |
| Wire slot depth | 3.0 mm | ±0.2 |
| Corner radius | 2.0 mm | - |

### MX Stem Socket Detail

```
    Cherry MX stem cross-section:
    
           1.2mm
          ┌─────┐
          │     │
    ┌─────┤     ├─────┐
    │     │     │     │ 1.2mm
    └─────┤     ├─────┘
          │     │
          └─────┘
            4.0mm
    
    Socket: Add 0.3mm clearance = 4.3mm × 1.5mm cross
```

---

## Part 2: Housing (Fixed Part)

The housing holds the magnets and steel yokes, mounting around the switch.

### Top View

```
                         24.0mm
    ┌───────────────────────────────────────────────┐
    │                                               │
    │   ┌─────┐                         ┌─────┐    │
    │   │ MAG │                         │ MAG │    │
    │   │10×5 │     ┌───────────┐       │10×5 │    │
    │   └─────┘     │           │       └─────┘    │
    │               │  15.6mm   │                  │
    │               │  switch   │                  │
    │    3.5mm      │  opening  │      3.5mm       │  24.0mm
    │     gap       │           │       gap        │
    │               │           │                  │
    │   ┌─────┐     │           │       ┌─────┐    │
    │   │ MAG │     └───────────┘       │ MAG │    │
    │   │10×5 │                         │10×5 │    │
    │   └─────┘                         └─────┘    │
    │                                               │
    │   ════════════════════════════════════════   │ ← Yoke slot
    │                                               │
    └───────────────────────────────────────────────┘
          │                                   │
          └─── Mounting tabs with M2.5 holes ─┘
```

### Side View (Cross Section)

```
                         24.0mm
    ┌───────────────────────────────────────────────┐
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
    │▓▓▓▓▓▓▓═══════════════════════════▓▓▓▓▓▓▓▓▓▓▓│ ← Top yoke slot
    │▓▓┌───┐▓▓▓▓▓▓▓┌───────────┐▓▓▓▓▓▓▓┌───┐▓▓▓▓▓│   (2.2mm deep)
    │▓▓│MAG│▓▓▓▓▓▓▓│           │▓▓▓▓▓▓▓│MAG│▓▓▓▓▓│
    │▓▓│   │▓▓▓▓▓▓▓│  Bobbin   │▓▓▓▓▓▓▓│   │▓▓▓▓▓│
    │▓▓└───┘▓▓▓▓▓▓▓│  travel   │▓▓▓▓▓▓▓└───┘▓▓▓▓▓│  12.0mm
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓│  space    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│  total
    │▓▓┌───┐▓▓▓▓▓▓▓│           │▓▓▓▓▓▓▓┌───┐▓▓▓▓▓│  height
    │▓▓│MAG│▓▓▓▓▓▓▓│           │▓▓▓▓▓▓▓│MAG│▓▓▓▓▓│
    │▓▓│   │▓▓▓▓▓▓▓└───────────┘▓▓▓▓▓▓▓│   │▓▓▓▓▓│
    │▓▓└───┘▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓└───┘▓▓▓▓▓│
    │▓▓════════════════════════════════════════▓▓▓│ ← Bottom yoke slot
    └───────────────────────────────────────────────┘   (2.2mm deep)
    
    ◄─────────── Switch opening (15.6mm) ──────────►
           ◄─ gap ─►             ◄─ gap ─►
            3.5mm                 3.5mm
```

### Magnet Pocket Detail

```
    Individual magnet pocket (4 total):
    
    ┌────────────────┐
    │                │
    │   10.2 × 5.2   │ ← With 0.2mm clearance
    │    × 3.2mm     │
    │                │
    └────────────────┘
    
    Pocket depth: 3.2mm (magnet sits flush or slightly recessed)
```

### Detailed Dimensions

| Feature | Dimension | Tolerance |
|---------|-----------|-----------|
| Outer width | 24.0 mm | ±0.3 |
| Outer depth | 24.0 mm | ±0.3 |
| Total height | 12.0 mm | ±0.2 |
| Switch opening | 15.6 × 15.6 mm | +0.5/-0 |
| Bobbin channel | 19.0 × 19.0 mm | ±0.3 |
| Bobbin channel depth | 6.0 mm | ±0.3 |
| Magnet pocket size | 10.2 × 5.2 × 3.2 mm | +0.2/-0 |
| Air gap width | 3.5 mm | ±0.2 |
| Bottom yoke slot | 22 × 10.3 × 2.2 mm | +0.2/-0 |
| Top yoke slot | (split) 2.2 mm deep | +0.2/-0 |
| Wall thickness | 2.0 mm | - |
| Corner radius | 3.0 mm | - |
| Mounting tab width | 6.0 mm | - |
| Mounting tab length | 4.0 mm | - |
| Mounting hole diameter | 2.5 mm | (for M2.5) |

---

## Part 3: Wire Guide / Strain Relief

Small clip to manage coil wires.

### Dimensions

```
    Top view:              Side view:
    
    ┌────────────┐         ┌────────────┐
    │  ○      ○  │         │            │
    │ 1.5mm dia  │  8mm    │    ○   ○   │ 4mm
    │  holes     │         │────────────│
    └────────────┘         └─────┬──────┘
         5mm                     │
                            Clip slot
```

| Feature | Dimension |
|---------|-----------|
| Width | 8.0 mm |
| Depth | 5.0 mm |
| Height | 4.0 mm |
| Wire holes | 1.5 mm diameter |
| Hole spacing | 4.0 mm center-to-center |
| Clip slot | 1.5 mm deep |

---

## Steel Yoke Dimensions

Cut from 2mm thick steel sheet.

### Bottom Yoke

```
    ┌────────────────────────────────┐
    │                                │
    │            22 × 10 mm          │  2mm thick
    │                                │
    └────────────────────────────────┘
```

### Top Yokes (2 pieces)

```
    ┌──────────┐          ┌──────────┐
    │          │          │          │
    │ ~8 × 10  │    or    │ ~8 × 10  │  2mm thick
    │   mm     │          │   mm     │
    └──────────┘          └──────────┘
    
    (Exact size depends on housing, aim to cover magnets)
```

---

## Print Settings

### Recommended Settings

| Parameter | Value |
|-----------|-------|
| Layer height | 0.15-0.2 mm |
| Infill | 30-50% |
| Perimeters | 3 |
| Top/bottom layers | 4 |
| Material | PLA or PETG |
| Supports | Yes (for housing magnet pockets) |
| Bed adhesion | Brim recommended |

### Print Orientation

**Bobbin:**
- Print with stem socket facing UP (no supports needed)
- Coil groove will print cleanly

**Housing:**
- Print with bottom yoke slot facing DOWN
- Use supports for magnet pocket overhangs
- Remove supports carefully

**Wire Guide:**
- Print flat, no supports needed

---

## Assembly Notes

### Clearances

| Interface | Designed Clearance | Notes |
|-----------|-------------------|-------|
| Bobbin in housing gap | 0.25 mm per side | Allows smooth movement |
| Magnet in pocket | 0.1 mm per side | Snug fit, may need glue |
| MX stem in socket | 0.15 mm | Light press fit |
| Yoke in slot | 0.1 mm | Snug fit |

### Fit Adjustments

If parts don't fit:
- **Too tight:** Sand lightly or increase tolerance in OpenSCAD
- **Too loose:** Use thin tape shim or decrease tolerance
- **Magnets fall out:** Use small drop of CA glue

---

## Coil Winding Reference

The bobbin's coil groove accommodates:

| Parameter | Value |
|-----------|-------|
| Wire gauge | 30 AWG (0.255mm bare) |
| Wire with enamel | ~0.29 mm |
| Groove width | 3.0 mm |
| Groove depth | 2.5 mm |
| Turns per layer | ~10 (3.0mm / 0.29mm) |
| Layers | 6-8 |
| Total turns | 60-80 |

**Winding path:**
```
    Start at wire slot → wind around groove → 
    layer by layer → end at opposite wire slot
```

---

## File Locations

| File | Description |
|------|-------------|
| `voice-coil-parts.scad` | OpenSCAD source file |
| `bobbin.stl` | Export from OpenSCAD |
| `housing.stl` | Export from OpenSCAD |
| `wire-guide.stl` | Export from OpenSCAD |

## Generating STL Files

### Method 1: Using Customizer (Recommended)

1. Open `voice-coil-parts.scad` in OpenSCAD
2. Enable Customizer: **View → Customizer**
3. In the Customizer panel, find **Part Selection**
4. Select from dropdown: `bobbin`, `housing`, `wire_guide`, or `print_plate`
5. Press **F6** to render
6. **File → Export → Export as STL**

### Method 2: Edit PART Variable

1. Open `voice-coil-parts.scad` in OpenSCAD
2. Scroll to bottom, find: `PART = "assembly";`
3. Change to: `PART = "bobbin";` (or housing, wire_guide, print_plate)
4. Press F6 to render
5. File → Export → Export as STL

### Method 3: Command Line

```bash
openscad -o bobbin.stl -D 'PART="bobbin"' voice-coil-parts.scad
openscad -o housing.stl -D 'PART="housing"' voice-coil-parts.scad
openscad -o wire_guide.stl -D 'PART="wire_guide"' voice-coil-parts.scad
```
