# marvin-perf

Host-side decoder and analyzer for the marvin firmware **perf-log** wire format.

The firmware emits framed binary records (schema version 5) over its USB-device
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
CLI helpers (record, set-mask, set-overlay, snapshot, export-ml).

## Usage

    # Launch the visual review server (default when no subcommand is given)
    # In live mode the "▣ Score region" toggle streams the scoring block; hit
    # Record to capture it into the .bin like any other record, then extract it
    # offline with `export-region`.
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

    # Stream a fixed sub-region (default: the scoring block) to PNGs at full rate
    uv run marvin-perf score-capture --port /dev/cu.usbmodem... --out scores/ --count 500
    uv run marvin-perf score-capture --port /dev/cu.usbmodem... --rect 114,309,96,105

    # Extract REGION strips from a recorded capture into score-NNNN.png (score corpus)
    uv run marvin-perf export-region session/ --out scores/

    # Export a capture as a SensiML-format CSV for MPLAB ML training
    uv run marvin-perf export-ml session/ --out session.csv --labels actuator-fb

## Tests

    uv run pytest

Fixtures under [`tests/fixtures/`](tests/fixtures/) are hand-built byte streams
covering one frame per record type plus framing edge cases (FCS mismatch,
truncated payload, mid-stream join needing SOF resync).
