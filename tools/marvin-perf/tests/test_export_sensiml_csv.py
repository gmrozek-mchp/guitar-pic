"""Tests for marvin_perf.exporters.sensiml_csv."""

from __future__ import annotations

import csv
from pathlib import Path

import pytest

from marvin_perf.capture import BIN_NAME, CaptureSource, finalize_capture_dir, init_capture_dir
from marvin_perf.exporters.sensiml_csv import (
    ExportError,
    export_sensiml_csv,
)

from .conftest import (
    build_detector_payload,
    build_fretboard_raw_payload,
    wrap_frame,
)


_TIMER_HZ = 1_000_000  # 1 MHz → ts_counter == microseconds (easy arithmetic)
_TS_PER_FRAME = _TIMER_HZ // 60  # one video frame at 60 Hz
_TS_PER_FRETBOARD = _TIMER_HZ // 240  # one fretboard tick at 240 Hz


def _write_capture(tmp_path: Path, payloads: list[bytes]) -> Path:
    cap_dir = init_capture_dir(tmp_path / "cap")
    bin_path = cap_dir / BIN_NAME
    with bin_path.open("wb") as fh:
        for p in payloads:
            fh.write(wrap_frame(p))
    finalize_capture_dir(cap_dir, source=CaptureSource(kind="file", file=str(bin_path)))
    return cap_dir


def _build_session_first(ts_counter: int = 0) -> bytes:
    """SESSION record body builder doesn't expose ts_counter; patch it in."""
    import struct
    from marvin_perf.records import PERF_LOG_HDR_MAGIC, RecordType, Session

    hdr = struct.pack(
        "<HBBIQ", PERF_LOG_HDR_MAGIC, int(RecordType.SESSION), 0, 0, ts_counter
    )
    body = Session._BODY.pack(_TIMER_HZ, 3, 0, 0, 0)
    return hdr + body


def _make_capture(tmp_path: Path) -> Path:
    """A small synthetic capture covering the join paths.

    Layout (timer_freq_hz=1 MHz so each ts_counter unit = 1 µs):
      SESSION                                                ts=0
      DETECTOR  epoch=1  pressed=0b00000  (no presses)       ts=10_000
      FRETBOARD_RAW × 4  epoch=1  adc=(100,200,...)          ts=10_000 + 0..3 ticks
      DETECTOR  epoch=2  pressed=0b00001  (green pressed)    ts=10_000 + 1 frame
      FRETBOARD_RAW × 4  epoch=2  adc=(50,...)               ts=...
      DETECTOR  epoch=3  pressed=0b00101  (green+yellow)     ts=...
      FRETBOARD_RAW × 4  epoch=3  adc=(40,...)               ts=...

    First detector record arrives BEFORE the first fretboard record so the
    "skipped pre-detector" path is covered by a separate test.
    """
    payloads: list[bytes] = [_build_session_first(ts_counter=0)]

    base = 10_000
    schedule = [
        (1, 0b00000, (3900, 3950, 3850, 3900, 3900)),
        (2, 0b00001, (1200, 3940, 3840, 3890, 3890)),  # green pressed → low
        (3, 0b00101, (1100, 3935, 1500, 3885, 3885)),  # green + yellow
    ]
    for i, (epoch, pressed, adc) in enumerate(schedule):
        det_ts = base + i * _TS_PER_FRAME
        payloads.append(
            build_detector_payload(
                frame_epoch=epoch,
                pressed_mask=pressed,
                edge_active_mask=0,
            )
        )
        for k in range(4):
            fb_ts = det_ts + k * _TS_PER_FRETBOARD
            payloads.append(
                build_fretboard_raw_payload(
                    frame_epoch=epoch, ts_counter=fb_ts, adc=adc
                )
            )

    return _write_capture(tmp_path, payloads)


def _read_csv(path: Path) -> tuple[list[str], list[list[str]]]:
    with path.open() as fh:
        reader = csv.reader(fh)
        rows = list(reader)
    return rows[0], rows[1:]


# ─── Header / schema ────────────────────────────────────────────────────────


def test_header_matches_documented_schema(tmp_path):
    cap = _make_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out)
    header, _ = _read_csv(out)
    assert header == [
        "timestamp",
        "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
        "label_green", "label_red", "label_yellow", "label_blue", "label_orange",
    ]


# ─── Row count ──────────────────────────────────────────────────────────────


def test_row_count_matches_fretboard_records(tmp_path):
    cap = _make_capture(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out)
    _, rows = _read_csv(out)
    assert len(rows) == 12  # 3 epochs × 4 fretboard records each
    assert stats.n_rows == 12
    assert stats.n_detector_records == 3
    assert stats.n_fretboard_records == 12
    assert stats.n_skipped_unlabeled == 0


