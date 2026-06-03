"""Stdlib-only tests for CSV loading and causal windowing."""

from __future__ import annotations

import textwrap
from pathlib import Path

import pytest

from edge_ai.data import (
    DataError,
    causal_window_indices,
    load_capture,
    strum_pos_weight,
)


def _write(tmp_path: Path, rows: list[str], *, header: bool = True) -> Path:
    hdr = (
        "timestamp,ph_green,ph_red,ph_yellow,ph_blue,ph_orange,"
        "fret_green,fret_red,fret_yellow,fret_blue,fret_orange,strum"
    )
    body = ("\n".join([hdr] + rows) if header else "\n".join(rows)) + "\n"
    p = tmp_path / "cap.csv"
    p.write_text(body)
    return p


def test_causal_window_indices_shape():
    idx = causal_window_indices(n_rows=5, window=3)
    # label rows 2,3,4; window ends at label row (exclusive end = row+1)
    assert idx == [(0, 3, 2), (1, 4, 3), (2, 5, 4)]


def test_causal_window_indices_window_one_is_identity():
    assert causal_window_indices(4, 1) == [(0, 1, 0), (1, 2, 1), (2, 3, 2), (3, 4, 3)]


def test_causal_window_indices_too_short():
    assert causal_window_indices(n_rows=2, window=3) == []


def test_load_capture_parses_columns(tmp_path):
    p = _write(tmp_path, [
        "0.0,3900,3900,3900,3900,3900,0,0,0,0,0,0",
        "0.004,1200,3900,3900,3900,3900,1,0,0,0,0,0",
        "0.008,1200,3900,1500,3900,3900,1,0,1,0,0,1",
    ])
    cap = load_capture(p)
    assert len(cap) == 3
    assert cap.adc[1] == (1200, 3900, 3900, 3900, 3900)
    assert cap.labels[2] == (1, 0, 1, 0, 0, 1)


def test_strum_fraction_and_events(tmp_path):
    p = _write(tmp_path, [
        "0,3900,3900,3900,3900,3900,0,0,0,0,0,0",
        "0,3900,3900,3900,3900,3900,0,0,0,0,0,1",  # rising edge 1
        "0,3900,3900,3900,3900,3900,0,0,0,0,0,1",
        "0,3900,3900,3900,3900,3900,0,0,0,0,0,0",
        "0,3900,3900,3900,3900,3900,0,0,0,0,0,1",  # rising edge 2
    ])
    cap = load_capture(p)
    assert cap.n_strum_events == 2
    assert cap.strum_fraction == pytest.approx(3 / 5)


def test_strum_pos_weight(tmp_path):
    p = _write(tmp_path, [
        "0,1,1,1,1,1,0,0,0,0,0,0",
        "0,1,1,1,1,1,0,0,0,0,0,0",
        "0,1,1,1,1,1,0,0,0,0,0,0",
        "0,1,1,1,1,1,0,0,0,0,0,1",
    ])
    cap = load_capture(p)
    # 1 positive, 3 negative -> neg/pos = 3
    assert strum_pos_weight([cap]) == pytest.approx(3.0)


def test_wrong_header_rejected(tmp_path):
    p = tmp_path / "bad.csv"
    p.write_text("timestamp,ph_green,label_green\n0,1,0\n")
    with pytest.raises(DataError):
        load_capture(p)


def test_detector_schema_rejected(tmp_path):
    # the old --labels=detector schema must be rejected by the actuator loader
    p = tmp_path / "det.csv"
    p.write_text(
        "timestamp,ph_green,ph_red,ph_yellow,ph_blue,ph_orange,"
        "label_green,label_red,label_yellow,label_blue,label_orange\n"
    )
    with pytest.raises(DataError):
        load_capture(p)
