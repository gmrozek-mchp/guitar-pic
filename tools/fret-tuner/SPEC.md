# Fret-Tuner Specification

## Overview

Web-based Python tool for developing and refining fret-press detection algorithms. Consumes the same raw ADC data the firmware sees -- either live from the board over UART or replayed from a captured CSV -- runs detection logic in Python, and visualizes everything in an interactive browser UI. Optionally sends actuation commands back to the microcontroller, moving the entire detection and strum-timing pipeline to the PC for rapid iteration.

## Video reference (always-on)

A first-class always-on subsystem. The UI exposes a video viewport at all times; behavior is independent of which detector is active.

**Primary role**: time-aligned visual reference and recording against the ADC sample timeline and any derived signals (waveforms, detector state, fret/strum events). The question it answers: does what was on screen at time *t* match what the ADC and algorithms reported at *t*?

**Secondary roles**: debugging alignment between capture and charts, reviewing a session after the fact, documenting false positives/negatives.

### Requirements

- **Primacy**: video reference is a first-class always-on subsystem, not gated on detector choice.
- **Source ownership**: the fret-tuner **backend** owns the camera. Exactly one source feeds the reference ring buffer at a time. The source is selectable in the UI from cameras enumerated by the backend, plus a "none" option, plus any video file supplied for replay.
- **Buffer**: server-side ring buffer with **≥30 s wall-clock coverage** at a **target 30 Hz** capture rate. The buffer accepts whatever rate the camera delivers, sized to retain ≥30 s even when the source runs as fast as 60 Hz.
- **Synchronization**: every buffered frame carries a sample-clock timestamp anchored to the ADC stream so that any time *t* in the chart timeline maps to the closest buffered frame.
- **Live behavior**: while playing, the browser shows the most recent server frame with overlays composed for that frame's *t*.
- **Paused / scrubbing**: when paused, or while panning/zooming the charts, the video display follows the chart cursor / center time, picking the closest buffered frame.
- **Overlays**: per-frame composition (live and paused) at the display path's natural rate. Required minimum set:
    - 5 fret **pressed-state** indicators (per ADC channel)
    - **strum state** indicators (down + up, sourced from the actuator pipeline)
    - **scrub-time** text cursor
  Detectors may register additional overlay layers via the extension hook (see [Detection Algorithm Contract → Overlay extension](#overlay-extension)).
- **Detector consumption**: detectors that need pixel data (e.g. `detect_video`) **read from the reference buffer**; they do not own a separate camera grab.

## Actuation (always-on)

A first-class always-on subsystem that owns the GPIO command serial link to the actuator microcontroller. Behavior is independent of which detector is active and of where the ADC data comes from (live serial, CSV replay, or none).

**Primary role**: consume detector state each tick, run the strum timing pipeline (chord window, FIFO scheduling, fret early/late, strum pulse), and emit a 1-byte GPIO bitmask to the actuator board over UART.

### Requirements

- **Primacy**: actuation is a first-class always-on subsystem, not a side effect of opening a data port. It can be enabled with any detector and any data source (including CSV-only replay against a live actuator board).
- **Source ownership**: the actuator owns its own `serial.Serial` handle. The port is selectable in the UI from ports enumerated by the backend, plus a "none" option.
- **Port sharing**: the actuator port may name the same `/dev/cu.*` device as an ADC data port (single-board case). In that case the backend opens the device once and shares the handle between the data reader and the actuator writer; the writer never blocks on the reader and vice versa.
- **Reconnect**: when the actuator port is lost (unplug, USB sleep, etc.) the writer goes silent without raising; on reconnect, output resumes. Auto-reconnect runs at the same cadence as the data-port reconnect.
- **Enable gate**: a UI toggle gates output at the *bitmask* level. When disabled, the timing pipeline still runs (and reports `output_mask = 0`), but no bytes are written. Manual GPIO override (`manual_output`) still works when actuation is disabled, so individual buttons can be poked from the browser without engaging the full pipeline.
- **Always-on lifecycle**: created at server startup, persists across detector switches, never owned by a detector.

## Architecture

The backend is organized around two **always-on subsystems** (video reference, actuation) and one swappable **detector subsystem**. Always-on subsystems own their I/O and run regardless of which detector is active. The detector consumes whatever inputs it needs (ADC stream, video frames, etc.), produces detection state, and feeds it to the actuator.

```
Browser (localhost:8080)
  ├── Video viewport (live or scrubbed frame + composited overlays)
  ├── uPlot charts (zoom, pan, pause)
  ├── Controls (camera source, actuator port, detector, params, recording)
  └── WebSocket  (sample data, detector state, sources, actuator status)
        ↕
FastAPI backend (Python)
  ├── camera.py          (OpenCV capture → shared ring buffer, ≥30 s @ ≥30 Hz)   [always-on]
  ├── actuator.py        (strum timing pipeline + GPIO serial TX)                 [always-on]
  ├── stream.py          (serial/CSV ADC source for ADC-based detectors)
  ├── detect_*.py        (consume samples and/or video frames; emit overlay layers)
  └── server.py          (WebSocket + HTTP video transport, subsystem wiring)
        ↕
Microcontrollers (PIC32CM)
  ├── Data board   →  fret_scan + data_stream     (12-byte ADC frames over UART)
  └── Actuator board → cmd_receive                (1-byte bitmask → GPIO assert/release)
                       The data board and actuator board may be the same physical
                       device sharing one UART, or two different devices.
```

Browser and backend are deployed as a split / resizable layout: video viewport and ADC charts are co-equal, neither dominant by default. The shared video ring buffer lives in the backend; detector pixel consumers and the browser display both read from it.

## File Layout

```
tools/fret-tuner/
    fret-tuner.py          # CLI entry point (launches server, opens browser)
    server.py              # FastAPI app, WebSocket, data pipeline wiring
    camera.py              # OpenCV capture + shared video ring buffer
    index.html             # Browser UI (uPlot charts + video viewport, no build step)
    stream.py              # Serial + CSV data sources
    actuator.py            # Strum timing pipeline (port of fret_button.c)
    detect_threshold.py    # Detection: fixed per-channel hysteresis
    detect_trough.py       # Detection: baseline + trough FSM
    detect_slope.py        # Detection: EMA + slope FSM with re-press handling
    detect_video.py        # Detection: pixel sampling against the reference buffer
    video_calibrate.py     # Standalone helper for marking detect_video sensor coords
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

### Serial Auto-Reconnect

When the USB serial connection is lost (board unplugged, USB sleep, etc.), the feed loop catches the error, closes the stale serial handle, and retries opening the port every 2 seconds until the device reappears. On disconnect and reconnect, the server pushes `serial_status` messages over WebSocket so the browser UI can display the current state (amber "Serial disconnected -- reconnecting..." while down, green "Connected" on recovery). CSV replay sources are not affected.

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

The server auto-injects `"raw"` (the raw ADC reading for that channel) into each per-channel state dict before charting, so detectors do not need to copy it themselves.

### Chart schema

Detectors describe what their five chart slots should display via:

```python
class Detector:
    def chart_schema(self) -> list[dict]:
        """Return 5 slot specs:
        {
            "id":      str,             # state-dict key (e.g. "green")
            "label":   str,             # axis label
            "color":   "#rrggbb",
            "y_range": [min, max],
            "series":  [
                {"key": str, "label": str, "color": "#rrggbb",
                 "width": float, "dash": [int, int] | None},
                ...
            ],
        }
        """
```

Each `series.key` names a field inside that channel's state dict. `stream.adc_chart_schema(extra_series=[...])` builds the conventional `raw + <extras>` layout for ADC detectors. Detectors with no use for raw ADC (e.g. `detect_video`) simply omit the `"raw"` series.

The browser rebuilds its chart slots whenever a new schema arrives, so switching detectors swaps the chart contents (labels, colors, y-range, plotted series) along with the algorithm.

### Overlay extension

Detectors may optionally contribute named overlay layers to the video viewport:

```python
class Detector:
    def overlays(self) -> list[OverlayLayer]:
        """Optional. Return list of overlay layers to draw on the video.
        Each layer = {name: str, draw(ctx, frame_meta, sample_state) -> None}.
        Returning [] (or omitting the method) means no detector-specific overlays.
        """
        return []
```

Layers compose on top of the always-on minimum set (fret pressed-state, strum state, scrub time). Transport for overlay drawcalls is implementation-defined: the server may bake them into delivered frames, or stream drawcall metadata (see [WebSocket Protocol](#websocket-protocol) `video_meta`) for client-side compositing.

`detect_video`, for example, registers a layer that draws its sensor sample points and per-button signal levels.

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

- 5 stacked uPlot time-series charts, one per fret slot (green, red, yellow, blue, orange)
- The active detector's `chart_schema()` defines the series, labels, colors, and y-range for each slot. ADC detectors plot raw ADC + their baseline / threshold lines; `detect_video` plots its hold + edge signals (no raw ADC). Switching detectors swaps the chart contents.
- Shaded press bands and (where the detector emits `press_count`) re-press tick marks are overlaid in the slot color
- Native mouse-drag zoom, scroll-wheel zoom, double-click to reset

### Controls (sidebar)

- **Pause / Resume** -- freezes chart updates; data continues to buffer server-side
- **Window width** -- slider controlling visible time range (0.5--30 seconds)
- **Detector dropdown** -- switch between available `detect_*.py` modules live
- **Parameter sliders** -- dynamically generated from the active detector's `default_params()`, changes take effect immediately
- **Camera source dropdown** -- enumerated cameras + "none"; controls the always-on video reference
- **Actuator port dropdown** -- enumerated `/dev/cu.*` (or platform equivalent) serial ports + "none"; controls the always-on actuator. Selecting the same device as a live ADC data port shares the handle.
- **Actuation toggle** -- enable/disable writing command bytes (the timing pipeline runs either way)
- **Timing sliders** -- strum delay, fret early, strum pulse, chord window
- **Connection status** indicators -- separate badges for the data port and the actuator port

### Layout

Split / resizable two-pane layout. The **video viewport** and the **ADC charts** are co-equal first-class panes; neither is dominant by default and the user can resize the split. Sidebar controls remain to one side.

### Video viewport

Backed by the always-on subsystem in [Video reference (always-on)](#video-reference-always-on). UI surface:

- **Source selector** -- lists cameras enumerated by the backend, plus "none" and any `--video FILE` replay file. Selection persisted by the backend, applied immediately.
- **Recording controls** -- start / stop / save buffer (writes paired video + ADC log; see [Recording & paired CSV replay](#recording--paired-csv-replay)). Default off.
- **Pause / scrub** -- synchronized with the charts' pause; chart center time drives the displayed frame.
- **Overlays** -- always-on minimum (5 fret pressed-state indicators, strum state, scrub time) plus any layers the active detector registers via [Overlay extension](#overlay-extension).

### Recording & paired CSV replay

- A live session can be **recorded**: the backend drains the reference ring buffer to a video file alongside the ADC sample log (CSV), with a shared time anchor written into the recording so the two replay in sync.
- `python fret-tuner.py --csv FILE [--video FILE]` replays both. If `--video` is omitted, the video viewport shows "no recorded video for this CSV" and disables source selection for the duration of the replay.
- Recording is started/stopped explicitly from the sidebar; default off.

### WebSocket Protocol

Sample data, detector state, and overlay metadata flow over the WebSocket; raw video frames are delivered over a parallel HTTP video transport (implementation choice; the existing tool uses MJPEG). Both share the same sample-clock timestamps so chart and video scrubbing target the same *t*.

#### Master clock: the displayed video frame

The currently displayed video frame is the single source of truth for *now*. Each captured frame has a `seq` (monotonic id) and a `t` (sample-clock timestamp), and every UI element (chart cursor, viewport scroll, overlay text) aligns to the *currently displayed* frame's `t` -- not to the latest ADC sample, the wall clock, or the network arrival time.

- **Live mode**: the server emits a `frame_tick` message on every buffer write (~30 Hz). The browser updates `currentFrame = {seq, t}` and re-clips the chart's leading edge to that `t`. ADC samples that arrived *after* the latest displayed frame are buffered but not yet visible; they are revealed on the next tick. Chart movement is therefore quantized at the camera frame rate, which is the trade-off we accept to keep video, charts, and overlays exactly aligned.
- **Pause / scrub**: `frame_tick` is ignored. The user moves `currentFrame` via the timeline, frame-step buttons, or slow-mo playback. Each scrub action returns from the server with a `{seq, t}` pair, which the browser writes back into `currentFrame`.
- **CSV / no-camera fallback**: when no camera is attached, no `frame_tick` is emitted. The browser falls back to the latest ingested ADC sample timestamp as `currentFrame.t`, with `seq=null`.

Server → client:

```json
{"type": "data", "samples": [...], "state": [...], "actuator_mask": 0}
{"type": "frame_tick", "seq": 1234, "t": 12.345}          // ~30 Hz, camera only
{"type": "detector", "name": "...", "params": {...}, "available": [...],
 "timing": {...}, "actuate_enabled": false}
{"type": "serial_status", "connected": true}              // data port
{"type": "actuator_status", "connected": true,
 "available": [...], "active": "/dev/cu.usbmodem21202"}   // actuator port
{"type": "video_meta", "t": 12.345, "layers": [...]}
{"type": "sources", "available": [...], "active": "..."}  // camera sources
{"type": "recording", "active": false, "path": null}
```

Client → server:

```json
{"type": "pause"}
{"type": "resume"}
{"type": "set_params", "params": {"KEY": value}}
{"type": "set_detector", "name": "detect_trough"}
{"type": "set_timing", "params": {"STRUM_DELAY_MS": 200}}
{"type": "set_actuate", "enabled": true}
{"type": "set_source", "name": "..."}            // camera source
{"type": "set_actuator_port", "name": "/dev/cu.usbmodem21202"}  // or "none"
{"type": "manual_output", "mask": 0}
{"type": "record_start"}
{"type": "record_stop"}
```

The backend also exposes:

- `GET /api/sources` -- camera sources (mirror of the WS `sources` message)
- `GET /api/serial-ports` -- enumerated serial ports for the actuator dropdown
- `GET /video` -- live MJPEG of the working-resolution buffer (server-rendered overlays applied)
- `GET /video/snap?t=<sec>` -- single JPEG of the buffered frame closest to *t*. Response carries `X-Frame-Seq` and `X-Frame-Time` headers (and a matching `Access-Control-Expose-Headers`) so the browser can sync `currentFrame` to the snapped frame even when the snapped *t* differs from the requested *t*.
- `GET /video/step?dir=next|prev&n=N&{seq=S | t=T}` -- walk the ring buffer to the n-th next/prev frame. Address by `seq` for exact stepping (preferred when the browser holds a known frame); fall back to `t` for the first scrub when seq is unknown. Returns `{"seq": int, "t": float, "buffered": int}`.

## CLI Reference

```
usage: fret-tuner.py [-h] (--port PORT | --csv FILE) [--video FILE]
                     [--actuator-port PORT] [--camera ID]
                     [--detector MODULE] [--param KEY=VALUE] [--fast]
                     [--host HOST] [--web-port PORT]
```

`--port` selects the **data** serial port (ADC frame source). `--actuator-port` selects the **actuator** serial port; if omitted, the actuator starts idle and a port can be chosen at runtime from the sidebar dropdown. If `--actuator-port` names the same device as `--port`, the backend opens it once and shares the handle for read and write.

### Examples

```bash
# Single board: ADC stream + actuator on the same port (handle is shared)
python fret-tuner.py --port /dev/cu.usbmodem21202 \
    --actuator-port /dev/cu.usbmodem21202

# Two boards: ADC stream on one device, actuator on another
python fret-tuner.py --port /dev/cu.usbmodem21202 \
    --actuator-port /dev/cu.usbmodem31301

# CSV replay against a live actuator board
python fret-tuner.py --csv session.csv --actuator-port /dev/cu.usbmodem31301

# CSV replay, no actuator (UI dropdown can attach one later)
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
- `opencv-python` (server-side video capture and `detect_video`)

## Bundled Detector: detect_video

Pixel-sampling detector that **consumes the shared reference video buffer**. It does not own a camera and does not serve its own video stream; sensor sample points and per-button signal levels are exposed as an [overlay layer](#overlay-extension) for the viewport.

Two sensor points per button, both above the strike line:

- **Sensor 1 (hold)** -- brightness detect (color OR white). Sets `pressed=True` whenever max channel brightness exceeds `THRESHOLD`.
- **Sensor 2 (edge)** -- color-filtered leading-edge detect. Increments `press_count` on each new note arrival for strum timing.

Detection thresholds are applied directly to raw pixel values (dark background is implicit zero, no settle/calibration phase).

### Observed Game Colors (camera-captured BGR)

Colors are highly saturated with negligible off-channel values:

| Color  | B    | G    | R    | Notes |
|--------|------|------|------|-------|
| Red    | <10  | <10  | >100 | Almost completely red |
| Green  | <10  | >100 | <10  | Completely green |
| Blue   | >100 | ~67  | <10  | B dominant, G about 2/3 of B, low R |
| Yellow | <10  | ~100 | ~100 | Even mix of G and R, very low B |
| Orange | <10  | ~50  | >100 | R dominant, G about half of R, low B |
| White (fret bar) | ~160 | ~160 | ~160 | Equal channels at brightest, sat near 0 |

The edge detector uses a color filter with saturation scaling `(max-min)/max` to reject white/gray fret bars regardless of camera white balance.

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `THRESHOLD` | 50 | Detection threshold (brightness for hold, color signal for edge) |
| `RELEASE_FRAC` | 60 | Release threshold as % of press threshold (hysteresis) |
| `PATCH_RADIUS` | 2 | Pixel averaging radius around sample point |
| `{COLOR}_X/Y` | varies | Hold sensor pixel coordinates per button |
| `{COLOR}_EX/EY` | varies | Edge sensor pixel coordinates per button |

Camera selection lives in the always-on video subsystem, not the detector; there is no `DEVICE_ID` parameter here. Sensor markers are drawn through the `overlays()` hook, replacing the previous `SHOW_PREVIEW`/`/video` path.

### Calibration

Use `video_calibrate.py` to interactively mark sensor positions:

```bash
# Full calibration (10 points: 5 hold + 5 edge)
python video_calibrate.py --device 0

# Color sampling mode (inspect BGR values without calibrating)
python video_calibrate.py --sample
```

## Future Work

- **Detector-owned data sources**: lift the ADC stream out of the top-level CLI into the detector itself, so each detector declares what inputs it needs (ADC serial / CSV / camera frames / nothing). The runtime UI would then expose a per-detector source picker. (Chart schemas already follow this model: each detector returns a `chart_schema()` and the browser rebuilds the 5 chart slots from it.) The current scope keeps `--port` / `--csv` as a top-level concern.
- Multi-detector overlay (compare two algorithms side-by-side on the same chart)
- Per-channel enable/disable toggles for actuation
- Latency measurement display (round-trip time from ADC sample to GPIO assertion)
- Multi-camera reference (more than one source recorded and replayable in sync)