# ─── ADC values ─────────────────────────────────────────────────────────────


def test_adc_columns_match_record_values(tmp_path):
    cap = _make_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out)
    _, rows = _read_csv(out)
    # First epoch: adc=(3900, 3950, 3850, 3900, 3900); 4 rows
    for r in rows[:4]:
        assert r[1:6] == ["3900", "3950", "3850", "3900", "3900"]
    # Second epoch: green dipped to 1200
    for r in rows[4:8]:
        assert r[1:6] == ["1200", "3940", "3840", "3890", "3890"]


# ─── Label step-function ────────────────────────────────────────────────────


def test_labels_step_function_on_epoch_boundaries(tmp_path):
    cap = _make_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out)
    _, rows = _read_csv(out)

    # Epoch 1: pressed=0b00000 → all labels 0
    for r in rows[:4]:
        assert r[6:11] == ["0", "0", "0", "0", "0"]
    # Epoch 2: pressed=0b00001 → only green=1
    for r in rows[4:8]:
        assert r[6:11] == ["1", "0", "0", "0", "0"]
    # Epoch 3: pressed=0b00101 → green and yellow
    for r in rows[8:12]:
        assert r[6:11] == ["1", "0", "1", "0", "0"]


# ─── Timestamp arithmetic ───────────────────────────────────────────────────


def test_timestamps_monotonic_and_first_row_is_zero(tmp_path):
    cap = _make_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out)
    _, rows = _read_csv(out)
    timestamps = [float(r[0]) for r in rows]
    # Strictly monotonic
    assert all(timestamps[i] < timestamps[i + 1] for i in range(len(timestamps) - 1))
    # First emitted row is t=0 (SensiML "elapsed since logging started")
    assert timestamps[0] == pytest.approx(0.0, abs=1e-9)
    # Each fretboard tick is 1/240 s ≈ 4166.67 µs apart within a frame
    assert (timestamps[1] - timestamps[0]) == pytest.approx(1 / 240.0, abs=1e-6)


# ─── Pre-detector edge case ─────────────────────────────────────────────────


def _capture_with_fretboard_before_detector(tmp_path: Path) -> Path:
    """FRETBOARD_RAW arrives before any DETECTOR record."""
    payloads = [
        _build_session_first(ts_counter=0),
        # Two unlabelled fretboard rows first
        build_fretboard_raw_payload(
            frame_epoch=0, ts_counter=5_000, adc=(3900, 3900, 3900, 3900, 3900)
        ),
        build_fretboard_raw_payload(
            frame_epoch=0, ts_counter=6_000, adc=(3899, 3899, 3899, 3899, 3899)
        ),
        # Then a detector + fretboard pair
        build_detector_payload(frame_epoch=1, pressed_mask=0b00010, edge_active_mask=0),
        build_fretboard_raw_payload(
            frame_epoch=1, ts_counter=10_000, adc=(3895, 1200, 3895, 3895, 3895)
        ),
    ]
    return _write_capture(tmp_path, payloads)


def test_unlabeled_rows_emitted_with_zero_labels_by_default(tmp_path):
    cap = _capture_with_fretboard_before_detector(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out)  # strict=False (default)
    _, rows = _read_csv(out)
    assert stats.n_rows == 3
    assert len(rows) == 3
    # First two rows: no detector yet → all labels 0
    for r in rows[:2]:
        assert r[6:11] == ["0", "0", "0", "0", "0"]
    # Third row: detector said red pressed → label_red=1
    assert rows[2][6:11] == ["0", "1", "0", "0", "0"]


def test_strict_mode_drops_unlabeled_rows(tmp_path):
    cap = _capture_with_fretboard_before_detector(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, strict=True)
    _, rows = _read_csv(out)
    assert stats.n_rows == 1
    assert stats.n_skipped_unlabeled == 2
    assert len(rows) == 1
    assert rows[0][6:11] == ["0", "1", "0", "0", "0"]


# ─── Missing-SESSION error ──────────────────────────────────────────────────


def test_missing_session_raises_export_error(tmp_path):
    payloads = [
        # No SESSION record — just detector + fretboard
        build_detector_payload(frame_epoch=1, pressed_mask=0b00001),
        build_fretboard_raw_payload(frame_epoch=1, ts_counter=10_000),
    ]
    cap = _write_capture(tmp_path, payloads)
    out = tmp_path / "out.csv"
    with pytest.raises(ExportError):
        export_sensiml_csv(cap, out)
    # Partial file should be cleaned up
    assert not out.exists()
