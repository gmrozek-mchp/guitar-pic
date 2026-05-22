"""Single-tenant live session: serial reader thread + WS push.

The reader thread owns the open SerialSource and runs the frame decoder.
Decoded records are pushed onto a per-WS-attach asyncio.Queue via
``loop.call_soon_threadsafe``. The CLI's `live`/`set-mask` primitives are
reused — `frame_encode`, `encode_set_mask_payload`, `SerialSource.send_command`
— so the wire-level concerns sit in transport.py / framing.py / records.py
and this module only composes them.

Recording (write framed bytes to a capture dir) is wired in Phase 3.
"""

from __future__ import annotations

import asyncio
import threading
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any

from fastapi import WebSocket, WebSocketDisconnect

from ..decode import decode_record
from ..framing import FrameStats, frame_encode, iter_frames
from ..records import (
    RECORD_TYPE_BY_NAME,
    Session,
    encode_set_mask_payload,
)
from ..transport import SerialSource


# Bounded so a slow WS write can't stack up minutes of stale records: the
# user wants "fresh" data when they toggle a mask, not the queue's history.
# At 60 Hz the queue holds ~8 s of all-types backlog before drop-oldest kicks
# in; for STRIP-heavy streams it's much less, which is the desired shape.
_QUEUE_MAX = 512


@dataclass
class _State:
    port: str | None = None
    mask: int = 0xFFFFFFFF
    started_at: str | None = None
    framing_stats: FrameStats = field(default_factory=FrameStats)
    last_session_dict: dict[str, Any] | None = None


