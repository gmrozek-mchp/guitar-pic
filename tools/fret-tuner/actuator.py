"""
Actuator -- Python port of firmware/fretboard/fret_button.c

Translates detector press/release edges into a timed sequence of
fret assertions and strum pulses, then sends a 1-byte GPIO bitmask
to the microcontroller over serial.

Driven by sample timestamps (not wall clock) so behavior is identical
in live and CSV replay modes.
"""

from collections import deque
from typing import Optional

import serial

from stream import CHANNELS

# Bitmask bit positions (must match cmd_receive.h)
BIT_GREEN = 1 << 0
BIT_RED = 1 << 1
BIT_YELLOW = 1 << 2
BIT_BLUE = 1 << 3
BIT_ORANGE = 1 << 4
BIT_STRUM_DOWN = 1 << 5
BIT_STRUM_UP = 1 << 6

CHANNEL_BITS = {
    "green": BIT_GREEN,
    "red": BIT_RED,
    "yellow": BIT_YELLOW,
    "blue": BIT_BLUE,
    "orange": BIT_ORANGE,
}

FIFO_CAP = 8

DEFAULT_TIMING = {
    "STRUM_DELAY_MS": 220,
    "FRET_EARLY_MS": 50,
    "STRUM_PULSE_MS": 50,
    "CHORD_WINDOW_MS": 20,
}


class _PendingNote:
    __slots__ = ("fret_mask", "assert_at")

    def __init__(self, fret_mask: int, assert_at: float):
        self.fret_mask = fret_mask
        self.assert_at = assert_at


class _PendingStrum:
    __slots__ = ("fret_mask", "strum_at")

    def __init__(self, fret_mask: int, strum_at: float):
        self.fret_mask = fret_mask
        self.strum_at = strum_at


