"""Per-record decode round-trip tests."""

from __future__ import annotations

import struct

import pytest

from marvin_perf.decode import DecodeError, decode_record
from marvin_perf.framing import FrameStats, iter_frames
from marvin_perf.records import (
    Actuator,
    ActuatorProducer,
    Detector,
    DetectorConfig,
    Drop,
    FRET_COUNT,
    FretboardRaw,
    HDR_SIZE,
    PERF_LOG_HDR_MAGIC,
    RecordType,
    Session,
    Stage,
    Stamp,
    Strip,
    StripKind,
    TaskHighwater,
    TaskId,
    TaskRuntime,
    TaskState,
    Timing,
    UnknownRecord,
)

from .conftest import (
    build_detector_payload,
    build_drop_payload,
    build_fretboard_raw_payload,
    build_header,
    build_session_payload,
    build_stamp_payload,
    build_strip_payload,
    build_task_highwater_payload,
    build_task_runtime_payload,
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
        timer_freq_hz=266_000_000, schema_version=3, fw_git_short=0xCAFEBABE
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Session)
    assert rec.timer_freq_hz == 266_000_000
    assert rec.schema_version == 3
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
        frame_epoch=9,
        now_ms=12_345,
        chord_open=1,
        chord_mask=0x05,
        chord_age_ms=12,
        note_q_count=3,
        note_head_mask=0x01,
        note_tail_mask=0x07,
        note_head_at_ms=12_555,
        strum_q_count=2,
        strum_head_mask=0x05,
        strum_dir_next=2,
        strum_head_at_ms=12_580,
        frets_active=0x03,
        strum_active=1,
        release_pending_mask=0x10,
        publish_mask=0x23,
        strum_release_at_ms=12_400,
        release_min_at_ms=12_700,
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Timing)
    assert rec.now_ms == 12_345
    assert rec.chord_open == 1
    assert rec.chord_mask == 0x05
    assert rec.chord_age_ms == 12
    assert rec.note_q_count == 3
    assert rec.note_head_mask == 0x01
    assert rec.note_tail_mask == 0x07
    assert rec.note_head_at_ms == 12_555
    assert rec.strum_q_count == 2
    assert rec.strum_head_mask == 0x05
    assert rec.strum_dir_next == 2
    assert rec.strum_head_at_ms == 12_580
    assert rec.frets_active == 0x03
    assert rec.strum_active == 1
    assert rec.release_pending_mask == 0x10
    assert rec.publish_mask == 0x23
    assert rec.strum_release_at_ms == 12_400
    assert rec.release_min_at_ms == 12_700


def test_drop_round_trip() -> None:
    payload = build_drop_payload(dropped_state=10, dropped_strip=20, dropped_sink=30)
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Drop)
    assert rec.dropped_state == 10
    assert rec.dropped_strip == 20
    assert rec.dropped_sink == 30


def test_task_highwater_round_trip() -> None:
    payload = build_task_highwater_payload(task_id=int(TaskId.PERF_DRAIN), words=173)
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, TaskHighwater)
    assert rec.task_id == int(TaskId.PERF_DRAIN)
    assert rec.words == 173


def test_task_runtime_round_trip() -> None:
    payload = build_task_runtime_payload(
        task_id=int(TaskId.PERF_DRAIN),
        state=int(TaskState.BLOCKED),
        priority=3,
        run_time_counter=12345,
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, TaskRuntime)
    assert rec.task_id == int(TaskId.PERF_DRAIN)
    assert rec.state == int(TaskState.BLOCKED)
    assert rec.priority == 3
    assert rec.run_time_counter == 12345


def test_strip_round_trip() -> None:
    payload = build_strip_payload(
        frame_epoch=11, kind=int(StripKind.SENSING), x=240, y=295, w=4, h=2, fill=0x42
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Strip)
    assert rec.kind == int(StripKind.SENSING)
    assert rec.kind_name == "sensing"
    assert (rec.x, rec.y, rec.w, rec.h) == (240, 295, 4, 2)
    assert rec.bgr == bytes((0x42,) * (4 * 2 * 3))
    assert rec.hdr.frame_epoch == 11


