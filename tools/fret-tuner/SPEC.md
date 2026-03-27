# Fret-Tuner Specification

## Overview

Web-based Python tool for developing and refining fret-press detection algorithms. Consumes the same raw ADC data the firmware sees -- either live from the board over UART or replayed from a captured CSV -- runs detection logic in Python, and visualizes everything in an interactive browser UI. Optionally sends actuation commands back to the microcontroller, moving the entire detection and strum-timing pipeline to the PC for rapid iteration.

## Architecture

```
Browser (localhost:8080)
  ├── uPlot charts (zoom, pan, pause)
  ├── Controls (detector, params, actuation)
  └── WebSocket
        ↕
FastAPI backend (Python)
  ├── stream.py        (serial/CSV data source)
  ├── detect_*.py      (pluggable detection algorithms)
  ├── actuator.py      (strum timing pipeline → serial TX)
  └── server.py        (WebSocket + HTTP)
        ↕
Microcontroller (PIC32CM)
  ├── fret_scan         (ADC sampling)
  ├── data_stream       (12-byte frames → UART TX)
  └── cmd_receive       (1-byte bitmask → GPIO assert/release)
```

## File Layout

```
tools/fret-tuner/
    fret-tuner.py          # CLI entry point (launches server, opens browser)
    server.py              # FastAPI app, WebSocket, data pipeline wiring
    index.html             # Browser UI (uPlot charts + controls, no build step)
    stream.py              # Serial + CSV data sources
    actuator.py            # Strum timing pipeline (port of fret_button.c)
    detect_threshold.py    # Detection: fixed per-channel hysteresis
    detect_trough.py       # Detection: baseline + trough FSM
    requirements.txt       # Python dependencies
    SPEC.md                # This file
    .gitignore

firmware/fretboard/
    cmd_receive.c          # UART RX command handler
    cmd_receive.h          # Header
```

## Data Source

### UART Binary Frame (micro → PC)

The fretboard firmware streams 12-byte packed frames at **500 Hz** over SERCOM1 USART at **115200 baud** (8N1):

```
Offset  Size   Field
------  -----  -----
0       1      Start byte (0x03)
1       2      Green   (uint16 LE, 12-bit ADC)
3       2      Red     (uint16 LE)
5       2      Yellow  (uint16 LE)
7       2      Blue    (uint16 LE)
9       2      Orange  (uint16 LE)
11      1      End byte (0xFC)
```

### Command Byte (PC → micro)

Single byte bitmask sent over the same serial connection (full-duplex). Each bit directly controls one GPIO output:

```
Bit   Output
---   ------
0     Green fret
1     Red fret
2     Yellow fret
3     Blue fret
4     Orange fret
5     Strum down
6     Strum up
```

Bit = 1 → assert (drive low). Bit = 0 → release (tri-state). The firmware applies the latest received byte each tick. When no bytes are received, outputs remain unchanged.

### CSV Replay Format

CSV files captured from Microchip Data Visualizer. Expected columns:

```
timestamp,BLUE RAW,BLUE,GREEN RAW,GREEN,ORANGE RAW,ORANGE,RED RAW,RED,YELLOW RAW,YELLOW
```

Only the `timestamp` and `* RAW` columns are consumed.

## Detection Algorithm Contract

Each detector lives in its own `detect_*.py` file and exports a `Detector` class:

```python
class Detector:
    @classmethod
    def default_params(cls) -> dict:
        """Return default parameter values."""

    def __init__(self, **params):
        """Accept tunable parameters as keyword args."""

    def update(self, sample: Sample) -> dict:
        """Process one sample. Return per-channel state dict:
        {
            "green":  {"pressed": bool, "baseline": int, ...},
            ...
        }
        """
```

The `update()` return dict must include `"pressed"` (bool) and `"baseline"` (int) per channel. Additional keys are detector-specific and available for charting.

### Adding a new detector

1. Create `tools/fret-tuner/detect_myalgo.py`
2. Implement a `Detector` class following the contract above
3. Select it from the dropdown in the browser UI (or `--detector detect_myalgo` on the CLI)

The server hot-reloads detector modules on switch, so you can edit the file and switch to it without restarting.

## Bundled Detector: detect_threshold

Fixed per-channel hysteresis. Matches the current firmware `fret_detect.c`.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `GREEN_PRESS` | 2500 | Green press threshold |
| `GREEN_RELEASE` | 2800 | Green release threshold |
| `RED_PRESS` | 2500 | Red press threshold |
| `RED_RELEASE` | 2800 | Red release threshold |
| `YELLOW_PRESS` | 1500 | Yellow press threshold |
| `YELLOW_RELEASE` | 1700 | Yellow release threshold |
| `BLUE_PRESS` | 1500 | Blue press threshold |
| `BLUE_RELEASE` | 1700 | Blue release threshold |
| `ORANGE_PRESS` | 2000 | Orange press threshold |
| `ORANGE_RELEASE` | 2200 | Orange release threshold |

## Bundled Detector: detect_trough

