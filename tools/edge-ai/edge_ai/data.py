"""Load actuator-fb SensiML CSVs and build causal training windows.

The CSV is produced by `marvin-perf export-ml --labels=actuator-fb`:

    timestamp, fb_seq,
    ph_green,ph_red,ph_yellow,ph_blue,ph_orange,
    fret_green,fret_red,fret_yellow,fret_blue,fret_orange,
    strum

Each row is one true 240 Hz fretboard sample with its label (the actuator
bitmask the fretboard was driving during the scan) paired atomically at the
source — no cross-stream join, so timing is clean. `fb_seq` is the fretboard's
monotonic sample counter; gaps mean frames dropped in transit. Windows are
sliced **by row index** and never span a `fb_seq` gap (see `contiguous_runs`),
so a dropped frame can't silently stitch two non-adjacent samples together.

CSV parsing, the window-index logic, and label/feature extraction are
stdlib-only. `build_arrays` materialises numpy tensors and imports numpy
lazily so the rest of the module loads without it.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

from . import FRET_COUNT, N_LABELS

EXPECTED_HEADER = [
    "timestamp", "fb_seq",
    "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
    "fret_green", "fret_red", "fret_yellow", "fret_blue", "fret_orange",
    "strum",
]

_ADC_COL0 = 2   # first ph_ column index
_LABEL_COL0 = 7  # first fret_ column index


class DataError(Exception):
    """Raised on a malformed or wrong-schema CSV."""


@dataclass(frozen=True)
class Capture:
    """One loaded capture. `adc[i]` is a 5-tuple, `labels[i]` a 6-tuple,
    `fb_seq[i]` the fretboard sample counter for row i."""

    name: str
    timestamps: list[float]
    fb_seq: list[int]
    adc: list[tuple[int, ...]]
    labels: list[tuple[int, ...]]

    def __len__(self) -> int:
        return len(self.adc)

    @property
    def n_seq_gaps(self) -> int:
        return sum(
            1 for i in range(1, len(self.fb_seq))
            if self.fb_seq[i] != self.fb_seq[i - 1] + 1
        )

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
    """Read one actuator-fb CSV into a Capture (stdlib only)."""
    path = Path(path)
    with path.open(newline="") as fh:
        reader = csv.reader(fh)
        try:
            header = next(reader)
        except StopIteration:
            raise DataError(f"{path}: empty file")
        if header != EXPECTED_HEADER:
            raise DataError(
                f"{path}: header is not the actuator-fb schema.\n"
                f"  expected: {EXPECTED_HEADER}\n"
                f"  got:      {header}\n"
                "Export with `marvin-perf export-ml --labels=actuator-fb`."
            )
        timestamps: list[float] = []
        fb_seq: list[int] = []
        adc: list[tuple[int, ...]] = []
        labels: list[tuple[int, ...]] = []
        for lineno, row in enumerate(reader, start=2):
            if len(row) != len(EXPECTED_HEADER):
                raise DataError(
                    f"{path}:{lineno}: expected {len(EXPECTED_HEADER)} fields, "
                    f"got {len(row)}"
                )
            timestamps.append(float(row[0]))
            fb_seq.append(int(row[1]))
            adc.append(tuple(int(row[_ADC_COL0 + c]) for c in range(FRET_COUNT)))
            labels.append(tuple(int(row[_LABEL_COL0 + b]) for b in range(N_LABELS)))
    return Capture(
        name=path.stem, timestamps=timestamps, fb_seq=fb_seq, adc=adc, labels=labels
    )


def contiguous_runs(fb_seq: list[int]) -> list[tuple[int, int]]:
    """Index ranges `[start, end)` over which `fb_seq` increments by exactly 1.

    A break (dropped frame → seq jump) starts a new run, so windowing within a
    run never stitches two non-adjacent samples across a gap.
    """
    if not fb_seq:
        return []
    runs = []
    start = 0
    for i in range(1, len(fb_seq)):
        if fb_seq[i] != fb_seq[i - 1] + 1:
            runs.append((start, i))
            start = i
    runs.append((start, len(fb_seq)))
    return runs


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


def compute_norm_stats(captures: list[Capture]):
    """Per-channel (mean, std) over all rows, for input standardisation.

    Standardising the narrow ADC band (idle ≈ 3900, dips ≈ 1000) is what makes
    the net trainable; raw /4095 leaves inputs clustered near 0.95 and starves
    gradients. The affine folds into the int8 input quantisation at deploy, so
    this stays deploy-compatible. Returns numpy arrays (mean[5], std[5]).
    """
    import numpy as np

    rows = np.concatenate(
        [np.asarray(c.adc, dtype=np.float32) for c in captures if len(c) > 0], axis=0
    )
    mean = rows.mean(axis=0)
    std = rows.std(axis=0)
    std[std < 1e-6] = 1.0
    return mean, std


def _dilate_strum(strum, k: int):
    """Widen each strum positive by ±k samples (training labels only)."""
    import numpy as np

    out = strum.copy()
    for off in range(1, k + 1):
        out[off:] = np.maximum(out[off:], strum[:-off])
        out[:-off] = np.maximum(out[:-off], strum[off:])
    return out


def build_arrays(
    captures: list[Capture],
    window: int,
    *,
    stats=None,
    strum_dilate: int = 0,
    label_lead: int = 0,
):
    """Materialise (X, Y, stats) for all captures, windowed per capture.

    X: float32 (n_windows, FRET_COUNT, window) — channels-first for Conv1d,
       per-channel standardised by `stats` (computed from `captures` if None).
    Y: float32 (n_windows, N_LABELS) — the 6 label bits at each window's
       label_row. With `strum_dilate>0` the strum label (bit 5) is widened by
       ±k samples so a near-miss isn't fully penalised — use for *training*
       only, never for eval.

    Windows never cross capture boundaries, and never span a `fb_seq` gap
    within a capture (each contiguous run is windowed independently).
    """
    import numpy as np

    if stats is None:
        stats = compute_norm_stats(captures)
    mean, std = stats

    xs = []
    ys = []
    for cap in captures:
        adc = (np.asarray(cap.adc, dtype=np.float32) - mean) / std  # (rows, 5)
        lab = np.asarray(cap.labels, dtype=np.float32)              # (rows, 6)
        if strum_dilate > 0:
            lab = lab.copy()
            lab[:, 5] = _dilate_strum(lab[:, 5], strum_dilate)
        for run_start, run_end in contiguous_runs(cap.fb_seq):
            run_len = run_end - run_start
            if run_len < window:
                continue
            for start, end, label_row in causal_window_indices(run_len, window):
                lr = label_row + label_lead   # >0 = predict the command N samples ahead
                if lr >= run_len:
                    continue                  # no future label that far ahead in this run
                xs.append(adc[run_start + start:run_start + end].T)   # (5, window)
                ys.append(lab[run_start + lr])                        # (6,)
    if not xs:
        raise DataError(
            f"no windows produced — every capture shorter than window={window}?"
        )
    return np.stack(xs).astype(np.float32), np.stack(ys).astype(np.float32), stats


def strum_pos_weight(captures: list[Capture]) -> float:
    """Inverse-frequency positive weight for the sparse strum bit: (neg/pos)."""
    pos = sum(row[5] for cap in captures for row in cap.labels)
    total = sum(len(cap) for cap in captures)
    neg = total - pos
    return (neg / pos) if pos else 1.0
