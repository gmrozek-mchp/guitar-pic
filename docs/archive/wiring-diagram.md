# Wiring Diagram

## Overview

This document describes the electrical connections between all components of the Guitar Hero Bot.

---

## System Block Diagram

```
                                    +12V
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    │    ┌────────────┴────────────┐    │
                    │    │      Power Supply       │    │
                    │    │         12V 5A          │    │
                    │    └────────────┬────────────┘    │
                    │                 │                 │
                    │    ┌────────────┴────────────┐    │
                    │    │    5V Regulator (2A)    │    │
                    │    └────────────┬────────────┘    │
                    │                 │                 │
        ┌───────────┼─────────────────┼─────────────────┼───────────┐
        │           │                 │                 │           │
   ┌────┴────┐ ┌────┴────┐      ┌─────┴─────┐     ┌─────┴─────┐     │
   │Solenoids│ │Solenoids│      │    PIC    │     │  Servos   │     │
   │ (Frets) │ │ (Strum) │      │  18F4550  │     │  (x2)     │     │
   │  x5     │ │   x2    │      │           │     │           │     │
   └────┬────┘ └────┬────┘      └─────┬─────┘     └─────┬─────┘     │
        │           │                 │                 │           │
        └───────────┴────────┬────────┴─────────────────┘           │
                             │                                      │
                    ┌────────┴────────┐                             │
                    │   ULN2803A or   │                             │
                    │   MOSFET Array  │◄────────────────────────────┘
                    └─────────────────┘                          GND
```

---

## PIC18F4550 Pin Assignments

### Recommended Pinout

| Pin | Name | Function | Connection |
|-----|------|----------|------------|
| 1 | MCLR | Reset | 10K pull-up to VCC, reset button to GND |
| 2 | RA0 | Digital Out | Fret 1 (Green) → ULN2803 IN1 |
| 3 | RA1 | Digital Out | Fret 2 (Red) → ULN2803 IN2 |
| 4 | RA2 | Digital Out | Fret 3 (Yellow) → ULN2803 IN3 |
| 5 | RA3 | Digital Out | Fret 4 (Blue) → ULN2803 IN4 |
| 6 | RA4 | Digital Out | Fret 5 (Orange) → ULN2803 IN5 |
| 7 | RA5 | Digital Out | Strum Down → ULN2803 IN6 |
| 8 | RE0 | Digital Out | Strum Up → ULN2803 IN7 |
| 11 | VDD | Power | +5V |
| 12 | VSS | Ground | GND |
| 13 | OSC1 | Oscillator | 20MHz Crystal + 22pF cap |
| 14 | OSC2 | Oscillator | 20MHz Crystal + 22pF cap |
| 15 | RC0 | PWM | Whammy Servo Signal |
| 16 | RC1 | PWM | Tilt Servo Signal |
| 23 | USB D- | USB | USB Connector D- |
| 24 | USB D+ | USB | USB Connector D+ |
| 25 | RC6/TX | UART TX | (Alternative to USB) |
| 26 | RC7/RX | UART RX | (Alternative to USB) |
| 31 | VSS | Ground | GND |
| 32 | VDD | Power | +5V |

---

## Solenoid Driver Circuit

### Using ULN2803A (Recommended)

```
                         +12V (Solenoid Power)
                           │
              ┌────────────┤
              │            │
         ┌────┴────┐  ┌────┴────┐
         │Solenoid │  │ Flyback │
         │  Coil   │  │  Diode  │
         └────┬────┘  └────┬────┘
              │            │
              └──────┬─────┘
                     │
                     │ (COM pin 10 of ULN2803)
         ┌───────────┴───────────┐
         │       ULN2803A        │
         │                       │
    IN1 ─┤1                   18├─ OUT1 ──► Solenoid 1
    IN2 ─┤2                   17├─ OUT2 ──► Solenoid 2
    IN3 ─┤3                   16├─ OUT3 ──► Solenoid 3
    IN4 ─┤4                   15├─ OUT4 ──► Solenoid 4
    IN5 ─┤5                   14├─ OUT5 ──► Solenoid 5
    IN6 ─┤6                   13├─ OUT6 ──► Solenoid 6
    IN7 ─┤7                   12├─ OUT7 ──► Solenoid 7
    IN8 ─┤8                   11├─ (unused)
    GND ─┤9                   10├─ COM (+12V)
         └───────────────────────┘
              │
         From PIC GPIO
```

