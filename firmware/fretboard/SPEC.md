# Fretboard Firmware Specification

> **Re-scoping (2026-06-16):** fretboard is becoming a phototransistor **detector** node on the
> T1S bus — its Wii-guitar **actuator** role (`cmd_receive.c` → open-drain GPIO) has moved to the
> new [`guitar`](../guitar/SPEC.md) subproject, which is now **proven end-to-end** (marvin drives
> it over T1S). The fretboard firmware below is **unchanged** — it still does both roles and still
> talks UART — and keeps actuating until it is itself moved onto a T1S detector node (id 1). The
> detector/actuator node-class model lives in the top-level
> [`SPEC.md`](../../SPEC.md) §2. (Edge-ai `MODEL_DRIVEN` is unaffected for now.)

## Overview

Firmware for PIC32CM6408PL10048 (Cortex-M0+, 24 MHz) that acts as an
**I/O bridge** between a Guitar Hero controller and an off-board host.
Five phototransistors above the TV strike line are sampled and streamed
out continuously; the host runs detection / chord / strum logic and
sends back a single-byte bitmask that drives the controller's button
GPIOs directly.

In its `MARVIN_DRIVEN` mode the firmware contains **no game logic** — it only
scans ADCs, emits frames, and applies received bitmasks. A second build-time
mode, **`MODEL_DRIVEN`** (`FRETBOARD_MODE` in `main.c`, the current default),
makes the board standalone: an on-device int8 neural net (`model_infer.c`,
weights in the generated `model_weights.h`) maps the ADC window to the button
bitmask itself, no host required. SW0 toggles it; LED0 shows the state.
Inference runs in the main loop; the 240 Hz TC0 ISR only samples and applies.
See [`docs/journal.md`](docs/journal.md) and edge-ai
[`runtime.md`](../../tools/edge-ai/docs/runtime.md).

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

Five phototransistors positioned above the TV strike line. Each
produces an analog voltage that drops when a note passes the sensor.
ADC reads 12-bit unsigned (0–4095); lower values indicate note
presence.

| Channel | ADC Input | Pin |
|---------|-----------|-----|
| Green | AIN28 | PA28 |
| Red | AIN16 | PA16 |
| Yellow | AIN19 | PA19 |
| Blue | AIN27 | PA27 |
| Orange | AIN26 | PA26 |

#### Button Outputs (Open-Drain GPIO)

Each button output is normally tri-stated (input mode). To "press" a
button the pin is driven low (clear + output-enable). To "release" it
the pin returns to input mode, which floats the line and lets the
guitar controller's own pull-up restore the idle state.

| Function | Pin | Cmd bit |
|----------|-----|---------|
| Green fret | PA06 | 0 |
| Red fret | PA05 | 1 |
| Yellow fret | PA07 | 2 |
| Blue fret | PA04 | 3 |
| Orange fret | PA01 | 4 |
| Strum down | PA03 | 5 |
| Strum up | PA00 | 6 |

#### UART (SERCOM1)

| Function | Pin |
|----------|-----|
| TX (data stream out) | PB00 |
| RX (command in) | PB01 |

Baud: **500 000**, 8N1.

## Software Architecture

### Processing Loop

`TC0` is configured to fire a periodic callback at **240 Hz**
(≈4.17 ms period). Each tick runs three stages in sequence from the
timer ISR:

```
TC0 callback (240 Hz)
    |
    v
fret_scan_all()          Blocking ADC read of all 5 channels
    |
    v
data_stream_send()       17-byte frame over SERCOM1 TX
    |
    v
cmd_receive_update()     Drains SERCOM1 RX, applies latest bitmask
```

`main()` is just init + idle loop; all work happens in the TC0 ISR.

### Module Descriptions

#### fret_scan ([fret_scan.c](fret_scan.c) / [fret_scan.h](fret_scan.h))

Reads all five ADC channels sequentially using blocking polling.
Results are stored in a static array and accessed via
`fret_scan_result(channel)`.

The channel enum defines the canonical fret ordering used throughout
the firmware:

```
FRET_GREEN = 0, FRET_RED = 1, FRET_YELLOW = 2, FRET_BLUE = 3, FRET_ORANGE = 4
```

#### data_stream ([data_stream.c](data_stream.c) / [data_stream.h](data_stream.h))

Emits one 17-byte little-endian frame per tick over SERCOM1 TX:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 1 | start = `0x03` |
| 1 | 2 | green (uint16) |
| 3 | 2 | red |
| 5 | 2 | yellow |
| 7 | 2 | blue |
| 9 | 2 | orange |
| 11 | 4 | sample_seq (uint32) — monotonic, one per tick |
| 15 | 1 | applied_mask — actuator bitmask driven this scan |
| 16 | 1 | end = `0xFC` (`~start`) |

`sample_seq` lets the host reconstruct true sample order and detect frames
dropped in transit (it advances per tick even when a send is skipped).
`applied_mask` is the bitmask `cmd_receive` currently drives, captured in the
same tick as the scan so sensor and actuator state are paired at the source
(used by edge-ai training-data export). The frame is dropped silently if the
TX free-buffer count is below the frame size. At 240 Hz this is 4 080 B/s —
well within the 50 000 B/s budget at 500 000 baud.

#### cmd_receive ([cmd_receive.c](cmd_receive.c) / [cmd_receive.h](cmd_receive.h))

Drains the SERCOM1 RX buffer each tick. The **last byte received** in
the tick is interpreted as a button bitmask and applied directly:

| Bit | Output |
|----:|--------|
| 0 | Green fret |
| 1 | Red fret |
| 2 | Yellow fret |
| 3 | Blue fret |
| 4 | Orange fret |
| 5 | Strum down |
| 6 | Strum up |

Bit set → drive low (assert). Bit clear → tri-state (release).

Note: only the most recent byte each tick is honoured. The host is
expected to send commands at a rate ≤ the loop rate; older bytes in
the same tick are coalesced away.

## Tools

### Host-side data-stream monitor

[`tools/ds_monitor.py`](tools/ds_monitor.py) opens the serial port,
resyncs on `0x03 … 0xFC` framing, and reports actual frame rate, byte
rate, framing-error count, and current ADC values once per second.

The script declares its dependencies inline (PEP 723) and is run via
[`uv`](https://docs.astral.sh/uv/) — no manual venv or `pip install`
needed:

```
./tools/ds_monitor.py /dev/tty.usbmodem<...>
# or, equivalently:
uv run tools/ds_monitor.py /dev/tty.usbmodem<...>
```

Defaults: `--baud 500000 --expect-hz 240`.

## Build System

MPLAB Extensions for VS Code. Project config is in
`.vscode/fretboard.mplab.json`. Source files are listed in the
`fileSets[0].files` array. MCC/Harmony-generated code lives under
`fretboard-mcc/` and should not be edited.

## File Map

| File | Purpose |
|------|---------|
| [main.c](main.c) | Init + TC0 callback wiring |
| [fret_scan.c](fret_scan.c) / [.h](fret_scan.h) | ADC channel scanning |
| [data_stream.c](data_stream.c) / [.h](data_stream.h) | UART output frame |
| [cmd_receive.c](cmd_receive.c) / [.h](cmd_receive.h) | UART input bitmask |
| [tools/ds_monitor.py](tools/ds_monitor.py) | Host-side frame-rate / content check |
| `fret_detect.c` / `.h` | **Orphaned** (see [docs/journal.md](docs/journal.md)) |
| `fret_button.c` / `.h` | **Orphaned** (see [docs/journal.md](docs/journal.md)) |
| `fretboard-mcc/` | MCC Harmony peripheral libraries (generated) |
