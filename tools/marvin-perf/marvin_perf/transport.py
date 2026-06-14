"""Byte sources feeding `framing.iter_frames`.

Each source is an iterable of `bytes` chunks. Chunk size is not load-bearing —
the framer handles arbitrary boundaries.
"""

from __future__ import annotations

from collections.abc import Iterator
from contextlib import AbstractContextManager
from pathlib import Path


# ─── File source ──────────────────────────────────────────────────────────────


class FileSource(AbstractContextManager["FileSource"]):
    """Iterate over fixed-size chunks from a captured `.bin` file."""

    def __init__(self, path: str | Path, chunk_bytes: int = 64 * 1024) -> None:
        self.path = Path(path)
        self.chunk_bytes = chunk_bytes
        self._fh: BinaryIO | None = None

    def __enter__(self) -> "FileSource":
        self._fh = self.path.open("rb")
        return self

    def __exit__(self, *_exc: object) -> None:
        if self._fh is not None:
            self._fh.close()
            self._fh = None

    def __iter__(self) -> Iterator[bytes]:
        if self._fh is None:
            raise RuntimeError("FileSource must be used as a context manager")
        while True:
            chunk = self._fh.read(self.chunk_bytes)
            if not chunk:
                return
            yield chunk


# ─── Serial source ────────────────────────────────────────────────────────────


class SerialSource(AbstractContextManager["SerialSource"]):
    """Read bytes from the marvin USB-device CDC ACM port via pyserial.

    The firmware sink is DTR-gated (`perf_log_sink_cdc.c:216`); pyserial raises
    DTR by default on open, which triggers the firmware to re-emit a SESSION
    record. CDC ACM ignores baud rate; we still pass one because pyserial
    requires it.
    """

    def __init__(
        self,
        port: str,
        *,
        baudrate: int = 921600,
        chunk_bytes: int = 4096,
        read_timeout_s: float = 0.1,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.chunk_bytes = chunk_bytes
        self.read_timeout_s = read_timeout_s
        self._ser: object | None = None

    def __enter__(self) -> "SerialSource":
        # Imported lazily so file-only workflows do not require pyserial.
        import serial  # noqa: WPS433  (intentional lazy import)

        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=self.read_timeout_s,
            dsrdtr=False,
        )
        # Make DTR explicit — some platforms default it off.
        try:
            self._ser.dtr = True  # type: ignore[attr-defined]
        except (OSError, AttributeError):
            pass
        return self

    def __exit__(self, *_exc: object) -> None:
        if self._ser is not None:
            try:
                self._ser.close()  # type: ignore[attr-defined]
            finally:
                self._ser = None

    def send_command(self, framed: bytes) -> int:
        """Write pre-framed command bytes to the device.

        Caller frames via `framing.frame_encode`. Returns bytes written.
        """
        if self._ser is None:
            raise RuntimeError("SerialSource must be used as a context manager")
        return self._ser.write(framed)  # type: ignore[attr-defined,no-any-return]

    def read_chunk(self) -> bytes:
        """One read of whatever has arrived; possibly empty on timeout.

        Lets a caller drive the framer with its own deadline/idle logic
        (e.g. `snapshot`) instead of the infinite `__iter__` loop.
        """
        if self._ser is None:
            raise RuntimeError("SerialSource must be used as a context manager")
        ser = self._ser
        n = max(1, getattr(ser, "in_waiting", 0))  # type: ignore[arg-type]
        return ser.read(min(n, self.chunk_bytes))  # type: ignore[attr-defined,no-any-return]

    def __iter__(self) -> Iterator[bytes]:
        if self._ser is None:
            raise RuntimeError("SerialSource must be used as a context manager")
        ser = self._ser
        while True:
            # `read` with timeout returns whatever has arrived (possibly empty
            # on timeout). `in_waiting` lets us drain the OS buffer in one go
            # when bytes are already queued.
            n = max(1, getattr(ser, "in_waiting", 0))  # type: ignore[arg-type]
            chunk: bytes = ser.read(min(n, self.chunk_bytes))  # type: ignore[attr-defined]
            if chunk:
                yield chunk


