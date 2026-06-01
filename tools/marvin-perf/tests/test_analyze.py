"""Analysis pass tests: latency, drops, HWM, schema/cadence checks."""

from __future__ import annotations

import pytest

from marvin_perf.analyze import (
    SchemaVersionMismatch,
    check_frame_epoch_monotonic,
    check_schema,
    check_video_publish_cadence,
    compute_cpu_snapshot,
    compute_drops,
    compute_hwm,
    compute_latencies,
    find_session,
)
from marvin_perf.records import (
    Detector,
    Drop,
    Header,
    PERF_LOG_HDR_MAGIC,
    RecordType,
    Session,
    Stage,
    Stamp,
    TaskHighwater,
    TaskId,
    TaskRuntime,
)


def _hdr(record_type: RecordType, *, frame_epoch: int = 0, ts: int = 0) -> Header:
    return Header(
        magic=PERF_LOG_HDR_MAGIC,
        type=int(record_type),
        flags=0,
        frame_epoch=frame_epoch,
        ts_counter=ts,
    )


def _stamp(stage: Stage, *, epoch: int, ts: int) -> Stamp:
    return Stamp(hdr=_hdr(RecordType.STAMP, frame_epoch=epoch, ts=ts),
                 stage_id=int(stage), aux=0)


def _session(*, schema: int = 2, freq: int = 1_000_000) -> Session:
    return Session(
        hdr=_hdr(RecordType.SESSION),
        timer_freq_hz=freq,
        schema_version=schema,
        fw_git_short=0,
    )


# ─── Latency ─────────────────────────────────────────────────────────────────


def test_compute_latencies_single_frame_ticks_to_microseconds() -> None:
    # 1 MHz timer => 1 tick = 1 µs.
    records = [
        _stamp(Stage.ISC_IRQ, epoch=1, ts=0),
        _stamp(Stage.VIDEO_PUBLISH, epoch=1, ts=500),
        _stamp(Stage.CV_START, epoch=1, ts=600),
        _stamp(Stage.CV_END, epoch=1, ts=2_600),
        _stamp(Stage.TP_TICK, epoch=1, ts=2_700),
        _stamp(Stage.FBL_SEND, epoch=1, ts=2_800),
        _stamp(Stage.CDC_WRITE_COMPLETE, epoch=1, ts=2_900),
    ]
    hists = compute_latencies(records, timer_freq_hz=1_000_000)
    by_pair = {h.pair: h for h in hists}
    assert by_pair[(Stage.ISC_IRQ, Stage.VIDEO_PUBLISH)].samples_us == [500.0]
    assert by_pair[(Stage.VIDEO_PUBLISH, Stage.CV_START)].samples_us == [100.0]
    assert by_pair[(Stage.CV_START, Stage.CV_END)].samples_us == [2_000.0]
    assert by_pair[(Stage.CV_END, Stage.TP_TICK)].samples_us == [100.0]


def test_compute_latencies_p50_p95_p99_max() -> None:
    # Build 100 frames where ISC→VIDEO delta = epoch (1..100).
    records = []
    for epoch in range(1, 101):
        records.append(_stamp(Stage.ISC_IRQ, epoch=epoch, ts=0))
        records.append(_stamp(Stage.VIDEO_PUBLISH, epoch=epoch, ts=epoch))
    hists = compute_latencies(records, timer_freq_hz=1_000_000)
    h = next(x for x in hists if x.pair == (Stage.ISC_IRQ, Stage.VIDEO_PUBLISH))
    assert h.n == 100
    # Nearest-rank percentile with rank = int(p * n): p50 → index 50 → value 51.
    assert h.p50 == 51.0
    assert h.p95 == 96.0
    assert h.p99 == 100.0
    assert h.max == 100.0


def test_compute_latencies_skips_frames_missing_stages() -> None:
    records = [
        # Frame 1: complete pair
        _stamp(Stage.ISC_IRQ, epoch=1, ts=0),
        _stamp(Stage.VIDEO_PUBLISH, epoch=1, ts=100),
        # Frame 2: only one stage — should not contribute
        _stamp(Stage.ISC_IRQ, epoch=2, ts=0),
    ]
    hists = compute_latencies(records, timer_freq_hz=1_000_000)
    h = next(x for x in hists if x.pair == (Stage.ISC_IRQ, Stage.VIDEO_PUBLISH))
    assert h.n == 1
    assert h.samples_us == [100.0]


def test_compute_latencies_zero_freq_returns_empty_hists() -> None:
    records = [
        _stamp(Stage.ISC_IRQ, epoch=1, ts=0),
        _stamp(Stage.VIDEO_PUBLISH, epoch=1, ts=100),
    ]
    hists = compute_latencies(records, timer_freq_hz=0)
    assert all(h.n == 0 for h in hists)


# ─── Drops ───────────────────────────────────────────────────────────────────