class _LiveSession:
    """Process-singleton; mutex guards all field writes."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._state = _State()
        self._ser: SerialSource | None = None
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._queue: asyncio.Queue[tuple[str, dict[str, Any]]] | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._ws: WebSocket | None = None
        # Late-bound to avoid a cycle: api.py passes this on import.
        self._record_to_dict = None  # type: ignore[var-annotated]

    # ─── lifecycle ──────────────────────────────────────────────────────

    def configure(self, *, record_to_dict) -> None:
        self._record_to_dict = record_to_dict

    def is_active(self) -> bool:
        with self._lock:
            return self._thread is not None and self._thread.is_alive()

    def start(self, port: str, *, ser_factory=None) -> None:
        """Open serial + start reader thread.

        ``ser_factory`` is an injection point for tests — if provided, called
        as ``ser_factory(port)`` and expected to return a context-manager
        iterable (yields bytes chunks) with ``send_command``.
        """
        with self._lock:
            if self.is_active():
                raise RuntimeError("live session already active")
            self._stop_event.clear()
            self._state = _State(
                port=port,
                started_at=datetime.now(timezone.utc).isoformat(),
            )
            ser = (ser_factory or SerialSource)(port)
            ser.__enter__()
            self._ser = ser
            self._thread = threading.Thread(
                target=self._reader_loop,
                name="marvin-perf-live",
                daemon=True,
            )
            self._thread.start()

    def stop(self) -> None:
        """Idempotent: safe to call when not active."""
        with self._lock:
            self._stop_event.set()
            ser = self._ser
            self._ser = None
            t = self._thread
            self._thread = None
        if ser is not None:
            try:
                ser.__exit__(None, None, None)
            except Exception:
                pass
        if t is not None and t.is_alive():
            t.join(timeout=3.0)

    # ─── control plane ──────────────────────────────────────────────────

    def set_mask_from_int(self, mask: int) -> int:
        m = mask & 0xFFFFFFFF
        with self._lock:
            ser = self._ser
            if ser is None:
                raise RuntimeError("live session not active")
            self._state.mask = m
        ser.send_command(frame_encode(encode_set_mask_payload(m)))
        # Drain the WS queue so the browser sees fresh records under the new
        # mask immediately, rather than draining 5–10 s of stale backlog.
        self._drain_queue()
        self._post("mask", {"value": f"0x{m:08x}", "source": "client"})
        return m

    def set_mask_from_names(self, names: list[str]) -> int:
        mask = 0
        for raw in names:
            token = raw.strip().upper()
            if not token:
                continue
            if token == "ALL":
                mask = 0xFFFFFFFF
                break
            if token not in RECORD_TYPE_BY_NAME:
                valid = ", ".join(sorted(RECORD_TYPE_BY_NAME))
                raise ValueError(f"unknown record type {token!r}; valid: {valid}, ALL")
            mask |= 1 << RECORD_TYPE_BY_NAME[token]
        return self.set_mask_from_int(mask)

    def status(self) -> dict[str, Any]:
        with self._lock:
            active = self.is_active()
            return {
                "active": active,
                "port": self._state.port if active else None,
                "mask": f"0x{self._state.mask:08x}" if active else None,
                "recording": None,  # Phase 3
                "started_at": self._state.started_at if active else None,
                "framing": {
                    "frames_ok": self._state.framing_stats.frames_ok,
                    "crc_err": self._state.framing_stats.crc_mismatches,
                    "resync_drop": self._state.framing_stats.bytes_resync_dropped,
                    "bad_lengths": self._state.framing_stats.bad_lengths,
                },
            }

    # ─── WS attachment ──────────────────────────────────────────────────

    async def attach_ws(self, ws: WebSocket) -> None:
        with self._lock:
            if self._ws is not None:
                await ws.close(code=4409, reason="live already in use")
                return
            if not self.is_active():
                await ws.close(code=4404, reason="no active live session")
                return
            self._loop = asyncio.get_running_loop()
            self._queue = asyncio.Queue(maxsize=_QUEUE_MAX)
            self._ws = ws

        await ws.accept()
        with self._lock:
            hello = {
                "type": "hello",
                "session": {
                    "started_at": self._state.started_at,
                    "port": self._state.port,
                    "mask": f"0x{self._state.mask:08x}",
                    "recording": None,
                },
            }
            replay = self._state.last_session_dict
        try:
            await ws.send_json(hello)
            if replay is not None:
                await ws.send_json({"type": "session_replay", "rec": replay})
            queue = self._queue
            while True:
                msg_type, payload = await queue.get()
                if msg_type == "record":
                    await ws.send_json({"type": "record", "rec": payload})
                elif msg_type == "mask":
                    await ws.send_json({"type": "mask", **payload})
                elif msg_type == "error":
                    await ws.send_json({"type": "error", **payload})
                    break
                elif msg_type == "framing_stats":
                    await ws.send_json({"type": "framing_stats", **payload})
        except WebSocketDisconnect:
            pass
        finally:
            with self._lock:
                self._ws = None
                self._queue = None

    # ─── reader thread ──────────────────────────────────────────────────

    def _reader_loop(self) -> None:
        ser = self._ser
        stats = self._state.framing_stats
        if ser is None:
            return
        try:
            for frame in iter_frames(ser, stats):
                if self._stop_event.is_set():
                    break
                try:
                    rec = decode_record(frame.payload)
                except Exception:
                    continue
                if self._record_to_dict is None:
                    continue
                rec_dict = self._record_to_dict(rec, include_bgr=True)
                if isinstance(rec, Session):
                    with self._lock:
                        self._state.last_session_dict = rec_dict
                self._post("record", rec_dict)
        except Exception as e:
            self._post("error", {"code": "serial-error", "msg": str(e)})

    # ─── internal: cross-thread queue post ──────────────────────────────

    def _post(self, msg_type: str, payload: dict[str, Any]) -> None:
        loop = self._loop
        queue = self._queue
        if loop is None or queue is None:
            return
        try:
            loop.call_soon_threadsafe(self._queue_put_nowait, msg_type, payload)
        except RuntimeError:
            pass

    def _queue_put_nowait(self, msg_type: str, payload: dict[str, Any]) -> None:
        # Runs on the asyncio loop thread (single producer/consumer here).
        # On overflow, drop the oldest item so the WS receiver always sees
        # the freshest data — useful when STRIP records swamp the link.
        if self._queue is None:
            return
        try:
            self._queue.put_nowait((msg_type, payload))
        except asyncio.QueueFull:
            try:
                self._queue.get_nowait()
            except asyncio.QueueEmpty:
                return
            try:
                self._queue.put_nowait((msg_type, payload))
            except asyncio.QueueFull:
                pass

    def _drain_queue(self) -> None:
        loop = self._loop
        if loop is None:
            return
        try:
            loop.call_soon_threadsafe(self._queue_drain)
        except RuntimeError:
            pass

    def _queue_drain(self) -> None:
        if self._queue is None:
            return
        while True:
            try:
                self._queue.get_nowait()
            except asyncio.QueueEmpty:
                return


SESSION = _LiveSession()
