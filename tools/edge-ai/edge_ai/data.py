"""Load actuator-labelled SensiML CSVs and build causal training windows.

The CSV is produced by `marvin-perf export-ml --labels=actuator`:

    timestamp,
    ph_green,ph_red,ph_yellow,ph_blue,ph_orange,
    fret_green,fret_red,fret_yellow,fret_blue,fret_orange,
    strum

Each row is one true 240 Hz fretboard sample. Windows are sliced **by row
index**, never by the `timestamp` column — timestamps are bursty (stamped at
marvin's USB-CDC RX time, not the sample instant; see the edge-ai journal).

CSV parsing, the window-index logic, and label/feature extraction are
stdlib-only. `build_arrays` materialises numpy tensors and imports numpy
lazily so the rest of the module loads without it.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

from . import ADC_MAX, FRET_COUNT, N_LABELS

EXPECTED_HEADER = [
    "timestamp",
    "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
    "fret_green", "fret_red", "fret_yellow", "fret_blue", "fret_orange",
    "strum",
]


class DataError(Exception):
    """Raised on a malformed or wrong-schema CSV."""


@dataclass(frozen=True)
class Capture:
    """One loaded capture. `adc[i]` is a 5-tuple, `labels[i]` a 6-tuple."""

    name: str
    timestamps: list[float]
    adc: list[tuple[int, ...]]
    labels: list[tuple[int, ...]]

    def __len__(self) -> int:
        return len(self.adc)

    @property
    def strum_fraction(self) -> float:
        if not self.labels:
            return 0.0
        return sum(row[5] for row in self.labels) / len(self.labels)

    @property
    def n_strum_events(self) -> int:
        prev = 0
        n = 0
        for row in self.labels:
            if row[5] and not prev:
                n += 1
            prev = row[5]
        return n


def load_capture(path: str | Path) -> Capture:
    """Read one actuator-labelled CSV into a Capture (stdlib only)."""
    path = Path(path)
    with path.open(newline="") as fh:
        reader = csv.reader(fh)
        try:
            header = next(reader)
        except StopIteration:
            raise DataError(f"{path}: empty file")
        if header != EXPECTED_HEADER:
            raise DataError(
                f"{path}: header is not the actuator schema.\n"
                f"  expected: {EXPECTED_HEADER}\n"
                f"  got:      {header}\n"
                "Export with `marvin-perf export-ml --labels=actuator`."
            )
        timestamps: list[float] = []
        adc: list[tuple[int, ...]] = []
        labels: list[tuple[int, ...]] = []
        for lineno, row in enumerate(reader, start=2):
            if len(row) != len(EXPECTED_HEADER):
                raise DataError(f"{path}:{lineno}: expected 12 fields, got {len(row)}")
            timestamps.append(float(row[0]))
            adc.append(tuple(int(row[1 + c]) for c in range(FRET_COUNT)))
            labels.append(tuple(int(row[6 + b]) for b in range(N_LABELS)))
    return Capture(name=path.stem, timestamps=timestamps, adc=adc, labels=labels)


def causal_window_indices(n_rows: int, window: int) -> list[tuple[int, int, int]]:
    """Causal "now-cast" windows over a single capture's rows.

    Returns `(start, end, label_row)` triples where rows `[start, end)` are the
    `window` feature samples (end is exclusive) and the label is taken at
    `label_row = end - 1` — i.e. the window ends *at* the row being predicted,
    so only past+present samples feed the prediction. Rows before the first
    full window have insufficient history and are skipped.
    """
    if window < 1:
        raise ValueError("window must be >= 1")
    return [(i - window + 1, i + 1, i) for i in range(window - 1, n_rows)]


def build_arrays(captures: list[Capture], window: int, *, adc_scale: float = float(ADC_MAX)):
    """Materialise (X, Y) for all captures, windowed independently per capture.

    X: float32 (n_windows, FRET_COUNT, window) — channels-first for Conv1d,
       scaled to [0, 1] by `adc_scale` (fixed affine, deploy-friendly).
    Y: float32 (n_windows, N_LABELS) — the 6 label bits at each window's
       label_row.

    Windows never cross capture boundaries (no song bleeds into another).
    """
    import numpy as np

    xs = []
    ys = []
    for cap in captures:
        if len(cap) < window:
            continue
        adc = np.asarray(cap.adc, dtype=np.float32) / adc_scale  # (rows, 5)
        lab = np.asarray(cap.labels, dtype=np.float32)           # (rows, 6)
        idx = causal_window_indices(len(cap), window)
        for start, end, label_row in idx:
            xs.append(adc[start:end].T)        # (5, window)
            ys.append(lab[label_row])          # (6,)
    if not xs:
        raise DataError(
            f"no windows produced — every capture shorter than window={window}?"
        )
    return np.stack(xs).astype(np.float32), np.stack(ys).astype(np.float32)


def strum_pos_weight(captures: list[Capture]) -> float:
    """Inverse-frequency positive weight for the sparse strum bit: (neg/pos)."""
    pos = sum(row[5] for cap in captures for row in cap.labels)
    total = sum(len(cap) for cap in captures)
    neg = total - pos
    return (neg / pos) if pos else 1.0
