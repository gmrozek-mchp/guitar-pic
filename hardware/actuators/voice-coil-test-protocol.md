# Voice Coil Actuator - Test Protocol

## Purpose

Validate the DIY voice coil actuator concept before building all 7 units. This test will confirm that sufficient force can be generated with the proposed magnet and coil configuration.

---

## Test Objectives

1. **Verify magnetic field strength** in the air gap
2. **Measure actual force** vs current
3. **Confirm smooth motion** through full stroke
4. **Assess noise level** during operation
5. **Test response time** 
6. **Evaluate thermal performance** under repeated actuation

---

## Test Materials

### Components to Build One Test Actuator

| Item | Specification | Source | Cost |
|------|---------------|--------|------|
| N42 block magnets | 10×5×3mm (pack of 10) | Amazon | $6 |
| Steel sheet | 2mm thick, ~50×50mm | Hardware store | $3 |
| 30 AWG magnet wire | Small spool (25m) | Amazon | $6 |
| 3D printed parts | Bobbin + housing | Self-print | $1 |
| Cherry MX switch | Any type for fit test | Amazon/spare | $1 |
| **Total test materials** | | | **~$17** |

### Test Equipment

| Item | Purpose |
|------|---------|
| Digital kitchen scale | Measure force (0.1g resolution) |
| Multimeter | Measure resistance, current |
| Variable power supply | 0-5V adjustable, 2A capable |
| Ammeter (or multimeter) | Measure current |
| Ruler/calipers | Measure gap and stroke |
| Oscilloscope (optional) | Measure response time |
| Stopwatch | Time thermal tests |

### Tools

- 3D printer (or order prints)
- Soldering iron
- Fine sandpaper (strip wire enamel)
- Small files
- Cyanoacrylate glue
- Tweezers
- Safety glasses (magnet handling)

---

## Test Fixture Construction

### Build Sequence

Allow ~2 hours for test actuator construction.

#### Step 1: Print Test Parts (30 min print time)

Print bobbin and housing per design document dimensions.

#### Step 2: Prepare Steel Yokes (10 min)

Cut two pieces: 24×10×2mm from steel sheet.

#### Step 3: Assemble Magnet Housing (15 min)

1. Insert bottom yoke into housing
2. Carefully install 4 magnets (check orientation!)
3. Install top yoke

**Magnet orientation check:**
```
    Test with compass or another magnet:
    
    Correct:  N │ gap │ N     (repels across gap)
              S │     │ S
              
    Wrong:    N │ gap │ S     (attracts across gap)
              S │     │ N
```

#### Step 4: Wind Test Coil (20 min)

1. Wind 60 turns of 30 AWG on bobbin
2. Keep tight and even
3. Secure ends with tape
4. Strip enamel from wire ends

#### Step 5: Measure Coil (5 min)

- Resistance should be 3-5Ω
- Check for shorts

#### Step 6: Assemble (10 min)

1. Insert bobbin into housing
2. Verify smooth movement
3. Attach test leads

---

## Test Procedures

### Test 1: Gap Measurement

**Objective:** Verify air gap matches design

**Procedure:**
1. Measure gap between magnet faces with calipers
2. Record actual gap

**Target:** 3.5mm ± 0.3mm

**Record:**
- Measured gap: _______ mm
- Pass/Fail: _______

---

### Test 2: Coil Resistance

**Objective:** Verify coil is wound correctly

**Procedure:**
1. Measure resistance with multimeter
2. Compare to expected value

**Expected:** 3-5Ω for 60 turns of 30 AWG

**Record:**
- Measured resistance: _______ Ω
- Calculated wire length: R / 0.339 Ω/m = _______ m
- Estimated turns: length / 0.056m = _______
- Pass/Fail: _______

---

### Test 3: Force vs Current Measurement

**Objective:** Quantify force output at various currents

**Setup:**
```
                    Fixed support
                         │
        ┌────────────────┴────────────────┐
        │         Voice Coil               │
        │         Housing                  │
        │  (magnets face down)             │
        └────────────────┬─────────────────┘
                         │
                      ┌──┴──┐
                      │Coil │ (bobbin pointing down)
                      │     │
                      └──┬──┘
                         │
                    ┌────┴────┐
                    │  Scale  │
                    │ (tared) │
                    └─────────┘
```

**Procedure:**
1. Mount housing with magnets facing down
2. Insert bobbin with coil
3. Place scale under bobbin
4. Tare scale with coil resting on it
5. Apply current and read force (scale shows negative = pull up)
6. Alternatively: attach string over pulley to measure pull force

**Alternative setup (pull force):**
```
        Fixed mount
             │
        ┌────┴────┐
        │ Housing │
        └────┬────┘
             │
          ┌──┴──┐
          │Coil │
          └──┬──┘
             │
         ────┴──── String
             │
        ┌────┴────┐
        │ Weights │ (add until coil can't lift)
        └─────────┘
```

**Data Table:**

| Voltage (V) | Current (A) | Measured Force (g) | Expected Force (g) |
|-------------|-------------|--------------------|--------------------|
| 0.5 | | | 15 |
| 1.0 | | | 30 |
| 1.5 | | | 45 |
| 2.0 | | | 60 |
| 2.5 | | | 75 |
| 3.0 | | | 90 |
| 4.0 | | | 120 |
| 5.0 | | | 150 |

**Pass criteria:** Measured force within 70% of expected at each point

---

### Test 4: Linearity Check

**Objective:** Verify force scales linearly with current

**Procedure:**
1. Using data from Test 3
2. Plot Force vs Current
3. Should be a straight line through origin

**Analysis:**
- Calculate Force/Current ratio for each point
- Should be approximately constant

