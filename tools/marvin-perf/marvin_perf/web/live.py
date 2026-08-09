"""Single-tenant live session: serial reader thread + WS push.

The reader thread owns the open SerialSource and runs the frame decoder.
Decoded records are pushed onto a per-WS-attach asyncio.Queue via
``loop.call_soon_threadsafe``. The CLI's `live`/`set-mask` primitives are
reused — `frame_encode`, `encode_set_mask_payload`, `SerialSource.send_command`
— so the wire-level concerns sit in transport.py / framing.py / records.py
and this module only composes them.

Recording: while a recording is active, the reader thread mirrors the
validated framed bytes to ``<capture_dir>/perf.bin``. Stop / serial error /
``stop()`` all funnel through the same finalize path so the bin is always
opened cleanly by offline mode.
"""

from __future__ import annotations

import asyncio
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import IO, Any

from fastapi import WebSocket, WebSocketDisconnect

from ..capture import (
    BIN_NAME,
    CaptureSource,
    finalize_capture_dir,
    init_capture_dir,
)
from ..decode import decode_record
from ..framing import FrameStats, frame_encode, iter_frames
from ..records import (
    HDR_SIZE,
    PERF_OVERLAY_STRIP,
    DEFAULT_REGION_RECT,
    RECORD_TYPE_BY_NAME,
    REGION_SLOTS,
    REGION_SLOT_BY_NUM,
    DetectorConfig,
    Session,
    Strip,
    StripKind,
    encode_region_stream_payload,
    payload_with_ts_counter,
    encode_set_mask_payload,
    encode_set_overlay_payload,
    encode_snapshot_payload,
)
from ..snapshot import (
    CompletedSnapshot,
    SnapshotAssembler,
    SnapshotError,
    save_snapshot,
)

# Record types whose latest framed bytes get prepended to a new recording's
# bin so the file is self-contained. Order matters for downstream parsers
# that key on first-seen-of-type — SESSION must come first so timer_freq_hz
# resolves before any record carrying a ts_counter is seen.
_PREPEND_TYPES: tuple[type, ...] = (Session, DetectorConfig)
from ..transport import SerialSource


# Bounded so a slow WS write can't stack up minutes of stale records: the
# user wants "fresh" data when they toggle a mask, not the queue's history.
# At 60 Hz the queue holds ~8 s of all-types backlog before drop-oldest kicks
# in; for STRIP-heavy streams it's much less, which is the desired shape.
_QUEUE_MAX = 512


# A snapshot's band burst takes ~0.4-1 s on the wire; this margin also covers
# the device re-locking HDMI before the first band. If no complete frame lands
# in this window the request is abandoned and the UI told it timed out.
_SNAPSHOT_TIMEOUT_S = 8.0


def _restamp_framed(framed: bytes, ts_counter: int | None) -> bytes:
    """Re-stamp a wire frame's record header ts_counter and re-frame it.

    `framed` is SOF(4) + LEN(2) + payload + FCS(2). Returned unchanged when
    `ts_counter` is None (nothing seen yet, so nothing better to stamp) or the
    frame is too short to hold a record header.
    """
    if ts_counter is None:
        return framed
    payload = framed[6:-2]
    if len(payload) < HDR_SIZE:
        return framed
    return frame_encode(payload_with_ts_counter(payload, ts_counter))


@dataclass
class _RegionState:
    """Per-slot region-stream state (enable + the rect last pushed to it)."""

    enabled: bool = False
    rect: tuple[int, int, int, int] = DEFAULT_REGION_RECT


@dataclass
class _State:
    port: str | None = None
    mask: int = 0xFFFFFFFF
    # Mirrors the device's boot default (PERF_OVERLAY_STRIP on). Tracked so a
    # late-attaching WS client can render the toggle in the right state.
    overlay_enabled: bool = True
    # Region streams, one entry per device slot (REGION_SLOTS): the 1p scoring
    # block and the two 2-player amp scoreboards. All off at device boot; the
    # strips ride the normal record path into the capture .bin like any other
    # record. Keyed by slot number.
    regions: dict[int, _RegionState] = field(
        default_factory=lambda: {s.slot: _RegionState(rect=s.rect) for s in REGION_SLOTS}
    )
    started_at: str | None = None
    framing_stats: FrameStats = field(default_factory=FrameStats)
    last_session_dict: dict[str, Any] | None = None
    # Most-recent framed bytes of records that should be prepended to a
    # new recording so the bin is self-contained from byte 0. Keyed by
    # record-class name. SESSION is one-shot per sink-attach (firmware
    # only emits it on connect-up edge) so without prepending we'd miss
    # timer_freq_hz. DETECTOR_CONFIG re-emits at ~1 Hz, but a short
    # capture might end before the next heartbeat — prepending keeps
    # STRIP-overlay rendering correct even on sub-second captures.
    prepend_framed: dict[str, bytes] = field(default_factory=dict)
    # Device ts_counter of the most recent decoded record. Prepended records
    # are re-stamped to it so a capture's first record sits at the moment
    # recording started, not at whenever the device happened to emit it —
    # a cached SESSION can otherwise be minutes stale, and every host-side
    # consumer treats the first record's ts_counter as the timeline origin.
    last_ts_counter: int | None = None


