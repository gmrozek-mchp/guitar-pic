# Fretboard Firmware Specification

## Overview

Standalone firmware for PIC32CM6408PL10048 (Cortex-M0+, 24 MHz) that detects Guitar Hero notes using phototransistor light sensors and actuates a physical guitar controller's buttons via open-drain GPIO outputs. The system operates without a PC -- the microcontroller handles detection, timing, and actuation entirely on its own.

## Hardware

### Target

| Spec | Value |
|------|-------|
| Device | PIC32CM6408PL10048 |
| Core | ARM Cortex-M0+ |
| Clock | 24 MHz |
| Toolchain | XC32 5.10 |
| Programmer | PKOB nano |

### Pin Map

#### Sensor Inputs (ADC)

Five phototransistors positioned above the TV strike line. Each produces an analog voltage that drops when a note passes the sensor. ADC reads 12-bit unsigned (0-4095); lower values indicate note presence.

| Channel | ADC Input | Pin |
|---------|-----------|-----|
| Green | AIN28 | PA28 |
| Red | AIN16 | PA16 |
| Yellow | AIN19 | PA19 |
| Blue | AIN27 | PA27 |
| Orange | AIN26 | PA26 |

#### Button Outputs (Open-Drain GPIO)

Each button output is normally tri-stated (input mode). To "press" a button the pin is driven low (clear + output-enable). To "release" it the pin returns to input mode, which floats the line and lets the guitar controller's own pull-up restore the idle state.

| Function | Pin |
|----------|-----|
| Green fret | PA06 |
| Red fret | PA05 |
| Yellow fret | PA07 |
| Blue fret | PA04 |
| Orange fret | PA01 |
| Strum up | PA00 |
| Strum down | PA03 |

#### Other

| Function | Pin | Notes |
|----------|-----|-------|
| SW0 (enable toggle) | PB03 | Active-low pushbutton, 30 ms debounce |
| LED0 (status) | PB02 | On when enabled, blinks on strum |
| CDC TX (UART) | PB00 | SERCOM1, debug data stream |
| CDC RX (UART) | PB01 | SERCOM1 |

## Software Architecture

### Processing Pipeline

The main loop runs at 500 Hz (2 ms interval) using SYSTICK as the timebase. Each tick executes four stages in sequence, sharing a single `now` timestamp:

```
SYSTICK tick (1 ms resolution)
    |
    v
fret_scan_all()          Blocking ADC read of all 5 channels
    |
    v
fret_detect_update()     Hysteresis threshold comparison, edge flags
    |
    v
fret_button_update(now)  Chord windowing, FIFO scheduling, GPIO output
    |
    v
data_stream_send()       17-byte debug frame over UART
```

### Module Descriptions

#### fret_scan (fret_scan.c / fret_scan.h)

Reads all five ADC channels sequentially using blocking polling. Results are stored in a static array and accessed via `fret_scan_result(channel)`.

The channel enum defines the canonical fret ordering used throughout the firmware:

```
FRET_GREEN = 0, FRET_RED = 1, FRET_YELLOW = 2, FRET_BLUE = 3, FRET_ORANGE = 4
```

#### fret_detect (fret_detect.c / fret_detect.h)

Applies per-channel hysteresis thresholds to determine press/release state. Each channel has independent press and release thresholds to prevent oscillation near the decision boundary:

| Channel | Press (below) | Release (above) |
|---------|---------------|-----------------|
| Green | 2500 | 3000 |
| Red | 2500 | 3000 |
| Yellow | 2200 | 2400 |
| Blue | 2200 | 2500 |
| Orange | 2200 | 2500 |

Provides three query interfaces:
- `fret_is_pressed(ch)` -- current level state
- `fret_detect_new_presses()` -- bitmask of channels that transitioned to pressed since last call (auto-clears)
- `fret_detect_new_releases()` -- bitmask of channels that transitioned to released since last call (auto-clears)

#### fret_button (fret_button.c / fret_button.h)

Translates detected note edges into timed button presses and strum actions on the guitar controller. This is the core game-play logic.