**Record:**
- Average F/I ratio: _______ g/A
- Variation: _______ %
- Linear? Yes / No

---

### Test 5: Stroke Test

**Objective:** Verify full 4mm stroke is achievable

**Procedure:**
1. Position bobbin at top of travel (4mm gap)
2. Apply current to pull down
3. Measure actual travel
4. Verify smooth motion through full stroke

**Record:**
- Maximum stroke achieved: _______ mm
- Motion quality: Smooth / Scratchy / Stuck
- Pass/Fail: _______

---

### Test 6: Response Time (Optional)

**Objective:** Measure actuation speed

**Procedure:**
1. Set up oscilloscope to trigger on voltage rising edge
2. Attach position sensor or use photointerruptor
3. Measure time from power-on to full travel

**Alternative (without oscilloscope):**
1. Drive coil with square wave (10Hz)
2. Listen/observe if coil keeps up
3. Increase frequency until motion fails

**Record:**
- Measured response time: _______ ms
- Maximum frequency sustained: _______ Hz
- Pass criteria: <10ms response

---

### Test 7: Noise Assessment

**Objective:** Confirm quiet operation

**Procedure:**
1. Set up actuator to move freely
2. Actuate repeatedly at 5Hz
3. Listen for noise
4. Compare to reference sounds

**Rating scale:**
- Silent: No audible sound
- Very quiet: Faint hum/movement, quieter than key click
- Quiet: Audible but soft
- Moderate: Clearly audible
- Loud: Objectionable

**Record:**
- Noise rating: _______
- Character (hum/click/buzz): _______
- Acceptable? Yes / No

---

### Test 8: Thermal Test

**Objective:** Check for overheating during use

**Procedure:**
1. Actuate 100 times at 50% duty cycle, 2Hz rate (50 seconds)
2. Measure coil temperature immediately after
3. Check for force degradation

**Current setting:** Use current for ~80g force

**Record:**
- Starting temperature: _______ °C (ambient)
- Temperature after test: _______ °C
- Force before: _______ g
- Force after: _______ g
- Pass criteria: <60°C, <10% force loss

---

### Test 9: Durability (Extended, Optional)

**Objective:** Long-term reliability check

**Procedure:**
1. Actuate 1000 times
2. Check for mechanical wear
3. Verify force output unchanged

**Record:**
- Cycles completed: _______
- Visual inspection: _______
- Force after 1000 cycles: _______ g
- Any issues: _______

---

## Results Summary Sheet

```
DATE: _______________
TESTER: _____________

COIL SPECIFICATIONS:
  Wire gauge: 30 AWG
  Turns: _______ (target: 60)
  Resistance: _______ Ω (target: 4Ω)

MAGNET CONFIGURATION:
  Type: N42, 10×5×3mm
  Quantity: 4
  Gap: _______ mm (target: 3.5mm)

TEST RESULTS:

Test 1 - Gap Measurement:      [PASS/FAIL]
  Measured: _______ mm

Test 2 - Coil Resistance:      [PASS/FAIL]
  Measured: _______ Ω

Test 3 - Force vs Current:     [PASS/FAIL]
  Force at 0.5A: _______ g (target: >40g)
  Force at 1.0A: _______ g (target: >80g)

Test 4 - Linearity:            [PASS/FAIL]
  F/I ratio: _______ g/A

Test 5 - Stroke:               [PASS/FAIL]
  Achieved: _______ mm (target: 4mm)

Test 6 - Response Time:        [PASS/FAIL]
  Measured: _______ ms (target: <10ms)

Test 7 - Noise:                [PASS/FAIL]
  Rating: _____________

Test 8 - Thermal:              [PASS/FAIL]
  Max temp: _______ °C

OVERALL RESULT:  [VIABLE / NEEDS WORK / NOT VIABLE]

NOTES:
_________________________________________________
_________________________________________________
_________________________________________________

RECOMMENDED ACTION:
[ ] Proceed to full build (7 units)
[ ] Modify design and retest
[ ] Consider alternative approach
```

---

## Decision Criteria

### Go / No-Go

| Result | Decision |
|--------|----------|
| Force ≥50g at ≤1A, smooth motion, quiet | **GO** - Build all 7 |
| Force 30-50g at 1A | **MAYBE** - Try stronger magnets or more turns |
| Force <30g at 1A | **NO-GO** - Design insufficient |
| Excessive noise or scratchiness | **NO-GO** - Alignment/gap issues |

### Optimization Options (if borderline)

1. **More turns** - Wind 80 instead of 60
2. **Thicker wire** - Use 28 AWG for higher current
3. **Larger magnets** - 12×6×4mm instead of 10×5×3mm
4. **Smaller gap** - 3mm instead of 3.5mm
5. **Better yoke** - Thicker steel, closer fit

---

## Safety Notes

1. **Neodymium magnets can pinch** - Handle carefully, keep apart
2. **Magnets are brittle** - Can shatter if snapped together
3. **Coil can get hot** - Limit continuous operation
4. **Keep magnets away from electronics** - Can damage HDDs, credit cards
5. **Wear safety glasses** - Magnet fragments are dangerous

---

## Next Steps After Testing

### If Test Passes:

1. Document final specifications
2. Order full quantity of materials
3. Set up production jig for consistent assembly
4. Build all 7 actuators
5. Update project spec with voice coil as selected approach

### If Test Needs Modification:

1. Identify specific failure mode
2. Adjust design parameters
3. Build revised test unit
4. Retest

### If Test Fails:

1. Document what didn't work
2. Evaluate if modifications can fix it
3. Consider electromagnet or solenoid alternatives
