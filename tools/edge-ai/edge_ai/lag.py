"""Measure the photo-dip → strum lag from real data (stdlib only).

With difficulty fixed, the delay from a note dipping a phototransistor to
marvin issuing the strum is a constant. We recover it by cross-correlating a
"photo darkness" signal against the strum rising-edge impulses: for each
candidate lag L, score how strongly a dip L samples *before* a strum predicts
that strum. The peak L is the dominant lag; it sizes the causal window.

This is a diagnostic, not training — kept dependency-free so it runs anywhere.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import SAMPLE_RATE_HZ
from .data import Capture


@dataclass(frozen=True)
class LagResult:
    best_lag_samples: int
    best_lag_ms: float
    centroid_ms: float
    n_strum_events: int
    curve: list[tuple[int, float]]  # (lag_samples, normalised_score)

    def summary(self) -> str:
        return (
            f"peak lag {self.best_lag_samples} samples "
            f"({self.best_lag_ms:.1f} ms), centroid {self.centroid_ms:.1f} ms, "
            f"over {self.n_strum_events} strum events"
        )


def _darkness(cap: Capture) -> list[float]:
    """Per-row aggregate dip magnitude: sum over channels of (bright - adc).

    `bright` is a per-channel high baseline (max over the capture, i.e. the
    idle/unlit level since lower ADC = brighter sensor = note present). Clipped
    at 0 so only dips contribute. Zero-mean so the cross-correlation isn't
    dominated by a DC term.
    """
    n = len(cap)
    if n == 0:
        return []
    n_ch = len(cap.adc[0])
    bright = [max(cap.adc[i][c] for i in range(n)) for c in range(n_ch)]
    d = [0.0] * n
    for i in range(n):
        acc = 0.0
        row = cap.adc[i]
        for c in range(n_ch):
            v = bright[c] - row[c]
            if v > 0:
                acc += v
        d[i] = acc
    mean = sum(d) / n
    return [v - mean for v in d]


def _strum_rising_edges(cap: Capture) -> list[int]:
    edges = []
    prev = 0
    for i, row in enumerate(cap.labels):
        s = row[5]
        if s and not prev:
            edges.append(i)
        prev = s
    return edges


def measure_lag(cap: Capture, *, max_lag_ms: float = 600.0) -> LagResult:
    """Cross-correlate dip signal vs strum rising edges over lags [0, max]."""
    d = _darkness(cap)
    edges = _strum_rising_edges(cap)
    max_lag = int(round(max_lag_ms / 1000.0 * SAMPLE_RATE_HZ))
    if not edges or max_lag < 1:
        return LagResult(0, 0.0, 0.0, len(edges), [])

    curve: list[tuple[int, float]] = []
    for L in range(0, max_lag + 1):
        acc = 0.0
        cnt = 0
        for e in edges:
            j = e - L
            if j >= 0:
                acc += d[j]
                cnt += 1
        curve.append((L, acc / cnt if cnt else 0.0))

    scores = [s for _, s in curve]
    lo = min(scores)
    shifted = [s - lo for s in scores]  # >= 0 for centroid weighting
    best_idx = max(range(len(curve)), key=lambda k: curve[k][1])
    best_lag = curve[best_idx][0]

    wsum = sum(shifted)
    centroid_samples = (
        sum(L * w for (L, _), w in zip(curve, shifted)) / wsum if wsum else 0.0
    )

    return LagResult(
        best_lag_samples=best_lag,
        best_lag_ms=best_lag / SAMPLE_RATE_HZ * 1000.0,
        centroid_ms=centroid_samples / SAMPLE_RATE_HZ * 1000.0,
        n_strum_events=len(edges),
        curve=curve,
    )