def test_strip_2p_kinds_round_trip() -> None:
    """The per-highway band pair and the two 2-player scoreboards decode by name."""
    for kind, name, rect in (
        (StripKind.SENSING_2P, "sensing_2p", (150, 300, 155, 32)),
        (StripKind.STRIKE_2P, "strike_2p", (120, 395, 205, 34)),
        (StripKind.SCORE_2P_LEFT, "score_2p_left", (128, 164, 68, 78)),
        (StripKind.SCORE_2P_RIGHT, "score_2p_right", (515, 164, 68, 78)),
    ):
        x, y, w, h = rect
        rec = _round_trip_via_iter_frames(
            build_strip_payload(frame_epoch=7, kind=int(kind), x=x, y=y, w=w, h=h, fill=0x5A)
        )
        assert isinstance(rec, Strip)
        assert rec.kind == int(kind)
        assert rec.kind_name == name
        assert (rec.x, rec.y, rec.w, rec.h) == rect
        assert len(rec.bgr) == w * h * 3


def test_strip_unknown_kind_renders_as_kind_n() -> None:
    payload = build_strip_payload(kind=99, w=1, h=1)
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Strip)
    assert rec.kind == 99
    assert rec.kind_name == "kind_99"


def test_strip_dimension_mismatch_raises() -> None:
    # Build a strip with header claiming 4×2 but supply only 4 pixel bytes.
    from marvin_perf.records import _STRIP_BODY
    body = _STRIP_BODY.pack(0, 0, 4, 2, 0, 0, b"\x00\x00")
    bgr = b"\x00" * 4  # truncated
    payload = build_header(RecordType.STRIP) + body + bgr
    with pytest.raises(DecodeError):
        decode_record(payload)


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


# ─── v3 records: DETECTOR_CONFIG and ACTUATOR ────────────────────────────────


def test_detector_config_round_trip() -> None:
    n = FRET_COUNT
    hx = (280, 317, 355, 393, 430)
    hy = (311, 311, 311, 311, 311)
    ex = (293, 330, 368, 380, 417)
    ey = (311, 311, 311, 311, 311)
    tb = (0.0, 0.0, 0.0, 1.0, 0.0)
    tg = (1.0, 0.0, 0.5, 0.4, 0.3)
    tr = (0.0, 1.0, 0.5, 0.0, 0.7)
    rb = (0.7, 0.7, 1.4, 0.0, 1.4)
    rg = (0.0, 0.7, 0.0, 0.0, 0.0)
    rr = (0.7, 0.0, 0.0, 1.4, 0.0)
    body = DetectorConfig._BODY.pack(
        *hx, *hy, *ex, *ey,
        100.0, 0.78, 25.0,
        *tb, *tg, *tr, *rb, *rg, *rr,
    )
    payload = build_header(RecordType.DETECTOR_CONFIG) + body
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, DetectorConfig)
    assert rec.sensor_hx == hx
    assert rec.sensor_hy == hy
    assert rec.sensor_ex == ex
    assert rec.sensor_ey == ey
    assert rec.hold_thresh == pytest.approx(100.0)
    assert rec.hold_release_frac == pytest.approx(0.78)
    assert rec.edge_thresh == pytest.approx(25.0)
    for got, want in zip(rec.color_target_b, tb):
        assert got == pytest.approx(want)
    for got, want in zip(rec.color_reject_r, rr):
        assert got == pytest.approx(want)
    assert len(rec.color_target_g) == n


def test_actuator_round_trip() -> None:
    body = Actuator._BODY.pack(
        0x23,                                    # intended_mask
        0x21,                                    # asserted_mask (last actually-sent)
        2,                                       # strum_dir = up
        int(ActuatorProducer.TIMING),
        0,                                       # last_ack_result = SUCCESS
        0xDEADBEEFCAFE,                          # last_ack_ts_counter
    )
    payload = build_header(RecordType.ACTUATOR) + body
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, Actuator)
    assert rec.intended_mask == 0x23
    assert rec.asserted_mask == 0x21
    assert rec.strum_dir == 2
    assert rec.producer_id == int(ActuatorProducer.TIMING)
    assert rec.producer_name == "timing"
    assert rec.last_ack_result == 0
    assert rec.last_ack_ts_counter == 0xDEADBEEFCAFE


def test_fretboard_raw_round_trip() -> None:
    payload = build_fretboard_raw_payload(
        frame_epoch=7,
        adc=(100, 200, 300, 400, 500),
        fb_sample_seq=0xCAFEBABE,
        applied_mask=0x61,  # green + strum-down + strum-up bits set
    )
    rec = _round_trip_via_iter_frames(payload)
    assert isinstance(rec, FretboardRaw)
    assert rec.adc == (100, 200, 300, 400, 500)
    assert rec.fb_sample_seq == 0xCAFEBABE
    assert rec.applied_mask == 0x61
    assert rec.hdr.frame_epoch == 7
    assert FretboardRaw.SIZE == 32
