"""
Data source abstraction for fret-tuner.

Provides SerialStream (live UART frames) and CsvStream (offline replay)
that yield Sample namedtuples through a common generator interface.
"""

import csv
import struct
import time
from collections import deque
from typing import Generator, NamedTuple

import serial
import serial.tools.list_ports

CHANNELS = ("green", "red", "yellow", "blue", "orange")

START_BYTE = 0x03
END_BYTE = 0xFC
FRAME_SIZE = 12
FRAME_STRUCT = struct.Struct("<BHHHHHBx"[:-1])  # see _unpack_frame

_FRAME_FMT = "<BHHHHHB"
_FRAME_STRUCT = struct.Struct(_FRAME_FMT)


class Sample(NamedTuple):
    timestamp: float
    green: int
    red: int
    yellow: int
    blue: int
    orange: int


def enumerate_serial_ports() -> list[dict]:
    """Return [{"name": "/dev/cu.usbmodemXXXX", "label": "..."}, ...].

    Used by the actuator-port and (future) data-port dropdowns. On macOS,
    skips the noisy `/dev/tty.*` aliases and prefers `/dev/cu.*` callout
    devices (which is what the existing CLI examples use).
    """
    out: list[dict] = []
    for p in sorted(serial.tools.list_ports.comports(), key=lambda c: c.device):
        device = p.device
        # On macOS pyserial reports both /dev/cu.* and /dev/tty.*. Filter the
        # tty.* duplicates -- callers should use the cu.* aliases for write.
        if device.startswith("/dev/tty.") and any(
            other.device == "/dev/cu." + device[len("/dev/tty."):]
            for other in serial.tools.list_ports.comports()
        ):
            continue
        label_parts = [p.description or "", p.manufacturer or ""]
        label = " - ".join([s for s in label_parts if s and s != "n/a"]).strip()
        if not label:
            label = device
        else:
            label = f"{device} ({label})"
        out.append({"name": device, "label": label})
    return out


def _unpack_frame(buf: bytes) -> tuple[int, int, int, int, int] | None:
    """Unpack a 12-byte frame, returning channel values or None on bad framing."""
    if len(buf) != FRAME_SIZE:
        return None
    start, green, red, yellow, blue, orange, end = _FRAME_STRUCT.unpack(buf)
    if start != START_BYTE or end != END_BYTE:
        return None
    return green, red, yellow, blue, orange


class SerialStream:
    """Read live binary frames from the fretboard UART stream.

    Can either open its own serial port (pass port/baudrate) or use an
    existing serial.Serial instance (pass ser).  When an external instance
    is provided the caller owns its lifetime -- samples() will not close it.
    """

    def __init__(self, port: str = "", baudrate: int = 115200,
                 ser: serial.Serial | None = None):
        self.port = port
        self.baudrate = baudrate
        self._external_ser = ser

    def samples(self) -> Generator[Sample, None, None]:
        owns_ser = self._external_ser is None
        if owns_ser:
            ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
            )
            ser.reset_input_buffer()
        else:
            ser = self._external_ser

        try:
            t0 = time.monotonic()
            buf = bytearray()

            while True:
                chunk = ser.read(max(1, ser.in_waiting))
                if not chunk:
                    continue
                buf.extend(chunk)

                while len(buf) >= FRAME_SIZE:
                    idx = buf.find(START_BYTE)
                    if idx < 0:
                        buf.clear()
                        break
                    if idx > 0:
                        del buf[:idx]
                    if len(buf) < FRAME_SIZE:
                        break

                    candidate = bytes(buf[:FRAME_SIZE])
                    vals = _unpack_frame(candidate)
                    if vals is None:
                        del buf[:1]
                        continue

                    del buf[:FRAME_SIZE]
                    ts = time.monotonic() - t0
                    yield Sample(ts, *vals)
        finally:
            if owns_ser:
                ser.close()


class CsvStream:
    """Replay a captured CSV file, yielding samples at original timing."""

    # Column name mapping (CSV header -> channel index)
    _COL_MAP = {
        "GREEN RAW": "green",
        "RED RAW": "red",
        "YELLOW RAW": "yellow",
        "BLUE RAW": "blue",
        "ORANGE RAW": "orange",
    }

    def __init__(self, path: str, realtime: bool = True):
        self.path = path
        self.realtime = realtime

    def samples(self) -> Generator[Sample, None, None]:
        with open(self.path, newline="") as f:
            reader = csv.DictReader(f)
            wall_start: float | None = None
            data_start: float | None = None

            for row in reader:
                ts = float(row["timestamp"])

                if data_start is None:
                    data_start = ts
                    wall_start = time.monotonic()

                if self.realtime:
                    elapsed_data = ts - data_start
                    target = wall_start + elapsed_data
                    now = time.monotonic()
                    if target > now:
                        time.sleep(target - now)

                vals = {
                    ch: int(row[csv_col])
                    for csv_col, ch in self._COL_MAP.items()
                }
                yield Sample(
                    timestamp=ts - data_start,
                    green=vals["green"],
                    red=vals["red"],
                    yellow=vals["yellow"],
                    blue=vals["blue"],
                    orange=vals["orange"],
                )
