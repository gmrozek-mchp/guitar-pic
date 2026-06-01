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
    uv sync --group viewer   # include viewer deps for the serve command

`uv sync` creates `.venv/` and writes `uv.lock`. Subsequent invocations use the
existing venv automatically. Omit `--group viewer` if you only need the CLI
decode/record commands.

## Usage

    # Launch the visual review server (default when no subcommand is given)
    uv run marvin-perf
    uv run marvin-perf serve
    uv run marvin-perf serve --host 0.0.0.0 --port 8765
    uv run marvin-perf serve --capture session.bin   # pre-load a capture on startup

    # Capture raw bytes from the marvin USB-device CDC port (no decode)
    uv run marvin-perf record --port /dev/cu.usbmodem... --out session.bin

    # Push a type-mask to a running device without attaching for capture
    uv run marvin-perf set-mask --port /dev/cu.usbmodem... --types ALL

## Tests

    uv run pytest

Fixtures under [`tests/fixtures/`](tests/fixtures/) are hand-built byte streams
covering one frame per record type plus framing edge cases (FCS mismatch,
truncated payload, mid-stream join needing SOF resync).
