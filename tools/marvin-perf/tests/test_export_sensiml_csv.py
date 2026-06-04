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
    build_actuator_payload,
    build_detector_payload,
    build_fretboard_raw_payload,
    wrap_frame,
)


# Wire-byte strum bits: bit 5 = strum-down, bit 6 = strum-up.
_STRUM_DOWN = 1 << 5  # 0x20
_STRUM_UP = 1 << 6    # 0x40


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


# ─── labels="actuator" path ──────────────────────────────────────────────────


def _make_actuator_capture(tmp_path: Path) -> Path:
    """SESSION + ACTUATOR + FRETBOARD_RAW covering frets and both strum dirs.

      epoch 1  mask=0                      → frets 00000, strum 0
      epoch 2  mask=green                  → frets 10000, strum 0
      epoch 3  mask=green|strum-down       → frets 10000, strum 1  (event 1)
      epoch 4  mask=green                  → frets 10000, strum 0
      epoch 5  mask=green|yellow|strum-up  → frets 10100, strum 1  (event 2)

    One ACTUATOR record then 4 FRETBOARD_RAW per epoch (240 Hz vs 60 Hz).
    """
    payloads: list[bytes] = [_build_session_first(ts_counter=0)]
    base = 10_000
    adc = (1234, 2345, 3456, 1500, 2500)
    masks = [
        (1, 0x00),
        (2, 0x01),
        (3, 0x01 | _STRUM_DOWN),
        (4, 0x01),
        (5, 0x01 | 0x04 | _STRUM_UP),
    ]
    for i, (epoch, mask) in enumerate(masks):
        act_ts = base + i * _TS_PER_FRAME
        payloads.append(
            build_actuator_payload(
                frame_epoch=epoch, ts_counter=act_ts, intended_mask=mask
            )
        )
        for k in range(4):
            fb_ts = act_ts + k * _TS_PER_FRETBOARD
            payloads.append(
                build_fretboard_raw_payload(
                    frame_epoch=epoch, ts_counter=fb_ts, adc=adc
                )
            )
    return _write_capture(tmp_path, payloads)


def test_actuator_header_matches_documented_schema(tmp_path):
    cap = _make_actuator_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out, labels="actuator")
    header, _ = _read_csv(out)
    assert header == [
        "timestamp",
        "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
        "fret_green", "fret_red", "fret_yellow", "fret_blue", "fret_orange",
        "strum",
    ]


