"""Command-line entry point: live / record / decode / summarize."""

from __future__ import annotations

import argparse
import sys
import time
from collections.abc import Iterable, Iterator
from pathlib import Path
from typing import Any

from .analyze import (
    SchemaVersionMismatch,
    check_frame_epoch_monotonic,
    check_schema,
    check_video_publish_cadence,
    compute_drops,
    compute_hwm,
    compute_latencies,
    find_session,
)
from .capture import (
    BIN_NAME,
    CaptureSource,
    finalize_capture_dir,
    init_capture_dir,
    open_capture,
)
from .decode import Record, decode_record
from .framing import FrameStats, iter_frames
from .records import (
    Detector,
    Drop,
    Patch,
    RecordType,
    Session,
    Stage,
    Stamp,
    TaskHighwater,
    TaskId,
    Timing,
    UnknownRecord,
)
from .transport import FileSource, SerialSource, TeeSource


# ─── Shared decode pump ──────────────────────────────────────────────────────


def _decode_stream(chunks: Iterable[bytes], stats: FrameStats) -> Iterator[Record]:
    for frame in iter_frames(chunks, stats):
        yield decode_record(frame.payload)


# ─── Pretty-printers ─────────────────────────────────────────────────────────


def _stage_name(stage_id: int) -> str:
    try:
        return Stage(stage_id).name
    except ValueError:
        return f"stage_{stage_id:#04x}"


def _format_drop(rec: Drop, baseline: Drop | None) -> str:
    if baseline is None:
        return (
            f"DROP       state={rec.dropped_state} patch={rec.dropped_patch} "
            f"sink_bytes={rec.dropped_sink}  (baseline)"
        )
    return (
        f"DROP       "
        f"state={rec.dropped_state} (Δ{rec.dropped_state - baseline.dropped_state:+d})  "
        f"patch={rec.dropped_patch} (Δ{rec.dropped_patch - baseline.dropped_patch:+d})  "
        f"sink_bytes={rec.dropped_sink} (Δ{rec.dropped_sink - baseline.dropped_sink:+d})"
    )


class _DropBaseline:
    """Tracks the first DROP seen so subsequent ones print as session deltas."""

    def __init__(self) -> None:
        self.first: Drop | None = None

    def format(self, rec: Drop) -> str:
        line = _format_drop(rec, self.first)
        if self.first is None:
            self.first = rec
        return line


def _format_record(rec: Record, *, drop_baseline: _DropBaseline | None = None) -> str:
    epoch = getattr(rec.hdr, "frame_epoch", 0)
    ts = getattr(rec.hdr, "ts_counter", 0)
    if isinstance(rec, Session):
        return (
            f"SESSION    timer={rec.timer_freq_hz} Hz schema=v{rec.schema_version} "
            f"git={rec.fw_git_short:#010x}"
        )
    if isinstance(rec, Stamp):
        return (
            f"STAMP      epoch={epoch:>8} ts={ts:>12} "
            f"stage={_stage_name(rec.stage_id):<19} aux={rec.aux:#010x}"
        )
    if isinstance(rec, Detector):
        return (
            f"DETECTOR   epoch={epoch:>8} ts={ts:>12} "
            f"hold={rec.hold_dist} edge={rec.edge_dist} "
            f"P={rec.pressed_mask:#04x} E={rec.edge_active_mask:#04x}"
        )
    if isinstance(rec, Timing):
        return (
            f"TIMING     epoch={epoch:>8} ts={ts:>12} "
            f"pub={rec.publish_mask:#04x} cw={rec.chord_window_fill} "
            f"fifo={rec.fifo_depth} dir={rec.strum_dir}"
        )
    if isinstance(rec, Drop):
        return (
            drop_baseline.format(rec)
            if drop_baseline is not None
            else _format_drop(rec, None)
        )
    if isinstance(rec, TaskHighwater):
        try:
            name = TaskId(rec.task_id).name
        except ValueError:
            name = f"task_{rec.task_id}"
        return f"HWM        task={name:<16} words_free={rec.words}"
    if isinstance(rec, Patch):
        return f"PATCH      epoch={epoch:>8} {rec.frame_w}×{rec.frame_h}  (5 frets, 75 B BGR each)"
    if isinstance(rec, UnknownRecord):
        return f"UNKNOWN    type={rec.hdr.type:#04x} ({len(rec.raw)} B)"
    return repr(rec)


# ─── Subcommands ─────────────────────────────────────────────────────────────


