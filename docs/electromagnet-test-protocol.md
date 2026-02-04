# Electromagnet Actuation - Test Protocol

## Purpose

Validate the electromagnet + neodymium magnet actuation concept before committing to full build. This low-cost test will determine if sufficient force can be generated at 4mm air gap.

---

## Test Objectives

1. **Measure actual force** at 4mm gap
2. **Determine minimum current** needed for 50g force
3. **Verify response time** is acceptable (<30ms)
4. **Assess noise level** compared to solenoid
5. **Identify any issues** with the approach

---

## Materials Needed

### Test Components

| Item | Specification | Source | Est. Cost |
|------|---------------|--------|-----------|
| Neodymium magnets | 4mm×2mm N42 disc (10 pack) | Amazon | $5 |
| Neodymium magnets (alt) | 5mm×2mm N42 disc (10 pack) | Amazon | $5 |
| Iron rod or bolts | 5mm dia, M5×30mm bolts | Hardware store | $3 |
| Magnet wire | 26 AWG, 50m+ spool | Amazon | $8 |
| **Total** | | | **~$15-20** |

### Test Equipment

| Item | Purpose | Notes |
|------|---------|-------|
| Digital kitchen scale | Measure force | 0.1g resolution preferred |
| Multimeter | Measure resistance, current | |
| 12V power supply | Power the coil | Variable voltage helpful |
| Ammeter or current sense | Measure actual current | Multimeter works |
| Ruler / calipers | Set and verify gap distance | |
| Stopwatch or oscilloscope | Measure response time | Optional |

### Tools

- Wire cutters/strippers
- Soldering iron
- Tape (electrical, masking)
- Superglue (for attaching magnet)
- Small clamp or vise

---

## Test Coil Construction

### Quick Test Coil

Build a test electromagnet in ~30 minutes:

```
Materials:
- M5×30mm steel bolt (core)
- 26 AWG magnet wire
- Tape

Steps:
1. Wrap tape around bolt threads (insulation)
2. Leave 100mm wire lead
3. Wind ~200 turns tightly around bolt shank
4. Secure with tape
5. Leave 100mm wire lead
6. Strip enamel from wire ends (sandpaper)
```

### Winding Diagram

```
        Head of bolt
             │
        ┌────┴────┐
        │  Nut    │ ← Optional: holds in fixture
        │(remove) │
        ├─────────┤
        │▓▓▓▓▓▓▓▓▓│
        │▓▓▓▓▓▓▓▓▓│
        │▓▓▓▓▓▓▓▓▓│  ← ~200 turns of 26 AWG
        │▓▓▓▓▓▓▓▓▓│     wound around shank
        │▓▓▓▓▓▓▓▓▓│
        ├─────────┤
        │  Shank  │ ← 5mm diameter (M5)
        │(smooth) │
        └─────────┘
        
    Wire leads out to power supply
```

---

## Test Setup

### Force Measurement Rig

```
                    ┌───────────────┐
                    │ Support frame │
                    │ (wood block,  │
                    │  box, etc.)   │
                    └───────┬───────┘
                            │
     Adjustable ──────►     │ ◄─── Set gap here (4mm, 3mm, etc.)
     spacer                 │
                            │
                    ┌───────┴───────┐
                    │    Neo        │
                    │   magnet      │ ← Glued to test paddle/key
                    │    [NS]       │
                    └───────┬───────┘
                            │
    ════════════════════════╪════════════ Gap (measured)
                            │
                    ┌───────┴───────┐
                    │    Test       │
                    │ Electromagnet │
                    └───────┬───────┘
                            │
                    ┌───────┴───────┐
                    │ Kitchen scale │ ← Measures pull force
                    │               │
                    └───────────────┘
```

### Alternative: Pull-Down Test

```
                    Fixed mount
                    ┌─────────────┐
                    │ Electromagnet│
                    │   (fixed)    │
                    └──────┬───────┘
                           │
                     4mm gap
                           │
                    ┌──────┴───────┐
                    │  Neo magnet  │
                    │   on string  │ ← Attach weights until
                    └──────┬───────┘   coil can't lift
                           │
                    ┌──────┴───────┐
                    │   Weights    │
                    │  (measure g) │
                    └──────────────┘
```

---

## Test Procedures

### Test 1: Coil Verification

**Objective:** Verify test coil is working properly

**Procedure:**
1. Measure coil resistance with multimeter
   - Expected: 3-8Ω for 26 AWG, 200 turns
2. Apply 12V briefly (<5 seconds)
3. Verify core becomes magnetic (attracts paper clip)
4. Remove power, verify magnetism drops quickly

**Pass criteria:**
- Resistance: 3-10Ω
- Core magnetizes when powered
- Core demagnetizes when power removed

### Test 2: Magnet Attraction (Qualitative)

**Objective:** Confirm neo magnet is attracted at distance

**Procedure:**
1. Hold 4mm×2mm neo magnet 10mm above energized coil
2. Feel for attraction force
3. Slowly lower to 5mm, 4mm, 3mm
4. Note where force becomes noticeable

**Record:**
- Distance at which attraction first felt: ___ mm
- Qualitative strength at 4mm: Weak / Moderate / Strong

### Test 3: Force Measurement at Fixed Gaps

**Objective:** Quantify pull force at various gap distances

**Procedure:**
1. Set up force measurement rig (scale method)
2. Set spacer for 5mm gap
3. Apply 12V to coil
4. Read force on scale (or note max weight that can be held)
5. Repeat for 4mm, 3mm, 2mm gaps
6. Repeat with 5mm×2mm magnet (if available)