@dataclass
class _Recording:
    fh: IO[bytes]
    dir: Path
    started_at: str
    bytes_written: int = 0
    n_frames: int = 0


@dataclass
class _SnapPending:
    assembler: SnapshotAssembler
    out_stem: str | None
    deadline: float  # time.monotonic() value
    started_at: str


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
        self._rec: _Recording | None = None
        self._snap: _SnapPending | None = None
        self._last_snapshot: CompletedSnapshot | None = None
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
        """Idempotent: safe to call when not active. Auto-finalizes any
        in-flight recording so the bin is openable from offline mode."""
        # Finalize first: while the reader is still alive, in-flight frames
        # land in the bin instead of being lost to the close race.
        try:
            self.record_stop()
        except Exception:
            pass
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

    def set_overlay(self, enabled: bool) -> bool:
        """Enable/disable the per-fret target rings on the SENSING strip.

        Only touches the viewer strip copy on the device — snapshots and the
        LVDS panel are unaffected.
        """
        with self._lock:
            ser = self._ser
            if ser is None:
                raise RuntimeError("live session not active")
            self._state.overlay_enabled = enabled
        flags = PERF_OVERLAY_STRIP if enabled else 0
        ser.send_command(frame_encode(encode_set_overlay_payload(flags)))
        self._post("overlay", {"enabled": enabled, "source": "client"})
        return enabled

    def set_region_stream(
        self,
        enabled: bool,
        rect: tuple[int, int, int, int] | None = None,
        slot: int = 0,
    ) -> dict[str, Any]:
        """Start/stop one slot's region stream (one strip per frame).

        The strips are ordinary PERF_REC_STRIP records, so an active recording
        captures them into the .bin like everything else — no special path. The
        slot fixes the strip kind (REGION_SLOTS); each slot defaults to its own
        rect and toggles independently of the others.
        """
        if slot not in REGION_SLOT_BY_NUM:
            raise ValueError(f"unknown region slot {slot}")
        with self._lock:
            ser = self._ser
            if ser is None:
                raise RuntimeError("live session not active")
            st = self._state.regions[slot]
            if rect is not None:
                st.rect = tuple(rect)  # type: ignore[assignment]
            x, y, w, h = st.rect
            st.enabled = enabled
        ser.send_command(
            frame_encode(encode_region_stream_payload(enabled, x, y, w, h, slot=slot))
        )
        info = {
            "slot": slot,
            "id": REGION_SLOT_BY_NUM[slot].id,
            "enabled": enabled,
            "rect": [x, y, w, h],
            "source": "client",
        }
        self._post("region", info)
        return info

    def _region_dict_locked(self) -> dict[str, Any]:
        """Every slot's state, keyed by slot id, plus slot 0 flattened.

        The flattened `enabled`/`rect` keep slot 0 (the original single region
        stream) readable by anything that predates the slot table.
        """
        slots = {}
        for s in REGION_SLOTS:
            st = self._state.regions[s.slot]
            x, y, w, h = st.rect
            slots[s.id] = {
                "slot": s.slot,
                "kind": int(s.kind),
                "label": s.label,
                "enabled": st.enabled,
                "rect": [x, y, w, h],
            }
        zero = self._state.regions[0]
        return {
            "enabled": zero.enabled,
            "rect": list(zero.rect),
            "slots": slots,
        }

    def status(self) -> dict[str, Any]:
        with self._lock:
            active = self.is_active()
            return {
                "active": active,
                "port": self._state.port if active else None,
                "mask": f"0x{self._state.mask:08x}" if active else None,
                "overlay": self._state.overlay_enabled if active else None,
                "region": self._region_dict_locked() if active else None,
                "recording": self._recording_dict_locked(),
                "started_at": self._state.started_at if active else None,
                "framing": {
                    "frames_ok": self._state.framing_stats.frames_ok,
                    "fcs_err": self._state.framing_stats.fcs_mismatches,
                    "resync_drop": self._state.framing_stats.bytes_resync_dropped,
                    "bad_lengths": self._state.framing_stats.bad_lengths,
                },
            }

    def _recording_dict_locked(self) -> dict[str, Any] | None:
        rec = self._rec
        if rec is None:
            return None
        return {
            "capture_dir": str(rec.dir),
            "bytes_written": rec.bytes_written,
            "n_frames": rec.n_frames,
            "started_at": rec.started_at,
        }

    # ─── recording ──────────────────────────────────────────────────────

    def record_start(self, out_dir: str | Path) -> dict[str, Any]:
        """Open ``<out_dir>/perf.bin`` and start mirroring framed bytes.

        Reader thread picks up the new ``self._rec`` on its next iteration
        and writes ``frame.framed`` per validated frame.
        """
        with self._lock:
            if not self.is_active():
                raise RuntimeError("live session not active")
            if self._rec is not None:
                raise RuntimeError("already recording")
            cap_dir = init_capture_dir(out_dir, exist_ok=False)
            bin_path = cap_dir / BIN_NAME
            fh = bin_path.open("wb")
            # Prepend cached one-shot / heartbeat records (SESSION,
            # DETECTOR_CONFIG) in _PREPEND_TYPES order so the bin is
            # self-contained from byte 0 even when recording starts
            # mid-session. Skipped silently if a type wasn't seen yet.
            # Each is re-stamped to the newest ts_counter seen so it lands at
            # the head of the recording's own time span (error bounded by one
            # record interval) instead of dragging the origin back to when the
            # device emitted it.
            bytes_written = 0
            n_frames = 0
            for cls in _PREPEND_TYPES:
                framed = self._state.prepend_framed.get(cls.__name__)
                if framed is not None:
                    framed = _restamp_framed(framed, self._state.last_ts_counter)
                    fh.write(framed)
                    bytes_written += len(framed)
                    n_frames += 1
            self._rec = _Recording(
                fh=fh,
                dir=cap_dir,
                started_at=datetime.now(timezone.utc).isoformat(),
                bytes_written=bytes_written,
                n_frames=n_frames,
            )
            snapshot = self._recording_dict_locked()
        self._post("recording", {"state": "started", **(snapshot or {})})
        return snapshot or {}

    def record_stop(self) -> dict[str, Any] | None:
        """Idempotent. Closes the bin, writes manifest.json, returns the
        manifest summary. ``None`` if not recording."""
        with self._lock:
            rec = self._rec
            self._rec = None
            port = self._state.port
            session = self._state.last_session_dict
        if rec is None:
            return None
        stopped_at_dt = datetime.now(timezone.utc)
        stopped_at = stopped_at_dt.isoformat()
        try:
            started_at_dt = datetime.fromisoformat(rec.started_at)
            duration_s = (stopped_at_dt - started_at_dt).total_seconds()
        except Exception:
            duration_s = None
        try:
            rec.fh.flush()
        except Exception:
            pass
        try:
            rec.fh.close()
        except Exception:
            pass
        # Pull timer_freq_hz from the cached SESSION if we saw one on the
        # WS at any point. Bin-derived freq still wins inside synthesize.
        timer_freq_fallback = None
        if session is not None:
            try:
                timer_freq_fallback = int(session.get("timer_freq_hz"))
            except (TypeError, ValueError):
                timer_freq_fallback = None
        manifest_dict: dict[str, Any]
        try:
            manifest = finalize_capture_dir(
                rec.dir,
                source=CaptureSource(kind="serial", port=port or ""),
                timer_freq_hz_fallback=timer_freq_fallback,
                recording_started_at=rec.started_at,
                recording_stopped_at=stopped_at,
                recording_duration_s=duration_s,
            )
            manifest_dict = manifest.to_dict()
        except Exception as e:
            manifest_dict = {"error": str(e)}
        result = {
            "capture_dir": str(rec.dir),
            "bytes_written": rec.bytes_written,
            "n_frames": rec.n_frames,
            "manifest": manifest_dict,
        }
        self._post("recording", {"state": "stopped", **result})
        return result

    # ─── snapshot ───────────────────────────────────────────────────────

    def request_snapshot(self, out_stem: str | None) -> dict[str, Any]:
        """Send PERF_CMD_SNAPSHOT; the reader thread assembles the reply.

        The session owns the only serial handle, so the snapshot rides the
        live link rather than a second port. Returned bands are reassembled
        in ``_feed_snapshot`` and announced over the WS on completion.
        """
        with self._lock:
            ser = self._ser
            if ser is None or not self.is_active():
                raise RuntimeError("live session not active")
            if self._snap is not None:
                raise RuntimeError("snapshot already in progress")
            self._snap = _SnapPending(
                assembler=SnapshotAssembler(),
                out_stem=out_stem or None,
                deadline=time.monotonic() + _SNAPSHOT_TIMEOUT_S,
                started_at=datetime.now(timezone.utc).isoformat(),
            )
        ser.send_command(frame_encode(encode_snapshot_payload()))
        self._post("snapshot", {"state": "requested"})
        return {"state": "requested"}

    def last_snapshot(self) -> CompletedSnapshot | None:
        with self._lock:
            return self._last_snapshot

    def _feed_snapshot(self, strip: Strip) -> None:
        """Reader-thread tap: drive the pending SnapshotAssembler with a
        SNAPSHOT band. On the LAST band, save to disk (if a stem was given),
        cache the frame for the PNG endpoint, and announce over the WS."""
        with self._lock:
            snap = self._snap
        if snap is None:
            return
        try:
            completed = snap.assembler.add(strip)
        except SnapshotError as e:
            with self._lock:
                if self._snap is snap:
                    self._snap = None
            self._post("snapshot", {"state": "error", "msg": str(e)})
            return
        if completed is None:
            return

        with self._lock:
            self._snap = None
            self._last_snapshot = completed
        paths: list[str] = []
        save_error: str | None = None
        if snap.out_stem:
            try:
                paths = [str(p) for p in save_snapshot(completed, snap.out_stem)]
            except Exception as e:
                save_error = str(e)
        payload: dict[str, Any] = {
            "state": "saved",
            "width": completed.width,
            "height": completed.height,
            "frame_epoch": completed.frame_epoch,
            "paths": paths,
        }
        if save_error is not None:
            payload["save_error"] = save_error
        self._post("snapshot", payload)

    def _check_snapshot_timeout(self) -> None:
        with self._lock:
            snap = self._snap
            if snap is None or time.monotonic() < snap.deadline:
                return
            self._snap = None
        self._post("snapshot", {"state": "timeout"})

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
                    "overlay": self._state.overlay_enabled,
                    "recording": self._recording_dict_locked(),
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
                elif msg_type == "recording":
                    await ws.send_json({"type": "recording", **payload})
                elif msg_type == "snapshot":
                    await ws.send_json({"type": "snapshot", **payload})
                elif msg_type == "overlay":
                    await ws.send_json({"type": "overlay", **payload})
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
                self._check_snapshot_timeout()
                self._mirror_to_recording(frame.framed)
                try:
                    rec = decode_record(frame.payload)
                except Exception:
                    continue
                with self._lock:
                    self._state.last_ts_counter = rec.hdr.ts_counter
                if isinstance(rec, Strip) and rec.kind == int(StripKind.SNAPSHOT):
                    # Snapshot bands feed the assembler and never enter the
                    # normal record stream / strip slots.
                    self._feed_snapshot(rec)
                    continue
                if self._record_to_dict is None:
                    continue
                rec_dict = self._record_to_dict(rec, include_bgr=True)
                if isinstance(rec, Session):
                    with self._lock:
                        self._state.last_session_dict = rec_dict
                if isinstance(rec, _PREPEND_TYPES):
                    with self._lock:
                        self._state.prepend_framed[type(rec).__name__] = frame.framed
                self._post("record", rec_dict)
        except Exception as e:
            self._post("error", {"code": "serial-error", "msg": str(e)})
        finally:
            # Reader exit (clean stop or serial error): finalize any
            # active recording so the bin is left in an openable state.
            try:
                self.record_stop()
            except Exception:
                pass

    def _mirror_to_recording(self, framed: bytes) -> None:
        """Reader-thread tap: write validated framed bytes to the active
        recording, if any. Disable the recording on write failure rather
        than tear down the whole session."""
        with self._lock:
            rec = self._rec
            if rec is None:
                return
            try:
                rec.fh.write(framed)
                rec.bytes_written += len(framed)
                rec.n_frames += 1
                return
            except OSError as e:
                self._rec = None
                err_msg = str(e)
                fh = rec.fh
        try:
            fh.close()
        except Exception:
            pass
        self._post("error", {"code": "record-write", "msg": err_msg})

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