def cmd_live(args: argparse.Namespace) -> int:
    stats = FrameStats()
    record_count = 0
    last_status = time.monotonic()
    drop_records = 0

    serial_src = SerialSource(args.port)
    if args.also_record:
        with serial_src as ser, TeeSource(ser, args.also_record) as tee:
            return _live_loop(_decode_stream(tee, stats), stats)
    else:
        with serial_src as ser:
            return _live_loop(_decode_stream(ser, stats), stats)


def _live_loop(records: Iterator[Record], stats: FrameStats) -> int:
    last_status = time.monotonic()
    record_count = 0
    drop_records = 0
    drop_baseline = _DropBaseline()
    for rec in records:
        record_count += 1
        if isinstance(rec, Drop):
            drop_records += 1
        print(_format_record(rec, drop_baseline=drop_baseline))
        now = time.monotonic()
        if (now - last_status) >= 1.0:
            print(
                f"  [status] frames={stats.frames_ok} resync_drop={stats.bytes_resync_dropped} "
                f"crc_err={stats.crc_mismatches} drop_recs={drop_records}",
                file=sys.stderr,
                flush=True,
            )
            last_status = now
    return 0


def _resolve_bin_path(path: str) -> Path:
    """Accept either a capture directory or a bare .bin and return the bin."""
    p = Path(path)
    if p.is_dir():
        bin_path = p / BIN_NAME
        if not bin_path.exists():
            raise FileNotFoundError(f"capture directory missing {BIN_NAME}: {p}")
        return bin_path
    return p


def cmd_record(args: argparse.Namespace) -> int:
    """Pure pass-through to disk; no decode.

    Two modes: `--out FILE.bin` writes a raw legacy file; `--out-dir DIR`
    writes a capture directory (DIR/perf.bin + manifest.json finalized on
    close).
    """
    if (args.out is None) == (args.out_dir is None):
        print("record: exactly one of --out or --out-dir is required", file=sys.stderr)
        return 2

    if args.out_dir is not None:
        cap_dir = init_capture_dir(args.out_dir, exist_ok=False)
        bin_path = cap_dir / BIN_NAME
        label = f"capture {cap_dir.name}/"
    else:
        cap_dir = None
        bin_path = Path(args.out)
        label = f"recording {bin_path.name}"

    bytes_written = 0
    last_status = time.monotonic()
    with SerialSource(args.port) as ser, bin_path.open("wb") as fh:
        try:
            for chunk in ser:
                fh.write(chunk)
                fh.flush()
                bytes_written += len(chunk)
                now = time.monotonic()
                if (now - last_status) >= 1.0:
                    print(f"{label}: {bytes_written} B", file=sys.stderr, flush=True)
                    last_status = now
        except KeyboardInterrupt:
            print(f"\nstopped after {bytes_written} B", file=sys.stderr)

    if cap_dir is not None:
        manifest = finalize_capture_dir(
            cap_dir,
            source=CaptureSource(kind="serial", port=args.port),
        )
        print(
            f"finalized {cap_dir}/manifest.json — {manifest.n_records} records, "
            f"types={manifest.producer_capabilities}",
            file=sys.stderr,
        )
    return 0


def cmd_decode(args: argparse.Namespace) -> int:
    stats = FrameStats()
    drop_baseline = _DropBaseline()
    bin_path = _resolve_bin_path(args.path)
    with FileSource(bin_path) as src:
        for rec in _decode_stream(src, stats):
            print(_format_record(rec, drop_baseline=drop_baseline))
    print(
        f"\n[decode] frames_ok={stats.frames_ok} resync_drop={stats.bytes_resync_dropped} "
        f"crc_err={stats.crc_mismatches} bad_len={stats.bad_lengths}",
        file=sys.stderr,
    )
    return 0


