"""Per-record decode round-trip tests."""

from __future__ import annotations

import struct

import pytest

from marvin_perf.decode import DecodeError, decode_record
from marvin_perf.framing import FrameStats, iter_frames
from marvin_perf.records import (
    Detector,
    Drop,
    HDR_SIZE,
    PERF_LOG_HDR_MAGIC,
    Patch,
    PATCH_BYTES,
    RecordType,
    Session,
    Stage,
    Stamp,
    TaskHighwater,
    TaskId,
    Timing,
    UnknownRecord,
)

from .conftest import (
    build_detector_payload,
    build_drop_payload,
    build_header,
    build_patch_payload,
    build_session_payload,
    build_stamp_payload,
    build_task_highwater_payload,
    build_timing_payload,
    wrap_frame,
)


def _decode_only(payload: bytes):
    return decode_record(payload)


def _round_trip_via_iter_frames(payload: bytes):
    """Wrap → iter_frames → decode. Exercises the full pipeline."""
    frame = wrap_frame(payload)
    stats = FrameStats()
    frames = list(iter_frames([frame], stats))
    assert len(frames) == 1
    return decode_record(frames[0].payload)


# ─── Per-type round-trips ────────────────────────────────────────────────────


def test_session_round_trip() -> None:
    payload = build_session_payload(
        timer_freq_hz=266_000_000, schema_version=1, fw_git_short=0xCAFEBABE
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Session)
    assert rec.timer_freq_hz == 266_000_000
    assert rec.schema_version == 1
    assert rec.fw_git_short == 0xCAFEBABE
    assert rec.hdr.magic == PERF_LOG_HDR_MAGIC
    assert rec.hdr.type == int(RecordType.SESSION)


def test_stamp_round_trip() -> None:
    payload = build_stamp_payload(
        stage=Stage.VIDEO_PUBLISH, frame_epoch=42, ts_counter=1_234_567, aux=0xDEAD
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Stamp)
    assert rec.stage_id == int(Stage.VIDEO_PUBLISH)
    assert rec.aux == 0xDEAD
    assert rec.hdr.frame_epoch == 42
    assert rec.hdr.ts_counter == 1_234_567


def test_detector_round_trip() -> None:
    payload = build_detector_payload(
        frame_epoch=7,
        hold_dist=(11, 22, 33, 44, 55),
        edge_dist=(1, 2, 3, 4, 5),
        pressed_mask=0x1F,
        edge_active_mask=0x0A,
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Detector)
    assert rec.hold_dist == (11, 22, 33, 44, 55)
    assert rec.edge_dist == (1, 2, 3, 4, 5)
    assert rec.pressed_mask == 0x1F
    assert rec.edge_active_mask == 0x0A
    assert rec.hdr.frame_epoch == 7


def test_timing_round_trip() -> None:
    payload = build_timing_payload(
        frame_epoch=9, publish_mask=0x03, chord_window_fill=4, fifo_depth=2, strum_dir=1
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Timing)
    assert rec.publish_mask == 0x03
    assert rec.chord_window_fill == 4
    assert rec.fifo_depth == 2
    assert rec.strum_dir == 1


def test_drop_round_trip() -> None:
    payload = build_drop_payload(dropped_state=10, dropped_patch=20, dropped_sink=30)
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Drop)
    assert rec.dropped_state == 10
    assert rec.dropped_patch == 20
    assert rec.dropped_sink == 30


def test_task_highwater_round_trip() -> None:
    payload = build_task_highwater_payload(task_id=int(TaskId.PERF_DRAIN), words=173)
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, TaskHighwater)
    assert rec.task_id == int(TaskId.PERF_DRAIN)
    assert rec.words == 173


def test_patch_round_trip() -> None:
    payload = build_patch_payload(frame_epoch=3)
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Patch)
    assert rec.frame_w == 720
    assert rec.frame_h == 480
    assert len(rec.fret) == 5
    # Spot-check fret 2 — conftest fills hold/edge with constant bytes.
    f2 = rec.fret[2]
    assert f2.hx == 0 + 10 * 2
    assert f2.hold_bgr == bytes((2 * 5 + 1,) * PATCH_BYTES)
    assert f2.edge_bgr == bytes((2 * 5 + 2,) * PATCH_BYTES)


# ─── Forward-compat: unknown record type ─────────────────────────────────────


def test_unknown_record_type_yields_unknown_record() -> None:
    # A header with type=0xFE (no _DISPATCH entry) plus 16 B body — passes the
    # header magic check, fails the dispatch, gets surfaced as UnknownRecord.
    hdr = struct.pack("<HBBIQ", PERF_LOG_HDR_MAGIC, 0xFE, 0, 0, 0)
    payload = hdr + b"\x00" * 16
    rec = _decode_only(payload)
    assert isinstance(rec, UnknownRecord)
    assert rec.hdr.type == 0xFE
    assert rec.raw == payload


# ─── Decode-time error paths ─────────────────────────────────────────────────


def test_bad_magic_raises() -> None:
    hdr = struct.pack("<HBBIQ", 0x1234, int(RecordType.SESSION), 0, 0, 0)
    payload = hdr + b"\x00" * 16
    with pytest.raises(DecodeError):
        decode_record(payload)


def test_truncated_payload_raises() -> None:
    with pytest.raises(DecodeError):
        decode_record(b"\x00" * (HDR_SIZE - 1))


def test_wrong_size_for_known_type_raises() -> None:
    # SESSION says 16-byte body; deliver a 4-byte body instead.
    hdr = build_header(RecordType.SESSION)
    with pytest.raises(DecodeError):
        decode_record(hdr + b"\x00" * 4)
