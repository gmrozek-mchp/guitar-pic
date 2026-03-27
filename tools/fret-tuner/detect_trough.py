"""
Trough-detection algorithm -- Python port of firmware/fretboard/fret_detect.c

Tracks a per-channel baseline (the idle ADC ceiling) and detects the
bottom-peak (trough) of each press waveform.  Three-state FSM:

    IDLE -> DESCENDING -> PRESSED -> IDLE

All threshold values are in raw 12-bit ADC counts.
"""

from enum import Enum, auto
from stream import Sample, CHANNELS


class _State(Enum):
    IDLE = auto()
    DESCENDING = auto()
    PRESSED = auto()


class _Track:
    __slots__ = ("state", "baseline", "run_min", "decay_count")

    def __init__(self) -> None:
        self.state = _State.IDLE
        self.baseline: int = 0
        self.run_min: int = 0
        self.decay_count: int = 0


class Detector:
    """Baseline + trough FSM detector (mirrors firmware fret_detect)."""

    @classmethod
    def default_params(cls) -> dict:
        return {
            "MIN_DROP": 200,
            "RISE_CONFIRM": 50,
            "RELEASE_MARGIN": 300,
            "BASELINE_DECAY_RATE": 250,
        }

    def __init__(self, **params):
        defaults = self.default_params()
        defaults.update(params)
        self.min_drop: int = int(defaults["MIN_DROP"])
        self.rise_confirm: int = int(defaults["RISE_CONFIRM"])
        self.release_margin: int = int(defaults["RELEASE_MARGIN"])
        self.baseline_decay_rate: int = int(defaults["BASELINE_DECAY_RATE"])

        self._tracks: dict[str, _Track] = {ch: _Track() for ch in CHANNELS}
        self._first = True

    def update(self, sample: Sample) -> dict:
        result: dict[str, dict] = {}

        for ch in CHANNELS:
            val: int = getattr(sample, ch)
            t = self._tracks[ch]

            if self._first:
                t.baseline = val
                result[ch] = self._channel_state(t)
                continue

            if t.state is _State.IDLE:
                self._update_idle(t, val)
            elif t.state is _State.DESCENDING:
                self._update_descending(t, val)
            elif t.state is _State.PRESSED:
                self._update_pressed(t, val)

            result[ch] = self._channel_state(t)

        self._first = False
        return result

    def _update_idle(self, t: _Track, val: int) -> None:
        if val > t.baseline:
            t.baseline = val
            t.decay_count = 0
        else:
            t.decay_count += 1
            if t.decay_count >= self.baseline_decay_rate:
                t.decay_count = 0
                if t.baseline > 0:
                    t.baseline -= 1

        if t.baseline >= self.min_drop and val < t.baseline - self.min_drop:
            t.state = _State.DESCENDING
            t.run_min = val

    def _update_descending(self, t: _Track, val: int) -> None:
        if val < t.run_min:
            t.run_min = val

        if val > t.run_min + self.rise_confirm:
            t.state = _State.PRESSED
        elif (t.baseline >= self.release_margin
              and val > t.baseline - self.release_margin):
            t.state = _State.IDLE

    def _update_pressed(self, t: _Track, val: int) -> None:
        if (t.baseline >= self.release_margin
                and val > t.baseline - self.release_margin):
            t.state = _State.IDLE

    @staticmethod
    def _channel_state(t: _Track) -> dict:
        return {
            "pressed": t.state is _State.PRESSED,
            "state": t.state.name,
            "baseline": t.baseline,
            "run_min": t.run_min,
        }
