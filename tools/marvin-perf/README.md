# marvin-perf

Host-side decoder and analyzer for the marvin firmware **perf-log** wire format.

The firmware emits framed binary records (schema version 6) over its USB-device
CDC ACM port. This tool consumes that stream live, records it to disk, and
replays captures offline. Summary analyses (latency attribution, drop
accounting, stack high-water trends) are produced by the `serve` viewer, not a
standalone CLI verb.

Wire format source-of-truth lives in the firmware tree at
[`firmware/marvin/default/src/perf_log/perf_log_records.h`](../../firmware/marvin/default/src/perf_log/perf_log_records.h).
The Python schema mirror in [`marvin_perf/records.py`](marvin_perf/records.py)
must match it byte-for-byte; bump `EXPECTED_SCHEMA_VERSION` (Python side) and
`PERF_LOG_SCHEMA_VERSION` (firmware side) together.

## Setup

All Python work runs through [**uv**](https://docs.astral.sh/uv/). First-time
bootstrap:

    cd tools/marvin-perf
    uv sync --group viewer   # include viewer deps for the serve command

`uv sync` creates `.venv/` and writes `uv.lock`. Subsequent invocations use the
existing venv automatically. Omit `--group viewer` if you only need the headless
CLI helpers (record, set-mask, set-overlay, snapshot, screendump, export-ml).

## Usage

    # Launch the visual review server (default when no subcommand is given)
    # In live mode the SCORE / 2P SC L / 2P SC R toggles each start one device
    # region slot; hit Record to capture them into the .bin like any other
    # record, then extract them offline with `export-region --kind`.
    uv run marvin-perf
    uv run marvin-perf serve
    uv run marvin-perf serve --host 0.0.0.0 --port 8765
    uv run marvin-perf serve --capture session.bin   # pre-load a capture on startup

    # Capture raw bytes from the marvin USB-device CDC port
    uv run marvin-perf record --port /dev/cu.usbmodem... --out session.bin
    uv run marvin-perf record --port /dev/cu.usbmodem... --out-dir session/   # perf.bin + manifest.json

    # Push a type-mask to a running device without attaching for capture
    uv run marvin-perf set-mask --port /dev/cu.usbmodem... --types ALL

    # Toggle the per-fret target rings on the SENSING strip
    uv run marvin-perf set-overlay --port /dev/cu.usbmodem... --on

    # Capture one full video frame from a running device and save it
    uv run marvin-perf snapshot --port /dev/cu.usbmodem... --out snapshots/

    # Dump a UI framebuffer (Legato canvas surface) — the counterpart to snapshot
    uv run marvin-perf screendump --port /dev/cu.usbmodem... --canvas dash
    uv run marvin-perf screendump --port /dev/cu.usbmodem... --canvas navigation
    uv run marvin-perf screendump --port /dev/cu.usbmodem... --canvas navigation \
        --rect 12,120,296,56 --out drawer-row.png

    # Stream a fixed sub-region (default: the scoring block) to PNGs at full rate
    uv run marvin-perf score-capture --port /dev/cu.usbmodem... --out scores/ --count 500
    uv run marvin-perf score-capture --port /dev/cu.usbmodem... --rect 114,309,96,105

    # ...or one of the two 2-player amp scoreboards (independent device slots,
    # so both can stream at once from separate invocations)
    uv run marvin-perf score-capture --port /dev/cu.usbmodem... --slot score-2p-left
    uv run marvin-perf score-capture --port /dev/cu.usbmodem... --slot score-2p-right

    # Extract strips of one kind from a recorded capture into <prefix>-NNNN.png
    uv run marvin-perf export-region session/ --out scores/
    uv run marvin-perf export-region session/ --kind score-2p-left --out scores-2pL/
    uv run marvin-perf export-region session/ --kind sensing-2p --out bands-2pL/

### `snapshot` vs `screendump`

They read two memories that share nothing, so neither can show the other's pixels:

* **`snapshot`** captures the **video frame** (the HDMI capture on the HEO hardware
  layer). Use it for anything about what the camera/console is producing.
* **`screendump`** captures a **Legato canvas surface** — the UI framebuffer a screen
  renders into. Use it to inspect widgets, icons and text at the pixel level.

`screendump` takes `--canvas` because every canvas is its own surface: the nav drawer,
the song/mode dialog, the album-art strip and the on-screen keyboard are **not** part
of the base view's buffer. Dump the canvas that owns the pixels you want.

Neither reproduces the panel: the LCDC composites the hardware layers (BASE / HEO /
OVR1 / OVR2) in the display controller, so the blended result exists only on the
glass. `screendump` deliberately does not try to fake it.

    # Export a capture as a SensiML-format CSV for MPLAB ML training
    uv run marvin-perf export-ml session/ --out session.csv --labels actuator-fb

### Capturing 2-player gameplay

Two-player mode moves everything the host cares about: the robot reads the
**left** highway instead of the centered one, and the single bottom-left scoring
block is replaced by **two amp scoreboards** near the top of the frame.

The detector's band strips are tagged per highway, so a capture says which
geometry produced them — `sensing`/`strike` for the 1p centered highway,
`sensing_2p`/`strike_2p` for the 2p left (robot) one. The switch is automatic
during a 2p song (marvin's screen classifier) and forceable from marvin's console
with `cvcfg 2pl`. Both kinds are gated by the STRIP type mask, as before.

The scoreboards ride the region-stream slots. The device has three independent
slots, each with its own enable and rect, each emitting its own strip kind:

| slot | `--slot` / `--kind` | strip kind | default rect |
|---|---|---|---|
| 0 | `score` | `region` | `114,309,96,105` (1p scoring block) |
| 1 | `score-2p-left` | `score_2p_left` | `128,164,68,78` |
| 2 | `score-2p-right` | `score_2p_right` | `515,164,68,78` |

Rects are host-supplied, so any slot can be repointed without a firmware
rebuild. Every slot streams at the full frame rate and there is no rate control:
over-subscribing the wire drops strips at the device's strip pool, counted in
`DROP.dropped_strip`. Capture the note bands and the scoreboards in **separate
sessions** rather than trying to fit both.

## Tests

    uv run pytest

Fixtures under [`tests/fixtures/`](tests/fixtures/) are hand-built byte streams
covering one frame per record type plus framing edge cases (FCS mismatch,
truncated payload, mid-stream join needing SOF resync).