**Data Table:**

| Gap (mm) | 4mm×2mm Magnet Force (g) | 5mm×2mm Magnet Force (g) |
|----------|--------------------------|--------------------------|
| 5.0 | | |
| 4.0 | | |
| 3.5 | | |
| 3.0 | | |
| 2.5 | | |
| 2.0 | | |
| 1.0 | | |

**Pass criteria:** ≥50g at 4mm gap (or ≥25g if using dual electromagnets)

### Test 4: Current vs Force

**Objective:** Determine optimal operating current

**Procedure:**
1. Set gap to 4mm
2. Use variable power supply or resistors to vary current
3. Measure force at different currents
4. Find minimum current for 50g force

**Data Table:**

| Voltage (V) | Current (A) | Force at 4mm (g) |
|-------------|-------------|------------------|
| 3 | | |
| 6 | | |
| 9 | | |
| 12 | | |

### Test 5: Response Time (Optional)

**Objective:** Measure actuation speed

**Procedure:**
1. Set up magnet on pivot or spring so it can move
2. Use oscilloscope or high-speed camera
3. Measure time from power-on to magnet reaching coil

**Pass criteria:** <30ms response time

### Test 6: Noise Assessment

**Objective:** Confirm quiet operation

**Procedure:**
1. Set up magnet that can move freely toward coil
2. Actuate multiple times
3. Listen for noise
4. Compare to solenoid (if available) or reference sound

**Record:**
- Sound level: Silent / Soft click / Moderate / Loud
- Character: None / Thud / Click / Buzz
- Comparison notes: ___

### Test 7: Repeated Actuation

**Objective:** Check for thermal issues and consistency

**Procedure:**
1. Actuate 100 times with 50% duty cycle
2. Monitor coil temperature (touch test or IR thermometer)
3. Check if force remains consistent

**Pass criteria:**
- Coil temperature: Warm but not hot (<60°C)
- Force remains consistent (±10%)

---

## Results Recording

### Test Summary Sheet

```
Date: _______________
Tester: _____________

COIL SPECIFICATIONS:
  Core: _____________ (material, size)
  Wire gauge: _______
  Turns: ___________
  Measured resistance: _____ Ω

MAGNET SPECIFICATIONS:
  Size: _____________ mm
  Grade: ___________

TEST RESULTS:

Test 1 - Coil Verification:
  Resistance: _____ Ω  [PASS/FAIL]
  Magnetizes: [YES/NO]
  Demagnetizes: [YES/NO]

Test 2 - Qualitative Attraction:
  First felt at: _____ mm
  Strength at 4mm: [Weak/Moderate/Strong]

Test 3 - Force Measurement:
  Force at 4mm: _____ g  [PASS if ≥50g / ≥25g for dual]

Test 4 - Current for 50g at 4mm:
  Required current: _____ A
  Required voltage: _____ V

Test 5 - Response Time:
  Measured: _____ ms  [PASS if <30ms]

Test 6 - Noise Level:
  Assessment: [Silent/Soft/Moderate/Loud]

Test 7 - Thermal:
  After 100 cycles: [Cool/Warm/Hot]
  Force consistency: [Consistent/Degraded]

OVERALL ASSESSMENT: [VIABLE / NEEDS MODIFICATION / NOT VIABLE]

NOTES:
_________________________________________________
_________________________________________________
_________________________________________________
```

---

## Decision Criteria

### Go / No-Go Decision

| Result | Decision |
|--------|----------|
| ≥50g at 4mm with single coil | **GO** - Single electromagnet per key |
| 25-50g at 4mm | **GO** - Use dual electromagnets per key |
| 15-25g at 4mm | **MAYBE** - Needs optimization (bigger magnet, more current) |
| <15g at 4mm | **NO-GO** - Insufficient force, use solenoid approach |

### Optimization Options (if borderline)

1. **Larger neo magnet** - 5mm or 6mm diameter
2. **Higher current** - Thicker wire, bigger power supply
3. **Better core material** - Pure soft iron vs steel bolt
4. **Core extension** - Extend core closer to magnet
5. **Smaller gap** - Modify key/mount to reduce gap to 3mm

---

## Safety Notes

1. **Electromagnet gets hot** - Don't leave powered continuously
2. **Neodymium magnets are brittle** - Don't let them snap together
3. **Magnetic fields** - Keep away from credit cards, pacemakers
4. **Current** - 2-3A can cause burns if shorted through skin

---

## Next Steps After Testing

### If Test Passes:

1. Document final coil specifications
2. Update main spec with electromagnet approach
3. Design final mounting system
4. Order materials for full build

### If Test Fails:

1. Document what didn't work
2. Consider optimization options
3. If still not viable, proceed with solenoid approach

---

## Appendix: Troubleshooting

| Issue | Possible Cause | Solution |
|-------|----------------|----------|
| No attraction at all | Coil winding open | Check continuity |
| Very weak force | Too few turns | Rewind with more turns |
| | Magnet too far | Reduce gap |
| | Wrong magnet pole | Flip magnet |
| Coil gets very hot | Too much current | Increase resistance or reduce voltage |
| | Continuous operation | Use pulsed operation |
| Force inconsistent | Magnet alignment | Ensure centered |
| | Core saturating | Normal at high current |
| Buzzing sound | AC component | Use pure DC |
| | Loose core | Secure core in bobbin |
