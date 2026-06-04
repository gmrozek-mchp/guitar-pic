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


def _runs_of_ones(seq: list[int]) -> list[tuple[int, int]]:
    """Maximal [start, end) runs where seq == 1."""
    runs = []
    i, n = 0, len(seq)
    while i < n:
        if seq[i]:
            j = i
            while j < n and seq[j]:
                j += 1
            runs.append((i, j))
            i = j
        else:
            i += 1
    return runs


@dataclass(frozen=True)
class PulseStats:
    """Shape of a predicted strum stream — what matters for in-game registration.

    The rising edge is what the game catches; duration matters only insofar as
    the pulse is long enough to register and free of mid-pulse dropouts. So we
    report pulse count, how many are too short to register, and how many brief
    0-gaps sit between pulses (glitches that would register as a double-strum).
    """

    n_pulses: int
    n_glitches: int
    n_too_short: int
    durations_ticks: list[int]

    def median_ms(self) -> float:
        if not self.durations_ticks:
            return 0.0
        d = sorted(self.durations_ticks)
        return d[len(d) // 2] / SAMPLE_RATE_HZ * 1000.0

    def summary(self) -> str:
        return (
            f"pulses={self.n_pulses} median={self.median_ms():.1f}ms "
            f"glitches={self.n_glitches} too_short={self.n_too_short}"
        )


def strum_pulse_stats(
    seq: list[int],
    *,
    min_ticks: int = 6,
    glitch_max_ticks: int = 2,
) -> PulseStats:
    """Describe the pulse shape of a binary strum stream.

    `min_ticks`: a pulse shorter than this may not register in-game (marvin
    asserts ~6 ticks / 25 ms). `glitch_max_ticks`: a 0-gap this short between
    two pulses is treated as a mid-strum dropout (one strum split in two).
    """
    runs = _runs_of_ones(seq)
    durations = [e - s for s, e in runs]
    n_too_short = sum(1 for d in durations if d < min_ticks)
    n_glitches = sum(
        1 for k in range(1, len(runs))
        if (runs[k][0] - runs[k - 1][1]) <= glitch_max_ticks
    )
    return PulseStats(
        n_pulses=len(runs),
        n_glitches=n_glitches,
        n_too_short=n_too_short,
        durations_ticks=durations,
    )


def monostable(seq: list[int], *, hold: int, refractory: int) -> list[int]:
    """Deploy-time strum post-processor: rising-edge-triggered one-shot.

    Triggers only on a 0→1 transition (one strum = one rising edge; you can't
    strum twice without releasing). On a trigger, force the output high for
    `hold` ticks, then block new triggers for `refractory` more ticks. So a
    single sustained input assertion yields exactly one pulse (no retriggering
    inside it), mid-pulse glitches are absorbed, and two real strums must be
    ≥ hold+refractory apart. Cheap enough for the MCU runtime (a small counter).
    """
    out = [0] * len(seq)
    hold_until = 0     # output forced high while i < hold_until
    block_until = 0    # no new trigger while i < block_until
    prev = 0
    for i, v in enumerate(seq):
        if v == 1 and prev == 0 and i >= block_until:
            hold_until = i + hold
            block_until = i + hold + refractory
        if i < hold_until:
            out[i] = 1
        prev = v
    return out
