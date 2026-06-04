"""Stdlib-only tests for the lag-measurement utility and metrics."""

from __future__ import annotations

from edge_ai.data import Capture
from edge_ai.lag import measure_lag
from edge_ai.metrics import monostable, strum_event_timing, strum_pulse_stats


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
        fb_seq=list(range(n)),
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
        fb_seq=[0, 1],
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


def test_monostable_fixed_hold_and_refractory():
    # a 1-tick blip becomes a `hold`-long pulse
    seq = [0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
    out = monostable(seq, hold=4, refractory=2)
    assert out == [0, 1, 1, 1, 1, 0, 0, 0, 0, 0]


def test_monostable_absorbs_glitch():
    # a 1-0-1 glitch within the hold window collapses to one clean pulse
    seq = [1, 0, 1, 1, 0, 0, 0, 0]
    out = monostable(seq, hold=5, refractory=2)
    assert out == [1, 1, 1, 1, 1, 0, 0, 0]  # single 5-tick pulse, glitch gone


def test_monostable_one_pulse_per_rising_edge():
    # a single sustained assertion is ONE strum -> one pulse, no retriggering
    seq = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    out = monostable(seq, hold=3, refractory=3)
    assert out == [1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]


def test_monostable_retrigger_needs_release_then_edge():
    # second strum requires a release (0) and a fresh rising edge past refractory
    seq = [1, 0, 0, 0, 0, 1, 0, 0]   # rising edges at 0 and 5
    out = monostable(seq, hold=2, refractory=2)
    # trigger@0: hold 0,1; block until 4. rising@5 (>=4): hold 5,6
    assert out == [1, 1, 0, 0, 0, 1, 1, 0]


def test_strum_pulse_stats_glitch_and_short():
    # two pulses: a 1-tick one (too short) then a glitchy pair (gap of 1)
    seq = [1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1]
    ps = strum_pulse_stats(seq, min_ticks=3, glitch_max_ticks=2)
    assert ps.n_pulses == 3
    assert ps.n_too_short == 1          # the leading 1-tick pulse
    assert ps.n_glitches == 1           # the 1-tick gap between the last two pulses