### Using Discrete MOSFETs

```
        +12V
          │
     ┌────┴────┐
     │Solenoid │
     │  Coil   │
     └────┬────┘
          │◄──────┐ Flyback Diode (1N4007)
          │       │ (Cathode to +12V)
          │───────┘
          │
     ┌────┴────┐
     │  DRAIN  │
     │ IRLZ44N │
     │  GATE   │◄─── 1K resistor ◄─── PIC GPIO
     │ SOURCE  │
     └────┬────┘
          │
         GND
```

---

## Servo Connections

```
         +5V (Regulated)
          │
          ├──────────────────────┐
          │                      │
     ┌────┴────┐            ┌────┴────┐
     │  Servo  │            │  Servo  │
     │ (Whammy)│            │ (Tilt)  │
     │ Red:VCC │            │ Red:VCC │
     │Brn:GND  │            │Brn:GND  │
     │Org:Sig  │            │Org:Sig  │
     └─┬───┬───┘            └─┬───┬───┘
       │   │                  │   │
       │   └──► PIC RC0 (PWM) │   └──► PIC RC1 (PWM)
       │                      │
      GND                    GND
```

**PWM Specifications**:
- Frequency: 50Hz (20ms period)
- Pulse width: 1ms (0°) to 2ms (180°)

---

## Power Distribution

```
     AC Input
         │
    ┌────┴────┐
    │  12V    │
    │  5A     │
    │  PSU    │
    └────┬────┘
         │
    ┌────┴────┐ +12V Bus
    │    │    │
    │    │    └──────────────────► ULN2803 COM (Pin 10)
    │    │
    │    │    ┌─────────────────┐
    │    └────┤ 5V Regulator    │
    │         │ (LM7805 or Buck)│
    │         └────────┬────────┘
    │                  │
    │             ┌────┴────┐ +5V Bus
    │             │    │    │
    │             │    │    └──► PIC VDD (Pins 11, 32)
    │             │    │
    │             │    └────────► Servos VCC
    │             │
    │            ═══ 100µF (smoothing)
    │             │
    └─────────────┴─────────────► GND Bus
```

**Notes**:
- Use thick wires (18-20 AWG) for solenoid power
- Add 100µF capacitor near ULN2803 for transient handling
- Keep 5V logic ground connected to 12V ground

---

## USB Connection (if using USB instead of UART)

```
    USB-B Connector
    ┌─────────────┐
    │  1: VCC     │──► (Can power PIC, or leave unconnected)
    │  2: D-      │──► PIC Pin 23 (USB D-)
    │  3: D+      │──► PIC Pin 24 (USB D+)
    │  4: GND     │──► GND
    └─────────────┘
```

---

## Complete Wiring Checklist

### Power
- [ ] 12V PSU connected to driver COM
- [ ] 5V regulator installed and outputting correctly
- [ ] All grounds connected together
- [ ] Capacitors installed for filtering

### PIC Microcontroller
- [ ] 5V and GND connected
- [ ] Crystal and capacitors installed (if external oscillator)
- [ ] MCLR pull-up resistor installed
- [ ] ICSP header for programming (optional)

### Solenoids
- [ ] All 7 solenoids connected to driver outputs
- [ ] Flyback diodes installed (check polarity!)
- [ ] Driver inputs connected to PIC GPIO
- [ ] 12V connected to driver COM

### Servos
- [ ] VCC connected to 5V regulated
- [ ] GND connected
- [ ] Signal wires to PIC PWM pins

### Communication
- [ ] USB cable connected (or UART TX/RX wired)
- [ ] Driver installed on PC (if needed)

---

## Safety Notes

1. **Flyback diodes are critical** - Solenoids will damage the driver without them
2. **Check polarity** before powering on
3. **Don't exceed current ratings** - ULN2803 is 500mA per channel
4. **Heat sinking** may be needed for continuous operation
5. **Disconnect power** before making wiring changes