**Enable/disable:** SW0 toggles the system on and off. When disabled, all outputs are released and no detection is processed. LED0 indicates the enabled state.

**Chord windowing:** When the first press edge arrives, a chord accumulation window opens for `CHORD_WINDOW_MS` (40 ms). Any additional press edges within this window are grouped into the same chord. When the window expires, the chord is committed to the pending FIFO.

**Pending chord FIFO:** A circular buffer of up to 4 pending chords, each storing:
- `fret_mask` -- bitmask of frets in the chord
- `press_at` -- tick when fret outputs should be asserted
- `strum_at` -- tick when the strum pulse should fire
- `frets_asserted` -- whether the fret outputs have been set

Chords are processed strictly in FIFO order, which guarantees press-before-strum-before-next-chord without needing priority sorting. If a new chord's scheduled press time would fall during the previous chord's strum pulse, the new chord's times are pushed forward to avoid interference.

**Per-fret release scheduling:** When a fret's detection goes low, a release is scheduled `STRUM_DELAY_MS` into the future. The release only fires if:
1. The scheduled time has arrived
2. The fret is no longer physically detected
3. No pending chord in the FIFO needs the fret

**Strum generation:** Each strum alternates direction (up/down). The strum pin is driven low for `STRUM_PULSE_MS`, then released.

#### data_stream (data_stream.c / data_stream.h)

Sends a 17-byte packed frame over SERCOM1 UART for use with Microchip Data Visualizer's Data Streamer protocol. Each frame contains all 5 raw ADC values (uint16) and all 5 pressed states (uint8), bracketed by start (0x03) and end (0xFC) bytes.

The frame is only sent if the UART transmit buffer has room; otherwise the call is silently skipped.

### Timing Constants

All timing is in milliseconds, measured via `SYSTICK_GetTickCounter()` (1 ms resolution).

| Constant | Value | Purpose |
|----------|-------|---------|
| `PROCESS_INTERVAL_MS` | 2 | Main loop period (500 Hz) |
| `STRUM_DELAY_MS` | 200 | Delay from chord commit to strum -- compensates for sensor placement above strike line |
| `FRET_EARLY_MS` | 50 | Fret output asserted this many ms before strum |
| `STRUM_PULSE_MS` | 50 | Duration the strum button is held low |
| `CHORD_WINDOW_MS` | 40 | Window for grouping simultaneous notes into a chord |
| `SW0_DEBOUNCE_MS` | 30 | Debounce time for enable/disable button |

Compile-time guards enforce:
- `STRUM_DELAY_MS > FRET_EARLY_MS`
- `STRUM_DELAY_MS > STRUM_PULSE_MS`

### Timing Diagram

Single note detected at T=0, chord window closes at T=40:

```
T=0      T=40              T=190             T=240        T=290
|         |                  |                 |            |
detect    commit             assert fret       strum down   strum release
          chord              output (low)      (low pulse)  (tri-state)
          window                               50ms pulse
          closes
          |--- STRUM_DELAY_MS (200) --------->|
                             |-- FRET_EARLY --|
                                   (50)
```

Two-note chord (green + red arrive 20 ms apart):

```
T=0      T=20     T=40     T=190             T=240
|         |        |        |                  |
green     red      commit   assert green+red   strum
detect    detect   chord
```

## Build System

MPLAB Extensions for VS Code. Project config is in `.vscode/fretboard.mplab.json`. Source files are listed in the `fileSets[0].files` array. MCC/Harmony-generated code lives under `fretboard-mcc/` and should not be edited.

## File Map

| File | Lines | Purpose |
|------|-------|---------|
| main.c | ~38 | Init + 500 Hz processing loop |
| fret_scan.c / .h | ~36 / ~24 | ADC channel scanning |
| fret_detect.c / .h | ~60 / ~32 | Hysteresis detection + edge flags |
| fret_button.c / .h | ~311 / ~33 | Chord FIFO, button/strum output, SW0 |
| data_stream.c / .h | ~52 / ~15 | UART debug frame output |
| fretboard-mcc/ | (generated) | MCC Harmony peripheral libraries |
