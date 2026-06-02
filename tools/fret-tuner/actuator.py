"""
Actuator -- Python port of firmware/fretboard/fret_button.c

Translates detector press/release edges into a timed sequence of
fret assertions and strum pulses, then sends a 1-byte GPIO bitmask
to the actuator microcontroller over its own UART.

Driven by sample timestamps (not wall clock) so behavior is identical
in live and CSV replay modes.

The actuator is an always-on subsystem: it owns its own serial port
(or shares one with the data source when the same physical device
is used for both ADC streaming and GPIO output). Port selection,
reconnect, and enable-gating are managed here so the timing pipeline
runs regardless of which detector is active.
"""

import logging
import threading
import time
from collections import deque
from typing import Callable, Optional

import serial

from stream import CHANNELS

log = logging.getLogger("fret-tuner.actuator")

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

FIFO_CAP = 16

DEFAULT_TIMING = {
    "STRUM_DELAY_MS": 220,
    "FRET_EARLY_MS": 50,
    "STRUM_PULSE_MS": 50,
    "CHORD_WINDOW_MS": 20,
}

ACTUATOR_BAUDRATE = 500000
RECONNECT_INTERVAL = 2.0


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
    """Strum timing pipeline + always-on serial GPIO output.

    Two attach modes:

    * **owned**: actuator opens the port itself and manages reconnect.
      Use `attach(port_name)` / `detach()`.
    * **shared**: a serial handle was opened elsewhere (the data-side
      reader) and we just write to it. The owner is responsible for
      reopening on disconnect; after each reopen they re-call
      `attach_shared(...)` with the new handle.
    """

    def __init__(self):
        self._ser: Optional[serial.Serial] = None
        self._port_name: Optional[str] = None
        self._owned: bool = False
        self._connected: bool = False
        self._enabled: bool = False

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
        self._strum_direction = False

        self._prev_pressed: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._prev_press_count: dict[str, int] = {ch: 0 for ch in CHANNELS}
        self._output_mask: int = 0

        self._lock = threading.Lock()
        self._listeners: list[Callable[[dict], None]] = []
        self._reconnect_stop = threading.Event()
        self._reconnect_thread: Optional[threading.Thread] = None

    # ----- public introspection / config ---------------------------------

    @property
    def enabled(self) -> bool:
        return self._enabled

    @property
    def output_mask(self) -> int:
        return self._output_mask

    def status(self) -> dict:
        return {
            "active": self._port_name is not None,
            "port": self._port_name,
            "owned": self._owned,
            "connected": self._connected,
            "enabled": self._enabled,
        }

    def add_status_listener(self, fn: Callable[[dict], None]) -> None:
        self._listeners.append(fn)

    def set_enabled(self, enabled: bool) -> None:
        self._enabled = enabled
        if not enabled:
            self._release_all()
        self._notify()

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

    # ----- port lifecycle ------------------------------------------------

    def attach(self, port_name: str) -> bool:
        """Open `port_name` ourselves; the actuator owns the handle."""
        self.detach()
        try:
            ser = serial.Serial(
                port=port_name,
                baudrate=ACTUATOR_BAUDRATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
            )
        except (serial.SerialException, OSError) as exc:
            log.warning("actuator: open %s failed: %s", port_name, exc)
            with self._lock:
                self._port_name = port_name
                self._owned = True
                self._connected = False
                self._ser = None
            self._notify()
            self._start_reconnect_worker()
            return False

        with self._lock:
            self._ser = ser
            self._port_name = port_name
            self._owned = True
            self._connected = True
        log.info("actuator: opened %s", port_name)
        self._notify()
        self._start_reconnect_worker()
        return True

    def attach_shared(self, port_name: str, ser: serial.Serial) -> None:
        """Use an externally-owned handle. Caller manages connect/reconnect."""
        self.detach()
        with self._lock:
            self._ser = ser
            self._port_name = port_name
            self._owned = False
            self._connected = bool(ser and ser.is_open)
        log.info("actuator: attached shared port %s", port_name)
        self._notify()

    def detach(self) -> None:
        """Release any current port (closes only if we own the handle)."""
        self._stop_reconnect_worker()
        with self._lock:
            ser = self._ser
            owned = self._owned
            self._ser = None
            self._port_name = None
            self._owned = False
            self._connected = False
        if owned and ser is not None:
            try:
                ser.close()
            except Exception:
                pass
        self._notify()

    def is_using_port(self, port_name: str) -> bool:
        with self._lock:
            return self._port_name == port_name

    # ----- timing pipeline -----------------------------------------------

    def update(self, now_ms: float, detect_state: dict) -> int:
        """Process one tick. Returns the 7-bit output bitmask."""
        if not self._enabled:
            self._output_mask = 0
            return 0

        presses = 0
        releases = 0
        for ch in CHANNELS:
            ch_state = detect_state.get(ch, {})
            pressed = ch_state.get("pressed", False)
            was_pressed = self._prev_pressed[ch]

            press_count = ch_state.get("press_count")
            if press_count is not None:
                if press_count != self._prev_press_count[ch]:
                    presses |= CHANNEL_BITS[ch]
                self._prev_press_count[ch] = press_count
            else:
                if pressed and not was_pressed:
                    presses |= CHANNEL_BITS[ch]

            if not pressed and was_pressed:
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

    def write_manual(self, mask: int) -> bool:
        """Push a raw bitmask out the actuator port (used by the manual buttons).

        Returns False if no port is attached or the write failed.
        Does not touch the timing pipeline or the enable gate.
        """
        return self._send(int(mask) & 0x7F)

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

            for ch in CHANNELS:
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

    # ----- serial output -------------------------------------------------

    def _send(self, mask: int) -> bool:
        with self._lock:
            ser = self._ser
            connected = self._connected
        if ser is None or not connected:
            return False
        if not ser.is_open:
            self._mark_disconnected()
            return False
        try:
            ser.write(bytes([mask & 0x7F]))
            return True
        except (serial.SerialException, OSError) as exc:
            log.warning("actuator: write failed: %s", exc)
            self._mark_disconnected()
            return False

    def _mark_disconnected(self) -> None:
        owned = False
        ser_to_close = None
        with self._lock:
            if self._connected:
                self._connected = False
                if self._owned:
                    owned = True
                    ser_to_close = self._ser
                    self._ser = None
        if owned and ser_to_close is not None:
            try:
                ser_to_close.close()
            except Exception:
                pass
        self._notify()

    # ----- reconnect (only for owned ports) ------------------------------

    def _start_reconnect_worker(self) -> None:
        if self._reconnect_thread and self._reconnect_thread.is_alive():
            return
        self._reconnect_stop.clear()
        t = threading.Thread(
            target=self._reconnect_loop, name="actuator-reconnect", daemon=True,
        )
        self._reconnect_thread = t
        t.start()

    def _stop_reconnect_worker(self) -> None:
        self._reconnect_stop.set()
        t = self._reconnect_thread
        if t is not None and t.is_alive():
            t.join(timeout=1.0)
        self._reconnect_thread = None

    def _reconnect_loop(self) -> None:
        while not self._reconnect_stop.is_set():
            if self._reconnect_stop.wait(RECONNECT_INTERVAL):
                return
            with self._lock:
                want = self._port_name
                owned = self._owned
                connected = self._connected
            if want is None or not owned or connected:
                continue
            try:
                ser = serial.Serial(
                    port=want,
                    baudrate=ACTUATOR_BAUDRATE,
                    bytesize=serial.EIGHTBITS,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE,
                    timeout=0.1,
                )
            except (serial.SerialException, OSError):
                continue
            with self._lock:
                if self._port_name != want or not self._owned:
                    try:
                        ser.close()
                    except Exception:
                        pass
                    continue
                self._ser = ser
                self._connected = True
            log.info("actuator: reconnected on %s", want)
            self._notify()

    # ----- listeners -----------------------------------------------------

    def _notify(self) -> None:
        s = self.status()
        for cb in list(self._listeners):
            try:
                cb(s)
            except Exception as exc:
                log.warning("actuator: status listener raised: %s", exc)
