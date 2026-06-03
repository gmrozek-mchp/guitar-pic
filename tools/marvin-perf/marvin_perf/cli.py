"""Command-line entry point: serve / record / set-mask.

`serve` (the default) launches the visual review server. `record` and
`set-mask` are headless helpers — capture raw bytes from a serial port and
push a type-mask command to a running device respectively.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from .capture import (
    BIN_NAME,
    CaptureSource,
    finalize_capture_dir,
    init_capture_dir,
)
from .exporters import export_sensiml_csv
from .exporters.sensiml_csv import ExportError
from .framing import frame_encode
from .records import (
    RECORD_TYPE_BY_NAME,
    TYPE_MASK_ALL,
    TYPE_MASK_MIN,
    encode_set_mask_payload,
)
from .transport import SerialSource


# ─── Type-mask CLI parsing ───────────────────────────────────────────────────


def parse_types_arg(spec: str | None) -> int | None:
    """Resolve a `--types` spec to a u32 mask. Returns None if `spec` is None.

    Accepts a comma-separated list of RecordType names plus the special tokens
    `ALL` (0xFFFFFFFF) and `MIN` (SESSION|DROP). Case-insensitive.
    """
    if spec is None:
        return None
    mask = 0
    for raw in spec.split(","):
        token = raw.strip().upper()
        if not token:
            continue
        if token == "ALL":
            return TYPE_MASK_ALL
        if token == "MIN":
            mask |= TYPE_MASK_MIN
            continue
        if token not in RECORD_TYPE_BY_NAME:
            valid = ", ".join(sorted(RECORD_TYPE_BY_NAME))
            raise argparse.ArgumentTypeError(
                f"unknown record type {token!r}; valid: {valid}, ALL, MIN"
            )
        mask |= 1 << RECORD_TYPE_BY_NAME[token]
    return mask


def _types_help() -> str:
    bit_lines = ", ".join(
        f"{name}={int(rt)}" for name, rt in sorted(RECORD_TYPE_BY_NAME.items())
    )
    return (
        "Comma-separated record types to enable on the device. "
        f"Names: {bit_lines}. Special: ALL (default), MIN (SESSION+DROP). "
        "SESSION is always emitted regardless of mask. Old firmware ignores the command."
    )


def _send_mask(ser: SerialSource, mask: int) -> None:
    payload = encode_set_mask_payload(mask)
    ser.send_command(frame_encode(payload))
    print(f"[mask] sent 0x{mask:08x}", file=sys.stderr, flush=True)


# ─── Subcommands ─────────────────────────────────────────────────────────────


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

    mask = parse_types_arg(args.types)
    bytes_written = 0
    last_status = time.monotonic()
    with SerialSource(args.port) as ser, bin_path.open("wb") as fh:
        if mask is not None:
            _send_mask(ser, mask)
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


def cmd_set_mask(args: argparse.Namespace) -> int:
    mask = parse_types_arg(args.types)
    if mask is None:
        print("set-mask: --types is required", file=sys.stderr)
        return 2
    with SerialSource(args.port) as ser:
        _send_mask(ser, mask)
    return 0


def cmd_export_ml(args: argparse.Namespace) -> int:
    """Export a finished capture as a SensiML-format CSV for MPLAB ML.

    One row per fretboard ADC sample (~240 Hz); each row carries the five
    raw 12-bit ADC values plus binary labels. With --labels=detector the
    labels are the five per-fret pressed bits from cv_marvin_v1's
    pressed_mask; with --labels=actuator they are the five frets plus a
    collapsed strum bit from the timing pipeline's intended_mask (the
    edge-ai distillation target).
    """
    try:
        stats = export_sensiml_csv(
            args.capture, args.out, labels=args.labels, strict=args.strict
        )
    except ExportError as e:
        print(f"export-ml: {e}", file=sys.stderr)
        return 1
    if args.labels == "actuator":
        print(
            f"wrote {args.out}: {stats.n_rows} rows over {stats.duration_s:.2f} s "
            f"({stats.n_actuator_records} actuator records, "
            f"{stats.n_strum_events} strum events, "
            f"{stats.n_fretboard_records} fretboard records, "
            f"{stats.n_skipped_unlabeled} skipped)",
            file=sys.stderr,
        )
    else:
        print(
            f"wrote {args.out}: {stats.n_rows} rows over {stats.duration_s:.2f} s "
            f"({stats.n_detector_records} detector records, "
            f"{stats.n_fretboard_records} fretboard records, "
            f"{stats.n_skipped_unlabeled} skipped)",
            file=sys.stderr,
        )
    # Loud failure modes: a structurally-valid CSV with zero usable labels is
    # training-useless and easy to miss from a one-line summary. Surface as a
    # warning the user can't ignore, and exit non-zero so scripts notice.
    if args.labels == "actuator" and stats.n_rows > 0 and stats.n_actuator_records == 0:
        print(
            "WARNING: capture contains no ACTUATOR records — every row's "
            "fret_*/strum columns are all zeros. The CSV is unlabelled and "
            "not useful for training. Re-record with both ACTUATOR and "
            "FRETBOARD_RAW enabled in the Types panel (or "
            "--types ACTUATOR,FRETBOARD_RAW for `marvin-perf record`).",
            file=sys.stderr,
        )
        return 2
    if args.labels == "detector" and stats.n_rows > 0 and stats.n_detector_records == 0:
        print(
            "WARNING: capture contains no DETECTOR records — every row's "
            "label_* columns are all zeros. The CSV is unlabelled and not "
            "useful for training. Re-record with both DETECTOR and "
            "FRETBOARD_RAW enabled in the Types panel (or "
            "--types DETECTOR,FRETBOARD_RAW for `marvin-perf record`).",
            file=sys.stderr,
        )
        return 2
    if stats.n_rows == 0:
        print(
            "WARNING: capture contains no FRETBOARD_RAW records — output "
            "CSV has only the header row. Re-record with FRETBOARD_RAW "
            "enabled.",
            file=sys.stderr,
        )
        return 2
    return 0


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


# ─── Argparse wiring ─────────────────────────────────────────────────────────


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="marvin-perf",
        description="marvin perf-log: visual review server + headless capture helpers.",
    )
    sub = p.add_subparsers(dest="command")

    p_record = sub.add_parser("record", help="Capture raw bytes (no decode).")
    p_record.add_argument("--port", required=True)
    p_record.add_argument("--out", help="Write a bare .bin (legacy)")
    p_record.add_argument("--out-dir", help="Write a capture directory (perf.bin + manifest.json)")
    p_record.add_argument("--types", help=_types_help())
    p_record.set_defaults(func=cmd_record)

    p_set_mask = sub.add_parser(
        "set-mask",
        help="Send a SET_TYPE_MASK command to a running device and exit.",
    )
    p_set_mask.add_argument("--port", required=True)
    p_set_mask.add_argument("--types", required=True, help=_types_help())
    p_set_mask.set_defaults(func=cmd_set_mask)

    p_export_ml = sub.add_parser(
        "export-ml",
        help="Export a capture as a SensiML-format CSV for MPLAB ML training.",
    )
    p_export_ml.add_argument("capture", help="Capture directory or .bin file")
    p_export_ml.add_argument("--out", required=True, help="Output CSV path")
    p_export_ml.add_argument(
        "--labels",
        choices=("detector", "actuator"),
        default="detector",
        help="Label source: 'detector' = 5 per-fret pressed bits from "
             "cv_marvin_v1 (default); 'actuator' = 5 frets + collapsed strum "
             "from the timing pipeline's intended_mask (edge-ai target).",
    )
    p_export_ml.add_argument(
        "--strict",
        action="store_true",
        help="Drop fretboard rows that arrive before the first label-source "
             "record (default: emit them with all labels = 0).",
    )
    p_export_ml.set_defaults(func=cmd_export_ml)

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


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command is None:
        args = parser.parse_args(["serve"])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