class Actuator:
    """Strum timing pipeline that sends GPIO bitmask bytes over serial."""

    def __init__(self, ser: Optional[serial.Serial] = None):
        self._ser = ser
        self._enabled = False

        self.strum_delay_ms: float = DEFAULT_TIMING["STRUM_DELAY_MS"]
        self.fret_early_ms: float = DEFAULT_TIMING["FRET_EARLY_MS"]
        self.strum_pulse_ms: float = DEFAULT_TIMING["STRUM_PULSE_MS"]
        self.chord_window_ms: float = DEFAULT_TIMING["CHORD_WINDOW_MS"]

        self._note_q: deque[_PendingNote] = deque(maxlen=FIFO_CAP)
        self._strum_q: deque[_PendingStrum] = deque(maxlen=FIFO_CAP)

        self._release_at: dict[str, float] = {}
        self._release_pending: dict[str, bool] = {ch: False for ch in CHANNELS}

        self._chord_open = False
        self._chord_start: float = 0.0
        self._chord_mask: int = 0

        self._frets_active: int = 0
        self._strum_active = False
        self._strum_release_at: float = 0.0
        self._strum_direction = False  # False = down, True = up

        self._prev_pressed: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._output_mask: int = 0

    @property
    def enabled(self) -> bool:
        return self._enabled

    def set_enabled(self, enabled: bool) -> None:
        self._enabled = enabled
        if not enabled:
            self._release_all()

    def set_timing(self, **params) -> None:
        for key, val in params.items():
            if key == "STRUM_DELAY_MS":
                self.strum_delay_ms = float(val)
            elif key == "FRET_EARLY_MS":
                self.fret_early_ms = float(val)
            elif key == "STRUM_PULSE_MS":
                self.strum_pulse_ms = float(val)
            elif key == "CHORD_WINDOW_MS":
                self.chord_window_ms = float(val)

    @property
    def output_mask(self) -> int:
        return self._output_mask

    def update(self, now_ms: float, detect_state: dict) -> int:
        """Process one tick. Returns the 7-bit output bitmask sent to the micro."""
        if not self._enabled:
            return 0

        presses = 0
        releases = 0
        for ch in CHANNELS:
            ch_state = detect_state.get(ch, {})
            pressed = ch_state.get("pressed", False)
            was_pressed = self._prev_pressed[ch]

            if pressed and not was_pressed:
                presses |= CHANNEL_BITS[ch]
            elif not pressed and was_pressed:
                releases |= CHANNEL_BITS[ch]

            self._prev_pressed[ch] = pressed

        if presses:
            if not self._chord_open:
                self._chord_open = True
                self._chord_start = now_ms
                self._chord_mask = 0
            self._chord_mask |= presses

        for ch in CHANNELS:
            bit = CHANNEL_BITS[ch]
            if releases & bit:
                self._release_at[ch] = now_ms + self.strum_delay_ms
                self._release_pending[ch] = True

        if self._chord_open and (now_ms - self._chord_start) >= self.chord_window_ms:
            self._chord_commit(now_ms)

        if self._strum_active and now_ms >= self._strum_release_at:
            self._strum_active = False

        self._process_notes(now_ms)
        self._process_strums(now_ms)
        self._process_releases(now_ms, detect_state)

        mask = self._frets_active
        if self._strum_active:
            if self._strum_direction:
                mask |= BIT_STRUM_UP
            else:
                mask |= BIT_STRUM_DOWN

        self._output_mask = mask
        self._send(mask)
        return mask

    def _chord_commit(self, now_ms: float) -> None:
        if len(self._note_q) >= FIFO_CAP or len(self._strum_q) >= FIFO_CAP:
            self._chord_open = False
            self._chord_mask = 0
            return

        assert_at = now_ms + self.strum_delay_ms - self.fret_early_ms
        strum_at = now_ms + self.strum_delay_ms

        if self._strum_q:
            last = self._strum_q[-1]
            earliest = last.strum_at + self.strum_pulse_ms
            if assert_at < earliest:
                assert_at = earliest
                strum_at = assert_at + self.fret_early_ms

        self._note_q.append(_PendingNote(self._chord_mask, assert_at))
        self._strum_q.append(_PendingStrum(self._chord_mask, strum_at))

        self._chord_open = False
        self._chord_mask = 0

    def _process_notes(self, now_ms: float) -> None:
        while self._note_q:
            n = self._note_q[0]
            if now_ms < n.assert_at:
                break

            for i, ch in enumerate(CHANNELS):
                bit = CHANNEL_BITS[ch]
                need = bool(n.fret_mask & bit)
                have = bool(self._frets_active & bit)
                if need and not have:
                    self._frets_active |= bit
                elif not need and have:
                    self._frets_active &= ~bit

            self._note_q.popleft()

    def _process_strums(self, now_ms: float) -> None:
        while self._strum_q:
            s = self._strum_q[0]
            if now_ms < s.strum_at:
                break

            self._strum_active = True
            self._strum_direction = not self._strum_direction
            self._strum_release_at = now_ms + self.strum_pulse_ms

            self._strum_q.popleft()

    def _process_releases(self, now_ms: float, detect_state: dict) -> None:
        for ch in CHANNELS:
            if not self._release_pending[ch]:
                continue
            if now_ms < self._release_at.get(ch, float("inf")):
                continue

            ch_state = detect_state.get(ch, {})
            if ch_state.get("pressed", False):
                continue

            bit = CHANNEL_BITS[ch]
            needed = False
            for n in self._note_q:
                if n.fret_mask & bit:
                    needed = True
                    break
            if not needed:
                for s in self._strum_q:
                    if s.fret_mask & bit:
                        needed = True
                        break
            if needed:
                continue

            self._frets_active &= ~bit
            self._release_pending[ch] = False

    def _release_all(self) -> None:
        self._frets_active = 0
        self._strum_active = False
        self._note_q.clear()
        self._strum_q.clear()
        self._chord_open = False
        self._chord_mask = 0
        for ch in CHANNELS:
            self._release_pending[ch] = False
        self._output_mask = 0
        self._send(0)

    def _send(self, mask: int) -> None:
        if self._ser is not None and self._ser.is_open:
            try:
                self._ser.write(bytes([mask & 0x7F]))
            except serial.SerialException:
                pass
