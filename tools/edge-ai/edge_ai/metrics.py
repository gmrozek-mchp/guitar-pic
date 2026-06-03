"""Evaluation metrics (stdlib only).

Two families:
- Per-bit accuracy / F1 over the 6 output bits (5 frets + strum).
- Strum-event timing: match predicted strum rising edges to true ones within a
  tolerance and report the timing-error distribution + precision/recall. This
  is the Phase-2 gate metric (p95 ≤ 20 ms ≈ 5 samples).
"""

from __future__ import annotations

from dataclasses import dataclass

from . import N_LABELS, SAMPLE_RATE_HZ

LABEL_NAMES = ["green", "red", "yellow", "blue", "orange", "strum"]


def per_bit_accuracy(pred: list[list[int]], true: list[list[int]]) -> list[float]:
    n = len(true)
    if n == 0:
        return [0.0] * N_LABELS
    acc = []
    for b in range(N_LABELS):
        correct = sum(1 for i in range(n) if pred[i][b] == true[i][b])
        acc.append(correct / n)
    return acc


def per_bit_f1(pred: list[list[int]], true: list[list[int]]) -> list[float]:
    out = []
    for b in range(N_LABELS):
        tp = sum(1 for i in range(len(true)) if pred[i][b] == 1 and true[i][b] == 1)
        fp = sum(1 for i in range(len(true)) if pred[i][b] == 1 and true[i][b] == 0)
        fn = sum(1 for i in range(len(true)) if pred[i][b] == 0 and true[i][b] == 1)
        denom = 2 * tp + fp + fn
        out.append((2 * tp / denom) if denom else 1.0)
    return out


def _rising_edges(seq: list[int]) -> list[int]:
    edges = []
    prev = 0
    for i, s in enumerate(seq):
        if s and not prev:
            edges.append(i)
        prev = s
    return edges


@dataclass(frozen=True)
class StrumTiming:
    n_true: int
    n_pred: int
    matched: int
    precision: float
    recall: float
    errors_ms: list[float]  # signed: pred - true, for matched events

    def percentile_abs_ms(self, p: float) -> float:
        if not self.errors_ms:
            return float("nan")
        vals = sorted(abs(e) for e in self.errors_ms)
        k = max(0, min(len(vals) - 1, int(round(p / 100.0 * (len(vals) - 1)))))
        return vals[k]

    def summary(self) -> str:
        return (
            f"strum events: true={self.n_true} pred={self.n_pred} "
            f"matched={self.matched} | precision={self.precision:.3f} "
            f"recall={self.recall:.3f} | |err| p50={self.percentile_abs_ms(50):.1f}ms "
            f"p95={self.percentile_abs_ms(95):.1f}ms"
        )


def strum_event_timing(
    pred_strum: list[int],
    true_strum: list[int],
    *,
    tol_samples: int = 5,
) -> StrumTiming:
    """Greedy nearest-match of predicted strum edges to true edges.

    Each true edge matches the nearest unused predicted edge within
    `tol_samples`. Default tol 5 ≈ 20.8 ms (one ADC sample over the Phase-2
    p95 gate). Timing error is signed (pred - true) in ms.
    """
    true_e = _rising_edges(true_strum)
    pred_e = _rising_edges(pred_strum)
    used = [False] * len(pred_e)
    errors_ms = []
    matched = 0
    for t in true_e:
        best_k = -1
        best_d = tol_samples + 1
        for k, p in enumerate(pred_e):
            if used[k]:
                continue
            d = abs(p - t)
            if d <= tol_samples and d < best_d:
                best_d = d
                best_k = k
        if best_k >= 0:
            used[best_k] = True
            matched += 1
            errors_ms.append((pred_e[best_k] - t) / SAMPLE_RATE_HZ * 1000.0)
    precision = matched / len(pred_e) if pred_e else 0.0
    recall = matched / len(true_e) if true_e else 0.0
    return StrumTiming(
        n_true=len(true_e),
        n_pred=len(pred_e),
        matched=matched,
        precision=precision,
        recall=recall,
        errors_ms=errors_ms,
    )
