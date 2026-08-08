"""Command-line entry point: serve / record / set-mask.

`serve` (the default) launches the visual review server. `record` and
`set-mask` are headless helpers — capture raw bytes from a serial port and
push a type-mask command to a running device respectively.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

from .capture import (
    BIN_NAME,
    CaptureSource,
    finalize_capture_dir,
    init_capture_dir,
    open_capture,
)
from .exporters import export_sensiml_csv
from .exporters.sensiml_csv import ExportError
from .decode import decode_record
from .framing import FrameStats, frame_encode, iter_frames
from .records import (
    CANVAS_IDS,
    PERF_OVERLAY_STRIP,
    DEFAULT_REGION_RECT,
    RECORD_TYPE_BY_NAME,
    REGION_SLOTS,
    REGION_SLOT_BY_ID,
    TYPE_MASK_ALL,
    TYPE_MASK_MIN,
    Strip,
    StripKind,
    encode_canvas_dump_payload,
    encode_region_stream_payload,
    encode_set_mask_payload,
    encode_set_overlay_payload,
    encode_snapshot_payload,
)
from .snapshot import CompletedSnapshot, SnapshotAssembler, save_region_strips, save_snapshot
from .transport import FileSource, SerialSource

# Default capture region — the scoring block (both training/career modes fit).
SCORE_BLOCK_RECT = DEFAULT_REGION_RECT

# Strip kinds `export-region` can pull out of a capture, and the PNG prefix each
# gets. The region slots cover the scoreboard streams; the band kinds let a
# 2-player session's note strips come out as their own corpus.
_EXPORT_KINDS: dict[str, tuple[StripKind, str]] = {
    **{s.id: (s.kind, s.prefix) for s in REGION_SLOTS},
    "sensing": (StripKind.SENSING, "sensing"),
    "strike": (StripKind.STRIKE, "strike"),
    "sensing-2p": (StripKind.SENSING_2P, "sensing-2p"),
    "strike-2p": (StripKind.STRIKE_2P, "strike-2p"),
}


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


def cmd_set_overlay(args: argparse.Namespace) -> int:
    """Toggle the per-fret target rings on the SENSING strip."""
    flags = PERF_OVERLAY_STRIP if args.on else 0
    with SerialSource(args.port) as ser:
        ser.send_command(frame_encode(encode_set_overlay_payload(flags)))
    print(f"set-overlay: strip rings {'on' if args.on else 'off'}", file=sys.stderr)
    return 0


def _idle_chunks(ser: SerialSource, idle_timeout_s: float):
    """Yield serial chunks until `idle_timeout_s` elapses with no new bytes.

    Lets `iter_frames` terminate once the snapshot burst stops (or stalls),
    instead of blocking forever on the infinite SerialSource iterator.
    """
    deadline = time.monotonic() + idle_timeout_s
    while time.monotonic() < deadline:
        chunk = ser.read_chunk()
        if chunk:
            deadline = time.monotonic() + idle_timeout_s
            yield chunk


def _retrying_chunks(
    ser: SerialSource,
    command: bytes,
    idle_timeout_s: float,
    resend_after_s: float = 0.75,
):
    """Yield serial chunks, (re)sending `command` until the device answers.

    The firmware's CDC sink is DTR-gated and only notices DTR when it polls, so a
    command written immediately after opening the port can be answered into a sink
    that is still closed — the reply is dropped and the caller waits out its whole
    timeout. Opening the port also makes the firmware re-emit a SESSION record, so
    "some bytes arrived" is not proof the command itself landed.

    Re-sending is safe because both the snapshot and canvas-dump commands are
    idempotent one-shots: a duplicate just produces another burst, and the
    assembler keys bands by frame_epoch so a stale partial burst cannot corrupt a
    later complete one. Stops re-sending once any byte arrives after a send, then
    falls back to plain idle-timeout behaviour.
    """
    ser.send_command(command)
    sent_at = time.monotonic()
    deadline = sent_at + idle_timeout_s
    answered = False

    while time.monotonic() < deadline:
        chunk = ser.read_chunk()
        if chunk:
            answered = True
            deadline = time.monotonic() + idle_timeout_s
            yield chunk
            continue
        if not answered and (time.monotonic() - sent_at) >= resend_after_s:
            ser.send_command(command)
            sent_at = time.monotonic()


def _resolve_snapshot_out(out: str | None) -> Path:
    """Resolve the `--out` argument to a concrete .png path.

    A value ending in `.png` is taken literally. Anything else (including the
    default when `--out` is omitted) is treated as a directory, and the next
    free `snapshot-NNNN.png` in it is chosen so rapid captures never clobber.
    """
    p = Path(out) if out else Path("snapshots")
    if p.suffix.lower() == ".png":
        p.parent.mkdir(parents=True, exist_ok=True)
        return p

    p.mkdir(parents=True, exist_ok=True)
    nums = [
        int(m.group(1))
        for f in p.glob("snapshot-*.png")
        if (m := re.fullmatch(r"snapshot-(\d+)", f.stem))
    ]
    nxt = (max(nums) + 1) if nums else 1
    return p / f"snapshot-{nxt:04d}.png"


def cmd_snapshot(args: argparse.Namespace) -> int:
    """Trigger a full-frame snapshot and save it as a lossless PNG.

    Sends PERF_CMD_SNAPSHOT, reassembles the returned SNAPSHOT band strips into
    one frame, and writes a PNG. With `--out` omitted (or pointed at a
    directory) the filename auto-increments as `snapshot-NNNN.png` for quick
    repeated captures.
    """
    out_path = _resolve_snapshot_out(args.out)

    assembler = SnapshotAssembler()
    with SerialSource(args.port) as ser:
        ser.send_command(frame_encode(encode_snapshot_payload()))
        print("[snapshot] requested; waiting for bands…", file=sys.stderr, flush=True)
        snap = None
        for fb in iter_frames(_idle_chunks(ser, args.timeout)):
            rec = decode_record(fb.payload)
            if not isinstance(rec, Strip):
                continue
            snap = assembler.add(rec)
            if snap is not None:
                break

    if snap is None:
        print(
            f"snapshot: no complete frame within {args.timeout:.1f}s idle timeout. "
            "Is marvin connected, HDMI locked, and the firmware schema current?",
            file=sys.stderr,
        )
        return 1

    written = save_snapshot(snap, out_path)
    print(
        f"snapshot: {snap.width}×{snap.height} (frame_epoch {snap.frame_epoch}) → "
        + ", ".join(str(p) for p in written),
        file=sys.stderr,
    )
    return 0


def _resolve_screendump_out(out: str | None, canvas: str) -> Path:
    """Pick the output path, auto-incrementing `<canvas>-NNNN.png` in a directory."""
    p = Path(out) if out else Path("screendumps")
    if out and p.suffix:
        p.parent.mkdir(parents=True, exist_ok=True)
        return p
    p.mkdir(parents=True, exist_ok=True)
    nxt = 1 + max(
        (
            int(m.group(1))
            for f in p.glob(f"{canvas}-*.png")
            if (m := re.fullmatch(rf"{re.escape(canvas)}-(\d+)", f.stem))
        ),
        default=0,
    )
    return p / f"{canvas}-{nxt:04d}.png"


def cmd_screendump(args: argparse.Namespace) -> int:
    """Dump a Legato canvas surface (the UI framebuffer) and save it as a PNG.

    The UI counterpart to `snapshot`, which captures the video frame. Each canvas
    is a separate surface and nothing is composited, so the drawer, dialogs and the
    on-screen keyboard do not appear in the base view's dump — pick the canvas that
    holds the pixels you care about. Likewise the video layer is never included.
    """
    canvas_id = CANVAS_IDS.get(args.canvas)
    if canvas_id is None:
        print(
            f"screendump: unknown canvas {args.canvas!r}; "
            f"choose from {', '.join(sorted(CANVAS_IDS))}",
            file=sys.stderr,
        )
        return 2

    x, y, w, h = _parse_rect(args.rect, default=(0, 0, 0, 0))
    out_path = _resolve_screendump_out(args.out, args.canvas)

    assembler = SnapshotAssembler(kind=StripKind.CANVAS, origin_x=x, origin_y=y)
    command = frame_encode(encode_canvas_dump_payload(canvas_id, x, y, w, h))
    with SerialSource(args.port) as ser:
        if args.listen:
            # Don't request anything — just assemble whatever CANVAS bands turn up.
            # Lets the dump be triggered another way (marvin's `perf dump <canvas>`
            # console command) to tell a broken request path from a broken emit path.
            print(
                f"[screendump] listening for canvas {args.canvas} bands "
                f"(trigger with: perf dump {canvas_id}"
                + (f" {x} {y} {w} {h}" if (x or y or w or h) else "")
                + ")…",
                file=sys.stderr,
                flush=True,
            )
            chunks = _idle_chunks(ser, args.timeout)
        else:
            print(
                f"[screendump] canvas {args.canvas} requested; waiting for bands…",
                file=sys.stderr,
                flush=True,
            )
            chunks = _retrying_chunks(ser, command, args.timeout)
        snap = None
        saw_strip = False
        for fb in iter_frames(chunks):
            rec = decode_record(fb.payload)
            if not isinstance(rec, Strip):
                continue
            saw_strip = True
            snap = assembler.add(rec)
            if snap is not None:
                break

    if snap is None:
        detail = (
            "bands arrived but never completed the region — a partial burst?"
            if saw_strip
            else f"no CANVAS bands arrived. Canvas {args.canvas} may have no surface "
                 "assigned yet, or the rect may fall outside it (check the device log)."
        )
        print(
            f"screendump: no complete dump within {args.timeout:.1f}s idle timeout: "
            f"{detail}",
            file=sys.stderr,
        )
        return 1

    written = save_snapshot(snap, out_path)
    print(
        f"screendump: {args.canvas} {snap.width}×{snap.height} at ({snap.x},{snap.y}) → "
        + ", ".join(str(p) for p in written),
        file=sys.stderr,
    )
    return 0


def _parse_rect(
    spec: str | None, default: tuple[int, int, int, int] | None = None
) -> tuple[int, int, int, int]:
    """Parse an "x,y,w,h" rect, or return the default score block."""
    if not spec and default is not None:
        return default
    if not spec:
        return SCORE_BLOCK_RECT
    parts = spec.split(",")
    if len(parts) != 4:
        raise ValueError(f"--rect must be x,y,w,h (got {spec!r})")
    x, y, w, h = (int(p) for p in parts)
    return x, y, w, h


def _score_dir_start(out: str | None, prefix: str = "score") -> tuple[Path, int]:
    """Resolve the output directory and the next free <prefix>-NNNN index."""
    p = Path(out) if out else Path("scores")
    p.mkdir(parents=True, exist_ok=True)
    nums = [
        int(m.group(1))
        for f in p.glob(f"{prefix}-*.png")
        if (m := re.fullmatch(rf"{re.escape(prefix)}-(\d+)", f.stem))
    ]
    return p, (max(nums) + 1 if nums else 1)


def cmd_score_capture(args: argparse.Namespace) -> int:
    """Stream a fixed video sub-region to disk, one PNG per frame, at full rate.

    Sends PERF_CMD_REGION_STREAM (start) on the chosen slot for the requested
    rect (default: the slot's own rect), saves each returned strip of that slot's
    kind as `<prefix>-NNNN.png`, and stops on `--count` or Ctrl-C, sending the
    stop command on the way out. Each strip is one complete region frame, so no
    reassembly is needed. The slot's command is the gate, so no type mask is
    needed on the device.
    """
    slot = REGION_SLOT_BY_ID[args.slot]
    x, y, w, h = _parse_rect(args.rect, default=slot.rect)
    out_dir, n = _score_dir_start(args.out, slot.prefix)
    saved = 0
    with SerialSource(args.port) as ser:
        ser.send_command(
            frame_encode(encode_region_stream_payload(True, x, y, w, h, slot=slot.slot))
        )
        print(
            f"[score-capture] streaming {slot.id} ({x},{y},{w}×{h}) → "
            f"{out_dir}/{slot.prefix}-NNNN.png; Ctrl-C to stop",
            file=sys.stderr,
            flush=True,
        )
        try:
            for fb in iter_frames(_idle_chunks(ser, args.timeout)):
                rec = decode_record(fb.payload)
                if not isinstance(rec, Strip) or rec.kind != int(slot.kind):
                    continue
                snap = CompletedSnapshot(
                    width=rec.w, height=rec.h, frame_epoch=rec.hdr.frame_epoch, bgr=rec.bgr
                )
                save_snapshot(snap, out_dir / f"{slot.prefix}-{n:04d}.png")
                saved += 1
                n += 1
                if args.count and saved >= args.count:
                    break
        except KeyboardInterrupt:
            pass
        finally:
            ser.send_command(
                frame_encode(encode_region_stream_payload(False, slot=slot.slot))
            )

    print(f"score-capture: saved {saved} frame(s) to {out_dir}/", file=sys.stderr)
    return 0 if saved else 1


def cmd_export_ml(args: argparse.Namespace) -> int:
    """Export a finished capture as a SensiML-format CSV for MPLAB ML.

    One row per fretboard ADC sample (~240 Hz); each row carries the five
    raw 12-bit ADC values plus binary labels. With --labels=detector the
    labels are the five per-fret pressed bits from cv_marvin_v1's
    pressed_mask; with --labels=actuator they are the five frets plus a
    collapsed strum bit from the timing pipeline's intended_mask (the
    edge-ai distillation target); with --labels=actuator-fb the same labels
    come from the actuator bitmask the fretboard reports inside each frame
    (atomically paired with the ADC; preferred — adds an fb_seq column).
    """
    try:
        stats = export_sensiml_csv(
            args.capture, args.out, labels=args.labels, strict=args.strict
        )
    except ExportError as e:
        print(f"export-ml: {e}", file=sys.stderr)
        return 1
    if args.labels in ("actuator-fb", "commanded-fb"):
        print(
            f"wrote {args.out}: {stats.n_rows} rows over {stats.duration_s:.2f} s "
            f"({stats.n_strum_events} strum events, "
            f"{stats.n_fretboard_records} fretboard records, "
            f"{stats.n_seq_gaps} seq gaps, "
            f"{stats.n_skipped_unlabeled} skipped)",
            file=sys.stderr,
        )
    elif args.labels == "detector-fb":
        print(
            f"wrote {args.out}: {stats.n_rows} rows over {stats.duration_s:.2f} s "
            f"({stats.n_detector_records} detector records, "
            f"{stats.n_strum_events} strum events, "
            f"{stats.n_fretboard_records} fretboard records, "
            f"{stats.n_seq_gaps} seq gaps)",
            file=sys.stderr,
        )
    elif args.labels == "actuator":
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
    if args.labels in ("detector", "detector-fb") and stats.n_rows > 0 and stats.n_detector_records == 0:
        print(
            "WARNING: capture contains no DETECTOR records — every row's "
            "fret columns are all zeros. The CSV is unlabelled and not "
            "useful for training. Re-record with both DETECTOR and "
            "FRETBOARD_RAW enabled in the Types panel (or "
            "--types DETECTOR,FRETBOARD_RAW for `marvin-perf record`).",
            file=sys.stderr,
        )
        return 2
    if args.labels in ("actuator-fb", "commanded-fb") and stats.n_rows > 0 and stats.n_strum_events == 0:
        src = ("marvin's teacher command (commanded_mask)"
               if args.labels == "commanded-fb"
               else "the in-frame actuator bitmask (applied_mask)")
        hint = ("marvin wasn't the active CV teacher during the capture "
                "(need `active cv` + a song playing), or the firmware predates "
                "schema v6 (frames carry no commanded_mask)."
                if args.labels == "commanded-fb"
                else "this isn't gameplay, or the firmware predates schema v4 "
                     "(frames carry no applied_mask).")
        print(
            f"WARNING: no strum events in {src} — the label source reported no "
            f"actuation. Either {hint}",
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


def cmd_export_region(args: argparse.Namespace) -> int:
    """Extract strips of one kind from a capture into numbered PNGs.

    Pulls the frames recorded (via the web viewer's Record button or `record`)
    out of a capture and writes them as `<prefix>-NNNN.png` — the input to the
    gameplay template corpora. Defaults to the slot-0 scoring block; `--kind`
    selects a 2-player scoreboard or either detector band instead.
    """
    kind, prefix = _EXPORT_KINDS[args.kind]
    cap = open_capture(args.capture)
    records = []
    stats = FrameStats()
    with FileSource(cap.bin_path) as src:
        for frame in iter_frames(src, stats):
            try:
                records.append(decode_record(frame.payload))
            except Exception:
                continue
    written = save_region_strips(records, args.out or "scores", kind=kind, prefix=prefix)
    print(
        f"export-region: {len(written)} {args.kind} frame(s) → {args.out or 'scores'}/",
        file=sys.stderr,
    )
    return 0 if written else 1


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

    p_set_overlay = sub.add_parser(
        "set-overlay",
        help="Toggle the per-fret target rings on the SENSING strip.",
    )
    p_set_overlay.add_argument("--port", required=True)
    grp = p_set_overlay.add_mutually_exclusive_group(required=True)
    grp.add_argument("--on", dest="on", action="store_true", help="Enable strip rings.")
    grp.add_argument("--off", dest="on", action="store_false", help="Disable strip rings.")
    p_set_overlay.set_defaults(func=cmd_set_overlay)

    p_snapshot = sub.add_parser(
        "snapshot",
        help="Capture one full video frame from a running device and save it.",
    )
    p_snapshot.add_argument("--port", required=True)
    p_snapshot.add_argument(
        "--out",
        default=None,
        help="Output PNG path, or a directory for auto-incrementing "
             "snapshot-NNNN.png (default: ./snapshots/).",
    )
    p_snapshot.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Idle timeout in seconds — give up if no bands arrive for this long "
             "(default: 5).",
    )
    p_snapshot.set_defaults(func=cmd_snapshot)

    p_screendump = sub.add_parser(
        "screendump",
        help="Capture a Legato canvas surface (UI framebuffer) and save it as a PNG.",
    )
    p_screendump.add_argument("--port", required=True)
    p_screendump.add_argument(
        "--canvas",
        default="dash",
        help="Which canvas surface to dump: " + ", ".join(sorted(CANVAS_IDS))
             + ". Each is separate — overlays are not in the base view's buffer "
               "(default: dash).",
    )
    p_screendump.add_argument(
        "--rect",
        default=None,
        help='Sub-rect "x,y,w,h" in surface pixels; w/h 0 means to the edge '
             "(default: the whole surface).",
    )
    p_screendump.add_argument(
        "--out",
        default=None,
        help="Output PNG path, or a directory for auto-incrementing "
             "<canvas>-NNNN.png (default: ./screendumps/).",
    )
    p_screendump.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Idle timeout in seconds — give up if no bands arrive for this long.",
    )
    p_screendump.add_argument(
        "--listen",
        action="store_true",
        help="Don't send the request; just assemble bands triggered elsewhere "
             "(marvin's `perf dump <canvas>` console command). Isolates a broken "
             "command path from a broken emit path.",
    )
    p_screendump.set_defaults(func=cmd_screendump)

    p_score = sub.add_parser(
        "score-capture",
        help="Stream a fixed video sub-region (default: scoring block) to PNGs at full rate.",
    )
    p_score.add_argument("--port", required=True)
    p_score.add_argument(
        "--out",
        default=None,
        help="Directory for auto-incrementing <prefix>-NNNN.png (default: ./scores/).",
    )
    p_score.add_argument(
        "--slot",
        choices=tuple(REGION_SLOT_BY_ID),
        default="score",
        help="Which device region slot to drive: "
             + "; ".join(
                 f"{s.id} = {s.label} {','.join(str(v) for v in s.rect)}"
                 for s in REGION_SLOTS
             )
             + " (default: score). Slots are independent, so several can stream "
               "at once from separate invocations.",
    )
    p_score.add_argument(
        "--rect",
        default=None,
        help="Region as x,y,w,h in the 720x480 frame (default: the chosen slot's "
             f"rect — score is {','.join(str(v) for v in SCORE_BLOCK_RECT)}).",
    )
    p_score.add_argument(
        "--count",
        type=int,
        default=None,
        help="Stop after N frames (default: run until Ctrl-C).",
    )
    p_score.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Idle timeout in seconds — stop if no frames arrive for this long "
             "(default: 5).",
    )
    p_score.set_defaults(func=cmd_score_capture)

    p_export_ml = sub.add_parser(
        "export-ml",
        help="Export a capture as a SensiML-format CSV for MPLAB ML training.",
    )
    p_export_ml.add_argument("capture", help="Capture directory or .bin file")
    p_export_ml.add_argument("--out", required=True, help="Output CSV path")
    p_export_ml.add_argument(
        "--labels",
        choices=("detector", "actuator", "actuator-fb", "detector-fb",
                 "commanded-fb"),
        default="detector",
        help="Label source: 'commanded-fb' = the PREFERRED edge-ai distillation "
             "target (schema v6+): marvin's CV teacher command (released-style, "
             "no legato hold) latched into each frame as commanded_mask, paired "
             "atomically with the ADC scan, plus an fb_seq column; "
             "'detector' = 5 per-fret pressed bits from cv_marvin_v1 (default); "
             "'actuator' = 5 frets + collapsed strum from the timing pipeline's "
             "intended_mask (cross-stream join); 'actuator-fb' = same labels from "
             "the fretboard-reported applied_mask (the node's own model output — "
             "self-label, useful for validation, not the teacher); 'detector-fb' "
             "= diagnostic probe: detector pressed_mask frets + in-frame applied "
             "strum + fb_seq.",
    )
    p_export_ml.add_argument(
        "--strict",
        action="store_true",
        help="Drop fretboard rows that arrive before the first label-source "
             "record (default: emit them with all labels = 0).",
    )
    p_export_ml.set_defaults(func=cmd_export_ml)

    p_export_region = sub.add_parser(
        "export-region",
        help="Extract region/band strips from a capture into numbered PNGs (corpus input).",
    )
    p_export_region.add_argument("capture", help="Capture directory or .bin file")
    p_export_region.add_argument(
        "--out", default=None,
        help="Output directory for <prefix>-NNNN.png (default: ./scores/).",
    )
    p_export_region.add_argument(
        "--kind",
        choices=tuple(_EXPORT_KINDS),
        default="score",
        help="Which strip kind to extract (default: score, the 1-player scoring "
             "block). The 2-player scoreboards and either detector band pair are "
             "pulled out the same way.",
    )
    p_export_region.set_defaults(func=cmd_export_region)

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