def test_compute_drops_tracks_max_delta_and_final() -> None:
    drops = [
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=0, dropped_strip=0, dropped_sink=0),
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=2, dropped_strip=1, dropped_sink=0),
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=2, dropped_strip=5, dropped_sink=10),
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=3, dropped_strip=5, dropped_sink=10),
    ]
    summary = compute_drops(drops)
    assert summary.n_drop_records == 4
    assert summary.final_state == 3
    assert summary.final_strip == 5
    assert summary.final_sink_bytes == 10
    assert summary.max_state_delta == 2
    assert summary.max_strip_delta == 4
    assert summary.max_sink_bytes_delta == 10
    # Baseline = first DROP (all zeros) so since-session == cumulative here.
    assert summary.since_session_state == 3
    assert summary.since_session_strip == 5
    assert summary.since_session_sink_bytes == 10


def test_compute_drops_baseline_subtracts_pre_attach_carry() -> None:
    # Mid-session attach: first DROP arrives with non-zero counters because
    # the firmware has been dropping records to /dev/null since boot. The
    # since-session view should report Δ0 / Δ0 / Δ0 until something new
    # actually fails post-attach.
    drops = [
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=0, dropped_strip=0, dropped_sink=8384),
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=0, dropped_strip=0, dropped_sink=8384),
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=0, dropped_strip=0, dropped_sink=8384),
    ]
    summary = compute_drops(drops)
    assert summary.final_sink_bytes == 8384
    assert summary.since_session_sink_bytes == 0
    assert summary.since_session_state == 0
    assert summary.since_session_strip == 0


def test_compute_drops_no_drop_records() -> None:
    summary = compute_drops([_session()])
    assert summary.n_drop_records == 0
    assert summary.final_state == 0
    assert summary.since_session_state == 0
    assert summary.since_session_sink_bytes == 0


# ─── HWM ─────────────────────────────────────────────────────────────────────


def test_compute_hwm_groups_by_task_id() -> None:
    records = [
        TaskHighwater(hdr=_hdr(RecordType.TASK_HIGHWATER, ts=10),
                      task_id=int(TaskId.PERF_DRAIN), words=200),
        TaskHighwater(hdr=_hdr(RecordType.TASK_HIGHWATER, ts=20),
                      task_id=int(TaskId.PERF_DRAIN), words=180),
        TaskHighwater(hdr=_hdr(RecordType.TASK_HIGHWATER, ts=30),
                      task_id=int(TaskId.VIDEO), words=300),
    ]
    hwm = compute_hwm(records)
    assert set(hwm) == {int(TaskId.PERF_DRAIN), int(TaskId.VIDEO)}
    perf = hwm[int(TaskId.PERF_DRAIN)]
    assert perf.task_name == "PERF_DRAIN"
    assert perf.min_words == 180
    assert perf.samples == [(10, 200), (20, 180)]


# ─── Sanity checks ───────────────────────────────────────────────────────────


def test_check_schema_match_passes() -> None:
    check_schema(_session(schema=3))  # no exception


def test_check_schema_mismatch_raises() -> None:
    with pytest.raises(SchemaVersionMismatch):
        check_schema(_session(schema=1))


def test_check_schema_no_session_is_ok() -> None:
    # Mid-stream attach: no SESSION yet — should not raise.
    check_schema(None)


def test_find_session_returns_first() -> None:
    a = _session(schema=1)
    b = _session(schema=1, freq=2)
    found = find_session([_stamp(Stage.ISC_IRQ, epoch=1, ts=0), a, b])
    assert found is a


def test_find_session_returns_none_when_absent() -> None:
    assert find_session([_stamp(Stage.ISC_IRQ, epoch=1, ts=0)]) is None


def test_frame_epoch_monotonic_clean() -> None:
    records = [
        _stamp(Stage.ISC_IRQ, epoch=1, ts=0),
        _stamp(Stage.VIDEO_PUBLISH, epoch=1, ts=10),
        _stamp(Stage.ISC_IRQ, epoch=2, ts=20),
    ]
    assert check_frame_epoch_monotonic(records) == []


def test_frame_epoch_monotonic_flags_backwards() -> None:
    records = [
        _stamp(Stage.ISC_IRQ, epoch=5, ts=0),
        _stamp(Stage.ISC_IRQ, epoch=3, ts=10),  # backwards
    ]
    warns = check_frame_epoch_monotonic(records)
    assert len(warns) == 1
    assert warns[0].kind == "frame_epoch_backwards"


def test_frame_epoch_monotonic_skips_session_drop_hwm() -> None:
    # SESSION/DROP/TASK_HIGHWATER ride at frame_epoch=0 — must not trip.
    records = [
        _stamp(Stage.ISC_IRQ, epoch=10, ts=0),
        Drop(hdr=_hdr(RecordType.DROP), dropped_state=0, dropped_strip=0, dropped_sink=0),
        _stamp(Stage.VIDEO_PUBLISH, epoch=10, ts=10),
    ]
    assert check_frame_epoch_monotonic(records) == []


def test_video_publish_cadence_within_tolerance() -> None:
    # 60 Hz at 1 MHz timer = 16_667 ticks per frame.
    records = [
        _stamp(Stage.VIDEO_PUBLISH, epoch=i, ts=i * 16_667) for i in range(1, 11)
    ]
    warns = check_video_publish_cadence(records, timer_freq_hz=1_000_000)
    assert warns == []