def test_actuator_fret_and_strum_labels(tmp_path):
    cap = _make_actuator_capture(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator")
    _, rows = _read_csv(out)
    assert len(rows) == 20  # 5 epochs × 4 fretboard each
    # epoch 1: idle
    for r in rows[0:4]:
        assert r[6:12] == ["0", "0", "0", "0", "0", "0"]
    # epoch 2: green held, no strum
    for r in rows[4:8]:
        assert r[6:12] == ["1", "0", "0", "0", "0", "0"]
    # epoch 3: green + strum-down → strum collapses to 1
    for r in rows[8:12]:
        assert r[6:12] == ["1", "0", "0", "0", "0", "1"]
    # epoch 4: green, strum released
    for r in rows[12:16]:
        assert r[6:12] == ["1", "0", "0", "0", "0", "0"]
    # epoch 5: green + yellow + strum-up → strum collapses to 1
    for r in rows[16:20]:
        assert r[6:12] == ["1", "0", "1", "0", "0", "1"]
    assert stats.labels == "actuator"
    assert stats.n_actuator_records == 5
    assert stats.n_fretboard_records == 20
    assert stats.n_rows == 20


def test_actuator_strum_event_count_is_rising_edges(tmp_path):
    cap = _make_actuator_capture(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator")
    # Two distinct strums (epoch 3 down, epoch 5 up), each a 0→1 rising edge.
    assert stats.n_strum_events == 2


def test_actuator_adc_columns_match(tmp_path):
    cap = _make_actuator_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out, labels="actuator")
    _, rows = _read_csv(out)
    for r in rows:
        assert r[1:6] == ["1234", "2345", "3456", "1500", "2500"]


def test_actuator_strict_drops_rows_before_first_actuator(tmp_path):
    payloads = [
        _build_session_first(ts_counter=0),
        build_fretboard_raw_payload(frame_epoch=0, ts_counter=5_000, adc=(1, 2, 3, 4, 5)),
        build_fretboard_raw_payload(frame_epoch=0, ts_counter=6_000, adc=(1, 2, 3, 4, 5)),
        build_actuator_payload(frame_epoch=1, ts_counter=10_000, intended_mask=0x02),
        build_fretboard_raw_payload(frame_epoch=1, ts_counter=11_000, adc=(1, 2, 3, 4, 5)),
    ]
    cap = _write_capture(tmp_path, payloads)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator", strict=True)
    _, rows = _read_csv(out)
    assert stats.n_skipped_unlabeled == 2
    assert len(rows) == 1
    # red bit set, no strum
    assert rows[0][6:12] == ["0", "1", "0", "0", "0", "0"]


def test_actuator_unlabeled_rows_zero_by_default(tmp_path):
    payloads = [
        _build_session_first(ts_counter=0),
        build_fretboard_raw_payload(frame_epoch=0, ts_counter=5_000, adc=(1, 2, 3, 4, 5)),
        build_actuator_payload(frame_epoch=1, ts_counter=10_000, intended_mask=0x02),
        build_fretboard_raw_payload(frame_epoch=1, ts_counter=11_000, adc=(1, 2, 3, 4, 5)),
    ]
    cap = _write_capture(tmp_path, payloads)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator")  # strict=False
    _, rows = _read_csv(out)
    assert len(rows) == 2
    assert rows[0][6:12] == ["0", "0", "0", "0", "0", "0"]  # no actuator yet
    assert rows[1][6:12] == ["0", "1", "0", "0", "0", "0"]  # red pressed
    assert stats.n_skipped_unlabeled == 0


def test_unknown_labels_mode_raises(tmp_path):
    cap = _make_actuator_capture(tmp_path)
    out = tmp_path / "out.csv"
    with pytest.raises(ExportError):
        export_sensiml_csv(cap, out, labels="bogus")


def test_detector_mode_unaffected_by_actuator_records(tmp_path):
    """An ACTUATOR record present in the capture must not perturb the
    detector label path (back-compat)."""
    payloads = [
        _build_session_first(ts_counter=0),
        build_detector_payload(frame_epoch=1, pressed_mask=0b00001, edge_active_mask=0),
        build_actuator_payload(frame_epoch=1, ts_counter=9_000, intended_mask=0x7F),
        build_fretboard_raw_payload(frame_epoch=1, ts_counter=10_000, adc=(1, 2, 3, 4, 5)),
    ]
    cap = _write_capture(tmp_path, payloads)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out)  # default detector mode
    header, rows = _read_csv(out)
    assert header[-1] == "label_orange"  # detector schema, no strum column
    assert len(rows) == 1
    assert rows[0][6:11] == ["1", "0", "0", "0", "0"]
    assert stats.labels == "detector"


# ─── labels="actuator-fb" path (in-frame applied_mask + fb_seq) ───────────────


def _make_actuator_fb_capture(tmp_path: Path, *, gap_after: int | None = None) -> Path:
    """SESSION + FRETBOARD_RAW frames carrying their own applied_mask + fb_seq.

    No ACTUATOR records — labels come from each frame's in-frame mask. With
    `gap_after` set, the fb_seq counter skips a value after that index to
    simulate a frame dropped in transit.
    """
    payloads: list[bytes] = [_build_session_first(ts_counter=0)]
    base = 10_000
    adc = (1234, 2345, 3456, 1500, 2500)
    masks = [
        0x00,                          # idle, strum 0
        0x01,                          # green, strum 0
        0x01 | _STRUM_DOWN,            # green + strum-down → strum 1 (event 1)
        0x01,                          # green, strum 0
        0x01 | 0x04 | _STRUM_UP,       # green+yellow+strum-up → strum 1 (event 2)
    ]
    seq = 0
    for i, mask in enumerate(masks):
        payloads.append(
            build_fretboard_raw_payload(
                frame_epoch=1, ts_counter=base + i * _TS_PER_FRETBOARD, adc=adc,
                fb_sample_seq=seq, applied_mask=mask,
            )
        )
        seq += 1
        if gap_after is not None and i == gap_after:
            seq += 1  # skip a sequence number → simulated dropped frame
    return _write_capture(tmp_path, payloads)


def test_actuator_fb_header_has_fb_seq(tmp_path):
    cap = _make_actuator_fb_capture(tmp_path)
    out = tmp_path / "out.csv"
    export_sensiml_csv(cap, out, labels="actuator-fb")
    header, _ = _read_csv(out)
    assert header == [
        "timestamp", "fb_seq",
        "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
        "fret_green", "fret_red", "fret_yellow", "fret_blue", "fret_orange",
        "strum",
    ]