def cmd_summarize(args: argparse.Namespace) -> int:
    stats = FrameStats()
    records: list[Record] = []
    bin_path = _resolve_bin_path(args.path)
    with FileSource(bin_path) as src:
        records.extend(_decode_stream(src, stats))

    session = find_session(records)
    try:
        check_schema(session)
    except SchemaVersionMismatch as e:
        print(f"FATAL: {e}", file=sys.stderr)
        return 2

    timer_freq_hz = session.timer_freq_hz if session else 0

    print("=== marvin perf-log summary ===")
    print(f"file: {args.path}")
    print(f"frames_ok={stats.frames_ok}  bytes_resync_dropped={stats.bytes_resync_dropped}  "
          f"crc_err={stats.crc_mismatches}  bad_len={stats.bad_lengths}")
    print(f"records: {len(records)}")

    if session is None:
        print("(no SESSION record found — capture began mid-stream)")
    else:
        print(
            f"session: timer={session.timer_freq_hz} Hz  schema=v{session.schema_version}  "
            f"git={session.fw_git_short:#010x}"
        )

    # Record-type histogram.
    counts: dict[str, int] = {}
    for rec in records:
        name = type(rec).__name__
        counts[name] = counts.get(name, 0) + 1
    print("\n--- record-type counts ---")
    for name in sorted(counts):
        print(f"  {name:<14} {counts[name]}")

    # Latency.
    print("\n--- latency (µs) ---")
    if timer_freq_hz <= 0:
        print("  (skipped — no SESSION; cannot convert ts_counter to µs)")
    else:
        hists = compute_latencies(records, timer_freq_hz)
        print(f"  {'pair':<40} {'n':>6} {'p50':>10} {'p95':>10} {'p99':>10} {'max':>10}")
        for h in hists:
            a, b = h.pair
            label = f"{a.name} → {b.name}"
            if h.n == 0:
                print(f"  {label:<40} {0:>6} {'—':>10} {'—':>10} {'—':>10} {'—':>10}")
            else:
                print(
                    f"  {label:<40} {h.n:>6} {h.p50:>10.1f} {h.p95:>10.1f} "
                    f"{h.p99:>10.1f} {h.max:>10.1f}"
                )

    # Drops.
    drops = compute_drops(records)
    print("\n--- drops (since-session / cumulative since boot) ---")
    print(
        f"  state:      Δ{drops.since_session_state} records  "
        f"(cum {drops.final_state}, max step {drops.max_state_delta})"
    )
    print(
        f"  patch:      Δ{drops.since_session_patch} records  "
        f"(cum {drops.final_patch}, max step {drops.max_patch_delta})"
    )
    print(
        f"  sink_bytes: Δ{drops.since_session_sink_bytes} bytes  "
        f"(cum {drops.final_sink_bytes}, max step {drops.max_sink_bytes_delta})"
    )
    print(f"  drop records observed: {drops.n_drop_records}")

    # HWM.
    hwm = compute_hwm(records)
    if hwm:
        print("\n--- stack high-water (StackType_t words free) ---")
        for series in sorted(hwm.values(), key=lambda s: s.task_id):
            samples = series.samples
            print(
                f"  {series.task_name:<16}  n={len(samples):>4}  "
                f"min={series.min_words}  last={samples[-1][1] if samples else '—'}"
            )

    # Warnings.
    warns = check_frame_epoch_monotonic(records) + check_video_publish_cadence(
        records, timer_freq_hz
    )
    if warns:
        print("\n--- warnings ---")
        for w in warns[:50]:
            print(f"  [{w.kind}] {w.message}")
        if len(warns) > 50:
            print(f"  …and {len(warns) - 50} more")
    return 0


# ─── Argparse wiring ─────────────────────────────────────────────────────────


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="marvin-perf",
        description="Decode and analyze marvin firmware perf-log streams.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    p_live = sub.add_parser("live", help="Live decode from a USB-CDC serial port.")
    p_live.add_argument("--port", required=True, help="Serial port (e.g. /dev/cu.usbmodem...)")
    p_live.add_argument("--also-record", help="Optionally also write raw bytes to FILE")
    p_live.set_defaults(func=cmd_live)

    p_record = sub.add_parser("record", help="Capture raw bytes (no decode).")
    p_record.add_argument("--port", required=True)
    p_record.add_argument("--out", help="Write a bare .bin (legacy)")
    p_record.add_argument("--out-dir", help="Write a capture directory (perf.bin + manifest.json)")
    p_record.set_defaults(func=cmd_record)

    p_decode = sub.add_parser(
        "decode", help="Pretty-print every record in a capture (.bin or directory)."
    )
    p_decode.add_argument("path")
    p_decode.set_defaults(func=cmd_decode)

    p_summary = sub.add_parser(
        "summarize", help="Run analysis pass over a capture (.bin or directory)."
    )
    p_summary.add_argument("path")
    p_summary.set_defaults(func=cmd_summarize)

    p_serve = sub.add_parser(
        "serve", help="Run the visual review server (requires viewer dep group)."
    )
    p_serve.add_argument("--host", default="127.0.0.1")
    p_serve.add_argument("--port", type=int, default=8765)
    p_serve.add_argument(
        "--capture",
        help="Optional capture path (directory or .bin) to pre-load on startup.",
    )
    p_serve.set_defaults(func=cmd_serve)

    return p


def cmd_serve(args: argparse.Namespace) -> int:
    try:
        from .web.server import run as run_server
    except ImportError as e:
        print(
            "marvin-perf serve requires the 'viewer' dep group.\n"
            "  uv sync --group viewer\n"
            f"  (import error: {e})",
            file=sys.stderr,
        )
        return 2
    return run_server(host=args.host, port=args.port, capture=args.capture)


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
