"""
Slope-aware detector -- EMA smoothing + relative-movement state machine.

Smooths the raw ADC with a lightweight integer EMA, then uses a three-state
FSM (IDLE / PRESSED / RISING) to detect both initial presses and rapid
re-presses that never reach the absolute release threshold.

Re-press detection works by tracking the trough (running minimum) while
pressed.  When the signal rises by *repress_delta* above the trough, the
RISING state arms.  If it then falls by *repress_fall* from the local peak
seen during the rise, that inflection confirms a new press.

Sustain handling: the fret waveform for a sustained note dips on initial
press, rises to a steady-state plateau, holds there, then dips *deeper*
when the sustain track lights up (~300ms later).  Without a guard this
second dip would false-trigger a re-press.  A time-based holdoff
(SUSTAIN_HOLDOFF_MS) distinguishes the two: if the signal has been in the
RISING / plateau state longer than the holdoff, subsequent falls are
treated as sustain deepening (trough updated, no new press event).

All arithmetic is integer-only and O(1) per sample per channel, suitable
for eventual porting to ARM Cortex-M0+ at 24 MHz.
"""

from enum import Enum, auto

from stream import Sample, CHANNELS

# EMA is computed in fixed-point with this many fractional bits.
# smoothed is stored scaled by (1 << _FP_BITS) to avoid truncation drift.
_FP_BITS = 8


class _State(Enum):
    IDLE = auto()
    PRESSED = auto()
    RISING = auto()


class _Track:
    __slots__ = (
        "state", "smoothed_fp", "trough", "peak",
        "press_count", "rising_since", "last_press_at",
    )

    def __init__(self) -> None:
        self.state = _State.IDLE
        self.smoothed_fp: int = 0  # fixed-point (raw << _FP_BITS)
        self.trough: int = 0
        self.peak: int = 0
        self.press_count: int = 0
        self.rising_since: float = 0.0  # timestamp when RISING was entered
        self.last_press_at: float = -1.0  # timestamp of last press_count increment


_DEFAULT_THRESHOLDS = {
    "green":  {"press": 2500, "release": 2800, "repress_delta": 250, "repress_fall": 150},
    "red":    {"press": 2500, "release": 2800, "repress_delta": 250, "repress_fall": 150},
    "yellow": {"press": 1500, "release": 1700, "repress_delta": 180, "repress_fall": 120},
    "blue":   {"press": 1500, "release": 1700, "repress_delta": 180, "repress_fall": 120},
    "orange": {"press": 2000, "release": 2200, "repress_delta": 200, "repress_fall": 130},
}


class Detector:
    """EMA + slope state-machine detector with re-press support."""

    @classmethod
    def default_params(cls) -> dict:
        params: dict = {
            "EMA_ALPHA": 25,
            "SUSTAIN_HOLDOFF_MS": 100,
            "REPRESS_COOLDOWN_MS": 40,
        }
        for ch, th in _DEFAULT_THRESHOLDS.items():
            prefix = ch.upper()
            params[f"{prefix}_PRESS"] = th["press"]
            params[f"{prefix}_RELEASE"] = th["release"]
            params[f"{prefix}_REPRESS_DELTA"] = th["repress_delta"]
            params[f"{prefix}_REPRESS_FALL"] = th["repress_fall"]
        return params

    def __init__(self, **params):
        defaults = self.default_params()
        defaults.update(params)

        alpha_pct = max(1, min(100, int(defaults["EMA_ALPHA"])))
        self._alpha_fp: int = (alpha_pct * (1 << _FP_BITS)) // 100
        self._sustain_holdoff_s: float = int(defaults["SUSTAIN_HOLDOFF_MS"]) / 1000.0
        self._repress_cooldown_s: float = int(defaults["REPRESS_COOLDOWN_MS"]) / 1000.0

        self._thresholds: dict[str, dict[str, int]] = {}
        for ch in CHANNELS:
            p = ch.upper()
            self._thresholds[ch] = {
                "press":         int(defaults[f"{p}_PRESS"]),
                "release":       int(defaults[f"{p}_RELEASE"]),
                "repress_delta": int(defaults[f"{p}_REPRESS_DELTA"]),
                "repress_fall":  int(defaults[f"{p}_REPRESS_FALL"]),
            }

        self._tracks: dict[str, _Track] = {ch: _Track() for ch in CHANNELS}
        self._first = True

    def update(self, sample: Sample) -> dict:
        result: dict[str, dict] = {}

        for ch in CHANNELS:
            raw: int = getattr(sample, ch)
            t = self._tracks[ch]
            th = self._thresholds[ch]

            if self._first:
                t.smoothed_fp = raw << _FP_BITS

            # EMA update (integer fixed-point):
            #   smoothed_fp += alpha_fp * (raw_fp - smoothed_fp) >> FP_BITS
            raw_fp = raw << _FP_BITS
            t.smoothed_fp += (self._alpha_fp * (raw_fp - t.smoothed_fp)) >> _FP_BITS
            smoothed = t.smoothed_fp >> _FP_BITS

            cooled = (sample.timestamp - t.last_press_at) >= self._repress_cooldown_s

            if t.state is _State.IDLE:
                if smoothed < th["press"]:
                    t.state = _State.PRESSED
                    t.trough = smoothed
                    t.press_count += 1
                    t.last_press_at = sample.timestamp

            elif t.state is _State.PRESSED:
                if smoothed < t.trough:
                    t.trough = smoothed

                if smoothed > th["release"]:
                    t.state = _State.IDLE
                elif cooled and smoothed > t.trough + th["repress_delta"]:
                    t.state = _State.RISING
                    t.peak = smoothed
                    t.rising_since = sample.timestamp

            elif t.state is _State.RISING:
                if smoothed > t.peak:
                    t.peak = smoothed

                if smoothed > th["release"]:
                    t.state = _State.IDLE
                elif smoothed < t.peak - th["repress_fall"]:
                    rising_dur = sample.timestamp - t.rising_since
                    t.state = _State.PRESSED
                    t.trough = smoothed
                    if rising_dur < self._sustain_holdoff_s:
                        t.press_count += 1
                        t.last_press_at = sample.timestamp

            result[ch] = {
                "pressed": t.state is not _State.IDLE,
                "press_count": t.press_count,
                "baseline": smoothed,
                "trough": t.trough,
                "peak": t.peak,
                "state": t.state.name,
            }

        self._first = False
        return result