def test_actuator_fb_labels_from_in_frame_mask(tmp_path):
    cap = _make_actuator_fb_capture(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator-fb")
    _, rows = _read_csv(out)
    assert len(rows) == 5
    # fb_seq column is monotonic 0..4
    assert [r[1] for r in rows] == ["0", "1", "2", "3", "4"]
    # frets+strum (cols 7..12) follow each frame's applied_mask
    assert rows[0][7:13] == ["0", "0", "0", "0", "0", "0"]
    assert rows[1][7:13] == ["1", "0", "0", "0", "0", "0"]
    assert rows[2][7:13] == ["1", "0", "0", "0", "0", "1"]  # strum-down collapses
    assert rows[3][7:13] == ["1", "0", "0", "0", "0", "0"]
    assert rows[4][7:13] == ["1", "0", "1", "0", "0", "1"]  # strum-up collapses
    assert stats.labels == "actuator-fb"
    assert stats.n_strum_events == 2
    assert stats.n_seq_gaps == 0


def test_actuator_fb_detects_seq_gap(tmp_path):
    cap = _make_actuator_fb_capture(tmp_path, gap_after=1)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator-fb")
    _, rows = _read_csv(out)
    # seq skips one after index 1: 0,1,3,4,5
    assert [r[1] for r in rows] == ["0", "1", "3", "4", "5"]
    assert stats.n_seq_gaps == 1


def test_actuator_fb_no_session_clocks_off_fb_seq(tmp_path):
    """actuator-fb needs no SESSION — it derives timestamps from fb_seq/240."""
    adc = (1234, 2345, 3456, 1500, 2500)
    payloads = []  # deliberately no SESSION record
    for i, m in enumerate([0x00, 0x01, 0x01 | _STRUM_DOWN]):
        payloads.append(build_fretboard_raw_payload(
            frame_epoch=1, adc=adc, fb_sample_seq=10 + i, applied_mask=m))
    cap = _write_capture(tmp_path, payloads)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator-fb")
    _, rows = _read_csv(out)
    assert len(rows) == 3
    # timestamps are written to 6 decimals, so compare with abs tolerance
    assert float(rows[0][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(rows[1][0]) == pytest.approx(1 / 240.0, abs=1e-6)
    assert float(rows[2][0]) == pytest.approx(2 / 240.0, abs=1e-6)
    assert [r[1] for r in rows] == ["10", "11", "12"]  # fb_seq preserved
    assert stats.n_rows == 3


def test_detector_fb_frets_from_detector_strum_from_frame(tmp_path):
    """detector-fb: fret bits come from the forward-filled Detector
    pressed_mask (clean per-note), strum from the in-frame applied_mask,
    fb_seq column present, no SESSION needed."""
    adc = (1234, 2345, 3456, 1500, 2500)
    payloads = [
        # no SESSION; detector says green+yellow pressed
        build_detector_payload(frame_epoch=1, pressed_mask=0b00101, edge_active_mask=0),
        # frame carries a strum-down in applied_mask but only green fret bit
        build_fretboard_raw_payload(
            frame_epoch=1, adc=adc, fb_sample_seq=0,
            applied_mask=0x01 | _STRUM_DOWN),
        build_fretboard_raw_payload(
            frame_epoch=1, adc=adc, fb_sample_seq=1, applied_mask=0x00),
    ]
    cap = _write_capture(tmp_path, payloads)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="detector-fb")
    header, rows = _read_csv(out)
    assert header[1] == "fb_seq"
    assert len(rows) == 2
    # frets follow the DETECTOR mask (green+yellow), NOT the frame's applied_mask
    assert rows[0][7:12] == ["1", "0", "1", "0", "0"]
    assert rows[1][7:12] == ["1", "0", "1", "0", "0"]
    # strum follows the in-frame applied_mask (down on row 0, none on row 1)
    assert rows[0][12] == "1"
    assert rows[1][12] == "0"
    assert stats.n_detector_records == 1
    assert stats.n_strum_events == 1


def test_actuator_fb_no_actuator_records_needed(tmp_path):
    """actuator-fb sources labels from the frame, so it works with zero
    ACTUATOR records (unlike the cross-stream actuator mode)."""
    cap = _make_actuator_fb_capture(tmp_path)
    out = tmp_path / "out.csv"
    stats = export_sensiml_csv(cap, out, labels="actuator-fb")
    assert stats.n_actuator_records == 0
    assert stats.n_rows == 5
