"""Analysis pass over a decoded session.

Operates on a `list[Record]` (session sizes are small enough at v0 — state-only
~9 KB/s × minutes is a few MB; even adding patches we stay under tens of MB).
Three deliverables:

1. Latency attribution — per-frame stage transitions in microseconds with
   p50/p95/p99/max histograms.
2. Drop accounting — deltas across the cumulative DROP record stream.
3. Stack high-water trend — per-task HWM over session time.

Plus always-on sanity checks:
- SESSION schema_version match (hard fail)
- frame_epoch monotonicity (warning)
- Inter-VIDEO_PUBLISH delta (warning if outside 16.67 ms ± 2 ms at 60 Hz)
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from typing import Iterable

from .records import (
    Drop,
    EXPECTED_SCHEMA_VERSION,
    Session,
    Stage,
    Stamp,
    TaskHighwater,
    TaskId,
)
from .decode import Record


# ─── Sanity-check errors / warnings ──────────────────────────────────────────


class SchemaVersionMismatch(Exception):
    """Raised when SESSION.schema_version does not match host expectation."""


@dataclass
class Warning:
    kind: str
    message: str


# ─── Latency attribution ─────────────────────────────────────────────────────

# Stage transitions the firmware is wired to emit (per perf_log_records.h:39-48
# and the journal's producer-wiring plan). Only adjacent pairs in a single
# frame's pipeline make sense.
STAGE_PAIRS: tuple[tuple[Stage, Stage], ...] = (
    (Stage.ISC_IRQ, Stage.VIDEO_PUBLISH),
    (Stage.VIDEO_PUBLISH, Stage.CV_START),
    (Stage.CV_START, Stage.CV_END),
    (Stage.CV_END, Stage.TP_TICK),
    (Stage.TP_TICK, Stage.FBL_SEND),
    (Stage.FBL_SEND, Stage.CDC_WRITE_COMPLETE),
)


@dataclass
class LatencyHist:
    pair: tuple[Stage, Stage]
    samples_us: list[float] = field(default_factory=list)

    @property
    def n(self) -> int:
        return len(self.samples_us)

    def percentile(self, p: float) -> float:
        if not self.samples_us:
            return float("nan")
        ordered = sorted(self.samples_us)
        # nearest-rank percentile, clamped to last index
        rank = max(0, min(len(ordered) - 1, int(p * len(ordered))))
        return ordered[rank]

    @property
    def p50(self) -> float: return self.percentile(0.50)
    @property
    def p95(self) -> float: return self.percentile(0.95)
    @property
    def p99(self) -> float: return self.percentile(0.99)
    @property
    def max(self) -> float:
        return max(self.samples_us) if self.samples_us else float("nan")


def compute_latencies(
    records: Iterable[Record],
    timer_freq_hz: int,
) -> list[LatencyHist]:
    """Group Stamp records by frame_epoch, derive per-pair deltas in µs."""
    by_epoch: dict[int, dict[int, int]] = defaultdict(dict)
    for rec in records:
        if not isinstance(rec, Stamp):
            continue
        # Last write wins on duplicate stage in a single epoch — that's a
        # producer bug worth surfacing later, but for v0 we simply take latest.
        by_epoch[rec.hdr.frame_epoch][rec.stage_id] = rec.hdr.ts_counter

    hists = [LatencyHist(pair=p) for p in STAGE_PAIRS]
    if timer_freq_hz <= 0:
        return hists

    us_per_tick = 1_000_000.0 / float(timer_freq_hz)
    for stages in by_epoch.values():
        for hist in hists:
            a, b = hist.pair
            t_a = stages.get(int(a))
            t_b = stages.get(int(b))
            if t_a is None or t_b is None:
                continue
            delta = t_b - t_a
            if delta < 0:
                continue  # 64-bit counter is monotonic; defensive only
            hist.samples_us.append(delta * us_per_tick)
    return hists


# ─── Drop accounting ─────────────────────────────────────────────────────────


@dataclass
class DropSummary:
    # Cumulative since firmware boot — `dropped_sink` is bytes, the others
    # are records (the firmware-side counter unit mismatch lives in
    # perf_log_sink_cdc.c:223 / perf_log.c:218,278).
    final_state: int = 0
    final_strip: int = 0
    final_sink_bytes: int = 0
    # Deltas from the first DROP record observed in this capture, which is
    # what users actually want when they attach mid-session: the pre-attach
    # cumulative is noise.
    since_session_state: int = 0
    since_session_strip: int = 0
    since_session_sink_bytes: int = 0
    n_drop_records: int = 0
    max_state_delta: int = 0
    max_strip_delta: int = 0
    max_sink_bytes_delta: int = 0


def compute_drops(records: Iterable[Record]) -> DropSummary:
    """Walk DROP records (cumulative since boot) and surface session-relative deltas."""
    summary = DropSummary()
    baseline: Drop | None = None
    prev: Drop | None = None
    for rec in records:
        if not isinstance(rec, Drop):
            continue
        summary.n_drop_records += 1
        if baseline is None:
            baseline = rec
        if prev is not None:
            summary.max_state_delta = max(
                summary.max_state_delta, rec.dropped_state - prev.dropped_state
            )
            summary.max_strip_delta = max(
                summary.max_strip_delta, rec.dropped_strip - prev.dropped_strip
            )
            summary.max_sink_bytes_delta = max(
                summary.max_sink_bytes_delta, rec.dropped_sink - prev.dropped_sink
            )
        summary.final_state = rec.dropped_state
        summary.final_strip = rec.dropped_strip
        summary.final_sink_bytes = rec.dropped_sink
        prev = rec
    if baseline is not None:
        summary.since_session_state = summary.final_state - baseline.dropped_state
        summary.since_session_strip = summary.final_strip - baseline.dropped_strip
        summary.since_session_sink_bytes = (
            summary.final_sink_bytes - baseline.dropped_sink
        )
    return summary


# ─── Stack HWM trend ─────────────────────────────────────────────────────────


@dataclass
class HwmSeries:
    task_id: int
    samples: list[tuple[int, int]] = field(default_factory=list)  # (ts_counter, words)

    @property
    def task_name(self) -> str:
        try:
            return TaskId(self.task_id).name
        except ValueError:
            return f"task_{self.task_id}"

    @property
    def min_words(self) -> int:
        return min((w for _, w in self.samples), default=0)


def compute_hwm(records: Iterable[Record]) -> dict[int, HwmSeries]:
    out: dict[int, HwmSeries] = {}
    for rec in records:
        if not isinstance(rec, TaskHighwater):
            continue
        s = out.setdefault(rec.task_id, HwmSeries(task_id=rec.task_id))
        s.samples.append((rec.hdr.ts_counter, rec.words))
    return out


# ─── Always-on sanity checks ─────────────────────────────────────────────────


def find_session(records: Iterable[Record]) -> Session | None:
    for rec in records:
        if isinstance(rec, Session):
            return rec
    return None


def check_schema(session: Session | None) -> None:
    if session is None:
        # No SESSION yet — happens when the host attaches mid-stream and the
        # firmware has not seen a fresh DTR-rising edge. Not fatal; analysis
        # that needs timer_freq_hz will degrade gracefully.
        return
    if session.schema_version != EXPECTED_SCHEMA_VERSION:
        raise SchemaVersionMismatch(
            f"firmware schema_version={session.schema_version} but host "
            f"expects {EXPECTED_SCHEMA_VERSION}. Check "
            f"perf_log_records.h:16 and bump records.py to match."
        )


def check_frame_epoch_monotonic(records: Iterable[Record]) -> list[Warning]:
    """Flag non-monotonic frame_epoch (gaps allowed; backwards jumps not)."""
    warnings: list[Warning] = []
    prev = -1
    for rec in records:
        # Records without per-frame epoch (SESSION, DROP, TASK_HIGHWATER) ride
        # along at frame_epoch=0 and are harmless to skip.
        if isinstance(rec, (Session, Drop, TaskHighwater)):
            continue
        epoch = rec.hdr.frame_epoch  # type: ignore[union-attr]
        if epoch < prev:
            warnings.append(
                Warning(
                    kind="frame_epoch_backwards",
                    message=f"frame_epoch went backwards: {prev} → {epoch}",
                )
            )
        prev = max(prev, epoch)
    return warnings


def check_video_publish_cadence(
    records: Iterable[Record],
    timer_freq_hz: int,
    expected_period_us: float = 16_666.7,
    tolerance_us: float = 2_000.0,
) -> list[Warning]:
    """Inter-VIDEO_PUBLISH stamps should hover at one frame period."""
    warnings: list[Warning] = []
    if timer_freq_hz <= 0:
        return warnings
    us_per_tick = 1_000_000.0 / float(timer_freq_hz)
    prev_ts: int | None = None
    for rec in records:
        if not isinstance(rec, Stamp) or rec.stage_id != int(Stage.VIDEO_PUBLISH):
            continue
        if prev_ts is not None:
            delta_us = (rec.hdr.ts_counter - prev_ts) * us_per_tick
            if abs(delta_us - expected_period_us) > tolerance_us:
                warnings.append(
                    Warning(
                        kind="video_publish_jitter",
                        message=(
                            f"VIDEO_PUBLISH delta {delta_us:.1f} µs vs "
                            f"expected {expected_period_us:.1f} ± {tolerance_us:.0f} µs"
                        ),
                    )
                )
        prev_ts = rec.hdr.ts_counter
    return warnings
