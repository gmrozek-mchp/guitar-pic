"""Stdlib-only tests for CSV loading, causal windowing, and run-splitting."""

from __future__ import annotations

from pathlib import Path

import pytest

from edge_ai.data import (
    DataError,
    causal_window_indices,
    contiguous_runs,
    load_capture,
    strum_pos_weight,
)

_HDR = (
    "timestamp,fb_seq,ph_green,ph_red,ph_yellow,ph_blue,ph_orange,"
    "fret_green,fret_red,fret_yellow,fret_blue,fret_orange,strum"
)


def _write(tmp_path: Path, rows: list[str]) -> Path:
    p = tmp_path / "cap.csv"
    p.write_text("\n".join([_HDR] + rows) + "\n")
    return p


def test_causal_window_indices_shape():
    idx = causal_window_indices(n_rows=5, window=3)
    assert idx == [(0, 3, 2), (1, 4, 3), (2, 5, 4)]


def test_causal_window_indices_window_one_is_identity():
    assert causal_window_indices(4, 1) == [(0, 1, 0), (1, 2, 1), (2, 3, 2), (3, 4, 3)]


def test_causal_window_indices_too_short():
    assert causal_window_indices(n_rows=2, window=3) == []


def test_contiguous_runs_no_gaps():
    assert contiguous_runs([0, 1, 2, 3, 4]) == [(0, 5)]


def test_contiguous_runs_with_gap():
    # seq jumps 2->5 (dropped 3,4) and 6->10
    assert contiguous_runs([0, 1, 2, 5, 6, 10]) == [(0, 3), (3, 5), (5, 6)]


def test_contiguous_runs_empty():
    assert contiguous_runs([]) == []


def test_windowing_never_spans_a_gap():
    # Compose runs + windows the way build_arrays does; assert every window's
    # feature span stays inside a single run (pure index math, no numpy).
    fb_seq = [0, 1, 2, 5, 6, 7, 8]  # gap between index 2 (seq 2) and 3 (seq 5)
    window = 3
    spans = []
    for run_start, run_end in contiguous_runs(fb_seq):
        run_len = run_end - run_start
        if run_len < window:
            continue
        for start, end, label_row in causal_window_indices(run_len, window):
            spans.append((run_start + start, run_start + end))
    # Run 1 (indices 0..2) yields (0,3); run 2 (indices 3..6) yields (3,6),(4,7).
    # No span crosses the gap at index 3.
    assert spans == [(0, 3), (3, 6), (4, 7)]
    assert all(not (s < 3 <= e - 1) for s, e in spans)  # none straddle the gap


def test_load_capture_parses_columns(tmp_path):
    p = _write(tmp_path, [
        "0.0,0,3900,3900,3900,3900,3900,0,0,0,0,0,0",
        "0.004,1,1200,3900,3900,3900,3900,1,0,0,0,0,0",
        "0.008,2,1200,3900,1500,3900,3900,1,0,1,0,0,1",
    ])
    cap = load_capture(p)
    assert len(cap) == 3
    assert cap.fb_seq == [0, 1, 2]
    assert cap.adc[1] == (1200, 3900, 3900, 3900, 3900)
    assert cap.labels[2] == (1, 0, 1, 0, 0, 1)


def test_n_seq_gaps(tmp_path):
    p = _write(tmp_path, [
        "0,0,1,1,1,1,1,0,0,0,0,0,0",
        "0,1,1,1,1,1,1,0,0,0,0,0,0",
        "0,3,1,1,1,1,1,0,0,0,0,0,0",  # seq jumps 1->3
    ])
    cap = load_capture(p)
    assert cap.n_seq_gaps == 1


def test_strum_pos_weight(tmp_path):
    p = _write(tmp_path, [
        "0,0,1,1,1,1,1,0,0,0,0,0,0",
        "0,1,1,1,1,1,1,0,0,0,0,0,0",
        "0,2,1,1,1,1,1,0,0,0,0,0,0",
        "0,3,1,1,1,1,1,0,0,0,0,0,1",
    ])
    cap = load_capture(p)
    assert strum_pos_weight([cap]) == pytest.approx(3.0)  # 1 pos, 3 neg


def test_wrong_header_rejected(tmp_path):
    p = tmp_path / "bad.csv"
    p.write_text("timestamp,ph_green,label_green\n0,1,0\n")
    with pytest.raises(DataError):
        load_capture(p)


def test_old_actuator_schema_rejected(tmp_path):
    # the pre-fb_seq actuator schema must be rejected (no fb_seq column)
    p = tmp_path / "old.csv"
    p.write_text(
        "timestamp,ph_green,ph_red,ph_yellow,ph_blue,ph_orange,"
        "fret_green,fret_red,fret_yellow,fret_blue,fret_orange,strum\n"
    )
    with pytest.raises(DataError):
        load_capture(p)
