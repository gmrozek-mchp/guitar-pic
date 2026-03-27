"""
Fixed-threshold hysteresis detector -- Python port of firmware fret_detect.c

Simple per-channel detection with separate press and release thresholds.
A channel is pressed when the ADC value drops below its press threshold
and released when it rises above its release threshold (hysteresis prevents
chatter).
"""

from stream import Sample, CHANNELS

# Firmware defaults from fret_detect.c
_DEFAULT_THRESHOLDS = {
    "green":  {"press": 2500, "release": 2800},
    "red":    {"press": 2500, "release": 2800},
    "yellow": {"press": 1500, "release": 1700},
    "blue":   {"press": 1500, "release": 1700},
    "orange": {"press": 2000, "release": 2200},
}


class Detector:
    """Per-channel fixed-threshold hysteresis detector (mirrors firmware)."""

    @classmethod
    def default_params(cls) -> dict:
        return {
            "GREEN_PRESS": 2500,  "GREEN_RELEASE": 2800,
            "RED_PRESS": 2500,    "RED_RELEASE": 2800,
            "YELLOW_PRESS": 1500, "YELLOW_RELEASE": 1700,
            "BLUE_PRESS": 1500,   "BLUE_RELEASE": 1700,
            "ORANGE_PRESS": 2000, "ORANGE_RELEASE": 2200,
        }

    def __init__(self, **params):
        defaults = self.default_params()
        defaults.update(params)

        self._thresholds: dict[str, dict[str, int]] = {}
        for ch in CHANNELS:
            key_p = f"{ch.upper()}_PRESS"
            key_r = f"{ch.upper()}_RELEASE"
            self._thresholds[ch] = {
                "press": int(defaults[key_p]),
                "release": int(defaults[key_r]),
            }

        self._pressed: dict[str, bool] = {ch: False for ch in CHANNELS}

    def update(self, sample: Sample) -> dict:
        result: dict[str, dict] = {}

        for ch in CHANNELS:
            val: int = getattr(sample, ch)
            thresh = self._thresholds[ch]

            if not self._pressed[ch]:
                if val < thresh["press"]:
                    self._pressed[ch] = True
            else:
                if val > thresh["release"]:
                    self._pressed[ch] = False

            result[ch] = {
                "pressed": self._pressed[ch],
                "baseline": thresh["release"],
                "press_threshold": thresh["press"],
                "release_threshold": thresh["release"],
            }

        return result