Baseline + trough FSM. Three states: IDLE → DESCENDING → PRESSED.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `MIN_DROP` | 200 | ADC counts below baseline to begin descent tracking |
| `RISE_CONFIRM` | 50 | ADC counts above running minimum to confirm press |
| `RELEASE_MARGIN` | 300 | Counts within baseline to trigger release |
| `BASELINE_DECAY_RATE` | 250 | Samples between 1-count baseline decrements |

## Actuator (actuator.py)

Python port of `firmware/fretboard/fret_button.c`. Translates detector press/release edges into a timed sequence of fret assertions and strum pulses.

### Timing Pipeline

1. **Chord window** -- new presses within `CHORD_WINDOW_MS` are accumulated
2. **Chord commit** -- schedules note assert at `t + STRUM_DELAY_MS - FRET_EARLY_MS` and strum at `t + STRUM_DELAY_MS`
3. **Note FIFO** (cap 8) -- pending fret assertions, processed in order
4. **Strum FIFO** (cap 8) -- pending strum triggers, alternating up/down
5. **Strum pulse** -- held for `STRUM_PULSE_MS`, then released
6. **Delayed releases** -- each released fret is scheduled for release at `t + STRUM_DELAY_MS`, skipped if still needed by pending notes/strums

### Timing Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `STRUM_DELAY_MS` | 220 | Delay from chord commit to strum |
| `FRET_EARLY_MS` | 50 | How early frets assert before strum |
| `STRUM_PULSE_MS` | 50 | How long the strum output is held |
| `CHORD_WINDOW_MS` | 20 | Window for accumulating chord presses |

All timing is driven by sample timestamps, not wall clock, so the pipeline behaves identically in live and CSV replay modes.

### Output

Computes a 7-bit bitmask each tick and writes it to the serial port. Actuation is disabled by default and toggled from the browser UI.

## Firmware: cmd_receive

Minimal command receiver added to the fretboard firmware. Replaces `fret_detect` and `fret_button` in `main.c` -- the microcontroller becomes a scan/stream/actuate relay.

### Main loop (modified)

```c
fret_scan_all();
data_stream_send();
cmd_receive_update();
```

### cmd_receive_update()

1. Checks `SERCOM1_USART_ReadCountGet()` for available bytes
2. Reads all available bytes, keeps the last one
3. Applies the bitmask: for each bit, asserts (drive low + output enable) or releases (input enable) the corresponding GPIO

## Browser UI

Single-file HTML served by the backend at `http://localhost:8080`. Loads uPlot from CDN (no build step).

### Charts

- 5 stacked uPlot time-series charts (one per channel: green, red, yellow, blue, orange)
- Each shows raw ADC waveform + baseline overlay (dashed) + shaded press regions
- Native mouse-drag zoom, scroll-wheel zoom, double-click to reset
- Y axis fixed at 0--4096 (full ADC range)

### Controls (sidebar)

- **Pause / Resume** -- freezes chart updates; data continues to buffer server-side
- **Window width** -- slider controlling visible time range (0.5--30 seconds)
- **Detector dropdown** -- switch between available `detect_*.py` modules live
- **Parameter sliders** -- dynamically generated from the active detector's `default_params()`, changes take effect immediately
- **Actuation toggle** -- enable/disable sending command bytes to the microcontroller
- **Timing sliders** -- strum delay, fret early, strum pulse, chord window
- **Connection status** indicator

### WebSocket Protocol

Server → client:

```json
{"type": "data", "samples": [...], "state": [...]}
{"type": "detector", "name": "...", "params": {...}, "available": [...],
 "timing": {...}, "actuate_enabled": false}
```

Client → server:

```json
{"type": "pause"}
{"type": "resume"}
{"type": "set_params", "params": {"KEY": value}}
{"type": "set_detector", "name": "detect_trough"}
{"type": "set_timing", "params": {"STRUM_DELAY_MS": 200}}
{"type": "set_actuate", "enabled": true}
```

## CLI Reference

```
usage: fret-tuner.py [-h] (--port PORT | --csv FILE) [--detector MODULE]
                     [--param KEY=VALUE] [--fast] [--host HOST]
                     [--web-port PORT]
```

### Examples

```bash
# Live from hardware (opens browser automatically)
python fret-tuner.py --port /dev/cu.usbmodem21202

# CSV replay
python fret-tuner.py --csv ../../firmware/fretboard/fretboard-sample-raw.csv

# Start with trough detector and custom parameters
python fret-tuner.py --port /dev/cu.usbmodem21202 --detector detect_trough \
    --param MIN_DROP=150 --param RISE_CONFIRM=30

# Fast CSV replay (no real-time pacing)
python fret-tuner.py --csv capture.csv --fast
```

## Dependencies

Uses the repo-level `.venv` (Python 3.12). Required packages:

- `pyserial >= 3.5`
- `numpy >= 1.24`
- `fastapi >= 0.115`
- `uvicorn[standard] >= 0.30`
- `websockets >= 12.0`

## Future Work

- Multi-detector overlay (compare two algorithms side-by-side on the same chart)
- Data recording/export from the browser UI
- Per-channel enable/disable toggles for actuation
- Latency measurement display (round-trip time from ADC sample to GPIO assertion)
