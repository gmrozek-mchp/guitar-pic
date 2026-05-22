# marvin-perf

Host-side decoder and analyzer for the marvin firmware **perf-log** wire format.

The firmware emits framed binary records (schema version 1) over its USB-device
CDC ACM port. This tool consumes that stream live, records it to disk, replays
captures offline, and produces summary analyses (latency attribution, drop
accounting, stack high-water trends).

Wire format source-of-truth lives in the firmware tree at
[`firmware/marvin/default/src/perf_log/perf_log_records.h`](../../firmware/marvin/default/src/perf_log/perf_log_records.h).
The Python schema mirror in [`marvin_perf/records.py`](marvin_perf/records.py)
must match it byte-for-byte; bump `PERF_LOG_SCHEMA_VERSION` on both sides
together.

## Setup

All Python work runs through [**uv**](https://docs.astral.sh/uv/). First-time
bootstrap:

    cd tools/marvin-perf
    uv sync

`uv sync` creates `.venv/` and writes `uv.lock`. Subsequent invocations use the
existing venv automatically.

## Usage

    # Live decode from the marvin USB-device CDC port
    uv run marvin-perf live --port /dev/cu.usbmodem...

    # Live decode and capture raw bytes to disk in parallel
    uv run marvin-perf live --port /dev/cu.usbmodem... --also-record session.bin

    # Capture raw bytes only (no decode)
    uv run marvin-perf record --port /dev/cu.usbmodem... --out session.bin

    # Pretty-print every record in a captured file
    uv run marvin-perf decode session.bin

    # Run the full analysis pass over a captured file
    uv run marvin-perf summarize session.bin

## Tests

    uv run pytest

Fixtures under [`tests/fixtures/`](tests/fixtures/) are hand-built byte streams
covering one frame per record type plus framing edge cases (CRC mismatch,
truncated payload, mid-stream join needing SOF resync).