def test_video_publish_cadence_flags_outlier() -> None:
    # Inject a 25 ms gap between two stamps — should breach the ±2 ms band.
    records = [
        _stamp(Stage.VIDEO_PUBLISH, epoch=1, ts=0),
        _stamp(Stage.VIDEO_PUBLISH, epoch=2, ts=25_000),
    ]
    warns = check_video_publish_cadence(records, timer_freq_hz=1_000_000)
    assert len(warns) == 1
    assert warns[0].kind == "video_publish_jitter"


def test_video_publish_cadence_zero_freq_no_warns() -> None:
    records = [_stamp(Stage.VIDEO_PUBLISH, epoch=1, ts=0)]
    assert check_video_publish_cadence(records, timer_freq_hz=0) == []


# ─── CPU snapshot ────────────────────────────────────────────────────────────


def _runtime(task_id: int, *, ts: int, run: int, prio: int = 1) -> TaskRuntime:
    return TaskRuntime(
        hdr=_hdr(RecordType.TASK_RUNTIME, ts=ts),
        task_id=task_id,
        state=0,
        priority=prio,
        run_time_counter=run,
    )


def test_compute_cpu_snapshot_simple_three_tasks() -> None:
    # Two emissions one second apart at a 1 MHz timer. Across the window
    # CV_MARVIN_V1 spent 700_000 ticks, PERF_DRAIN 270_000, IDLE 30_000 →
    # 70 / 27 / 3 % CPU.
    recs = [
        _runtime(int(TaskId.CV_MARVIN_V1), ts=0,         run=0),
        _runtime(int(TaskId.PERF_DRAIN),   ts=0,         run=0),
        _runtime(int(TaskId.IDLE),         ts=0,         run=0),
        _runtime(int(TaskId.CV_MARVIN_V1), ts=1_000_000, run=700_000),
        _runtime(int(TaskId.PERF_DRAIN),   ts=1_000_000, run=270_000),
        _runtime(int(TaskId.IDLE),         ts=1_000_000, run=30_000),
    ]
    cpu = compute_cpu_snapshot(recs)
    assert cpu[int(TaskId.CV_MARVIN_V1)].cpu_pct == pytest.approx(70.0, abs=1e-9)
    assert cpu[int(TaskId.PERF_DRAIN)].cpu_pct == pytest.approx(27.0, abs=1e-9)
    assert cpu[int(TaskId.IDLE)].cpu_pct == pytest.approx(3.0, abs=1e-9)
    # Σ = 100 % within rounding.
    assert sum(s.cpu_pct for s in cpu.values()) == pytest.approx(100.0, abs=1e-9)


def test_compute_cpu_snapshot_uses_last_two_only() -> None:
    # Three emissions: only the last two count toward the snapshot.
    recs = [
        _runtime(int(TaskId.CV_MARVIN_V1), ts=0,   run=0),
        _runtime(int(TaskId.IDLE),         ts=0,   run=0),
        _runtime(int(TaskId.CV_MARVIN_V1), ts=100, run=10),    # ignored
        _runtime(int(TaskId.IDLE),         ts=100, run=90),    # ignored
        _runtime(int(TaskId.CV_MARVIN_V1), ts=200, run=60),    # Δ=50
        _runtime(int(TaskId.IDLE),         ts=200, run=140),   # Δ=50
    ]
    cpu = compute_cpu_snapshot(recs)
    assert cpu[int(TaskId.CV_MARVIN_V1)].cpu_pct == pytest.approx(50.0, abs=1e-9)
    assert cpu[int(TaskId.IDLE)].cpu_pct == pytest.approx(50.0, abs=1e-9)


def test_compute_cpu_snapshot_uint32_wrap() -> None:
    # run_time_counter is uint32; wrap from 0xFFFFFFF0 to 0x0000000F is Δ=31.
    recs = [
        _runtime(int(TaskId.CV_MARVIN_V1), ts=0,   run=0xFFFFFFF0),
        _runtime(int(TaskId.IDLE),         ts=0,   run=0),
        _runtime(int(TaskId.CV_MARVIN_V1), ts=100, run=0x0000000F),
        _runtime(int(TaskId.IDLE),         ts=100, run=69),
    ]
    cpu = compute_cpu_snapshot(recs)
    # Δcv = 31, Δidle = 69 → 31% / 69%.
    assert cpu[int(TaskId.CV_MARVIN_V1)].run_time_delta == 31
    assert cpu[int(TaskId.CV_MARVIN_V1)].cpu_pct == pytest.approx(31.0, abs=1e-9)
    assert cpu[int(TaskId.IDLE)].cpu_pct == pytest.approx(69.0, abs=1e-9)


def test_compute_cpu_snapshot_single_sample_returns_empty() -> None:
    recs = [_runtime(int(TaskId.CV_MARVIN_V1), ts=0, run=0)]
    assert compute_cpu_snapshot(recs) == {}


def test_compute_cpu_snapshot_no_runtime_records_returns_empty() -> None:
    recs = [_stamp(Stage.ISC_IRQ, epoch=1, ts=0)]
    assert compute_cpu_snapshot(recs) == {}
