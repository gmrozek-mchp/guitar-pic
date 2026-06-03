"""Stdlib-only tests for the lag-measurement utility and metrics."""

from __future__ import annotations

from edge_ai.data import Capture
from edge_ai.lag import measure_lag
from edge_ai.metrics import strum_event_timing


def _synthetic_capture(n: int, lag: int, *, period: int = 80, dip_width: int = 4) -> Capture:
    """Notes dip a channel every `period` samples; the strum fires `lag`
    samples after each dip. Lag-measurement should recover `lag`."""
    adc = [[3900] * 5 for _ in range(n)]
    labels = [[0] * 6 for _ in range(n)]
    dip_starts = range(period, n - lag - dip_width, period)
    for d in dip_starts:
        for k in range(dip_width):
            adc[d + k][0] = 1000  # green channel dips
        s = d + lag
        if 0 <= s < n:
            labels[s][5] = 1
            if s + 1 < n:
                labels[s + 1][5] = 1  # 2-tick strum pulse
    return Capture(
        name="syn",
        timestamps=[i / 240.0 for i in range(n)],
        adc=[tuple(r) for r in adc],
        labels=[tuple(r) for r in labels],
    )


def test_measure_lag_recovers_known_delay():
    cap = _synthetic_capture(n=4000, lag=48)  # 48 samples = 200 ms
    res = measure_lag(cap, max_lag_ms=400)
    assert res.n_strum_events > 10
    # peak should land at the injected lag (allow ±dip_width slack)
    assert abs(res.best_lag_samples - 48) <= 4


def test_measure_lag_no_strums_is_safe():
    cap = Capture(
        name="empty",
        timestamps=[0.0, 0.004],
        adc=[(3900,) * 5, (3900,) * 5],
        labels=[(0,) * 6, (0,) * 6],
    )
    res = measure_lag(cap)
    assert res.n_strum_events == 0
    assert res.best_lag_samples == 0


def test_strum_event_timing_perfect_match():
    seq = [0, 0, 1, 1, 0, 0, 0, 1, 0]
    st = strum_event_timing(seq, seq, tol_samples=5)
    assert st.n_true == 2 and st.matched == 2
    assert st.precision == 1.0 and st.recall == 1.0
    assert st.percentile_abs_ms(95) == 0.0


def test_strum_event_timing_offset_within_tol():
    true = [0, 0, 1, 0, 0, 0, 0, 0]
    pred = [0, 0, 0, 1, 0, 0, 0, 0]  # one sample late
    st = strum_event_timing(pred, true, tol_samples=5)
    assert st.matched == 1
    assert st.errors_ms[0] > 0  # pred later than true


def test_strum_event_timing_miss_outside_tol():
    true = [1, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    pred = [0, 0, 0, 0, 0, 0, 0, 0, 1, 0]  # 8 samples late, tol 5
    st = strum_event_timing(pred, true, tol_samples=5)
    assert st.matched == 0
    assert st.recall == 0.0
