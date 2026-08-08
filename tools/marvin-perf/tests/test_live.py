"""Single-tenant live session — reader thread lifecycle + control plane."""

from __future__ import annotations

import json
import struct
import threading
import time
from pathlib import Path
from typing import Any

import pytest

from marvin_perf.capture import BIN_NAME, MANIFEST_NAME
from marvin_perf.framing import FrameStats, frame_encode, iter_frames
from marvin_perf.records import (
    DEFAULT_REGION_RECT,
    HDR_SIZE,
    PERF_CMD_HDR_MAGIC,
    PERF_CMD_REGION_STREAM,
    PERF_CMD_SET_OVERLAY,
    PERF_CMD_SET_TYPE_MASK,
    PERF_CMD_SNAPSHOT,
    PERF_LOG_HDR_MAGIC,
    PERF_OVERLAY_STRIP,
    EXPECTED_SCHEMA_VERSION,
    REGION_SLOT_BY_ID,
    STRIP_FLAG_LAST,
    RecordType,
    Session,
    StripKind,
    encode_region_stream_payload,
    encode_set_mask_payload,
    encode_set_overlay_payload,
    encode_snapshot_payload,
)
from marvin_perf.web.live import _LiveSession

from .conftest import build_strip_payload


class _FakeSerial:
    """Stand-in for SerialSource — context-managed iterable + send_command capture.

    The iterator drains an internal byte queue (``feed`` from the test) and
    blocks once empty until the session closes the source. This lets a test
    push framed bytes through the live reader thread on demand."""

    def __init__(self, port: str) -> None:
        self.port = port
        self.sent: list[bytes] = []
        self._closed = threading.Event()
        self._has_data = threading.Event()
        self._chunks: list[bytes] = []
        self._chunks_lock = threading.Lock()
        self._entered = False

    def __enter__(self) -> "_FakeSerial":
        self._entered = True
        return self

    def __exit__(self, *_exc: object) -> None:
        self._closed.set()
        self._has_data.set()

    def send_command(self, framed: bytes) -> int:
        self.sent.append(framed)
        return len(framed)

    def feed(self, chunk: bytes) -> None:
        with self._chunks_lock:
            self._chunks.append(chunk)
        self._has_data.set()

    def __iter__(self):
        while not self._closed.is_set():
            self._has_data.wait(timeout=0.05)
            with self._chunks_lock:
                pending = self._chunks
                self._chunks = []
                if not pending:
                    self._has_data.clear()
            for c in pending:
                yield c


def _make_session() -> _LiveSession:
    sess = _LiveSession()
    sess.configure(record_to_dict=lambda rec, *, include_bgr=False: {})
    return sess


def test_start_then_stop_releases_thread() -> None:
    sess = _make_session()
    fake_holder: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        fake_holder["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    assert sess.is_active()
    assert fake_holder["ser"]._entered

    sess.stop()
    assert not sess.is_active()
    assert fake_holder["ser"]._closed.is_set()
    # Idempotent.
    sess.stop()


def test_set_mask_round_trips_to_serial_send() -> None:
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    try:
        resolved = sess.set_mask_from_names(["STAMP", "SESSION"])
        expected = (1 << RecordType.SESSION) | (1 << RecordType.STAMP)
        assert resolved == expected

        ser = captured["ser"]
        assert len(ser.sent) == 1
        # Compare directly to a fresh encode (iter_frames rejects sub-16-byte
        # payloads — host→device commands are 8 bytes — so we can't round-trip
        # through it here).
        assert ser.sent[0] == frame_encode(encode_set_mask_payload(expected))
        # And the payload portion starts with the command magic + opcode.
        payload = ser.sent[0][6:-2]
        assert payload[:2] == bytes(
            (PERF_CMD_HDR_MAGIC & 0xFF, (PERF_CMD_HDR_MAGIC >> 8) & 0xFF)
        )
        assert payload[2] == PERF_CMD_SET_TYPE_MASK
    finally:
        sess.stop()


def test_set_region_stream_round_trips_to_serial_send() -> None:
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    try:
        info = sess.set_region_stream(True)  # default rect = scoring block
        assert info["enabled"] is True
        assert tuple(info["rect"]) == DEFAULT_REGION_RECT
        ser = captured["ser"]
        assert ser.sent[-1] == frame_encode(
            encode_region_stream_payload(True, *DEFAULT_REGION_RECT)
        )
        payload = ser.sent[-1][6:-2]
        assert payload[2] == PERF_CMD_REGION_STREAM

        sess.set_region_stream(False)
        assert ser.sent[-1] == frame_encode(
            encode_region_stream_payload(False, *DEFAULT_REGION_RECT)
        )
        assert sess.status()["region"]["enabled"] is False
    finally:
        sess.stop()


def test_set_region_stream_when_inactive_raises() -> None:
    sess = _make_session()
    with pytest.raises(RuntimeError):
        sess.set_region_stream(True)


def test_region_slots_toggle_independently() -> None:
    """Each slot carries its own enable + rect; enabling one leaves the others off."""
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    try:
        ser = captured["ser"]
        left = REGION_SLOT_BY_ID["score-2p-left"]
        right = REGION_SLOT_BY_ID["score-2p-right"]

        info = sess.set_region_stream(True, slot=left.slot)
        assert (info["slot"], info["id"]) == (left.slot, left.id)
        assert tuple(info["rect"]) == left.rect
        assert ser.sent[-1] == frame_encode(
            encode_region_stream_payload(True, *left.rect, slot=left.slot)
        )

        sess.set_region_stream(True, slot=right.slot)
        slots = sess.status()["region"]["slots"]
        assert slots[left.id]["enabled"] is True
        assert slots[right.id]["enabled"] is True
        assert slots["score"]["enabled"] is False  # slot 0 untouched
        assert slots[right.id]["kind"] == int(StripKind.SCORE_2P_RIGHT)

        # Stopping one slot leaves the other streaming.
        sess.set_region_stream(False, slot=left.slot)
        assert ser.sent[-1] == frame_encode(
            encode_region_stream_payload(False, *left.rect, slot=left.slot)
        )
        slots = sess.status()["region"]["slots"]
        assert slots[left.id]["enabled"] is False
        assert slots[right.id]["enabled"] is True
        # The flattened view stays slot 0, for readers that predate the table.
        assert sess.status()["region"]["enabled"] is False
    finally:
        sess.stop()


def test_set_region_stream_unknown_slot_raises() -> None:
    sess = _make_session()
    sess.start("/dev/null", ser_factory=_FakeSerial)
    try:
        with pytest.raises(ValueError):
            sess.set_region_stream(True, slot=9)
    finally:
        sess.stop()


def test_set_mask_when_inactive_raises() -> None:
    sess = _make_session()
    with pytest.raises(RuntimeError):
        sess.set_mask_from_int(0xFF)


def test_set_mask_unknown_name_raises() -> None:
    sess = _make_session()

    def factory(port: str) -> _FakeSerial:
        return _FakeSerial(port)

    sess.start("/dev/null", ser_factory=factory)
    try:
        with pytest.raises(ValueError):
            sess.set_mask_from_names(["BOGUS"])
    finally:
        sess.stop()


def test_set_mask_all_token() -> None:
    sess = _make_session()

    def factory(port: str) -> _FakeSerial:
        return _FakeSerial(port)

    sess.start("/dev/null", ser_factory=factory)
    try:
        resolved = sess.set_mask_from_names(["ALL"])
        assert resolved == 0xFFFFFFFF
    finally:
        sess.stop()


def test_double_start_raises() -> None:
    sess = _make_session()

    def factory(port: str) -> _FakeSerial:
        return _FakeSerial(port)

    sess.start("/dev/null", ser_factory=factory)
    try:
        with pytest.raises(RuntimeError):
            sess.start("/dev/null", ser_factory=factory)
    finally:
        sess.stop()


def test_status_inactive() -> None:
    sess = _make_session()
    s = sess.status()
    assert s["active"] is False
    assert s["port"] is None
    assert s["mask"] is None


def test_status_active_reports_port_and_mask() -> None:
    sess = _make_session()

    def factory(port: str) -> _FakeSerial:
        return _FakeSerial(port)

    sess.start("/dev/cu.usbmodem-test", ser_factory=factory)
    try:
        s = sess.status()
        assert s["active"] is True
        assert s["port"] == "/dev/cu.usbmodem-test"
        assert s["mask"] == "0xffffffff"
        assert s["recording"] is None
    finally:
        sess.stop()


# ─── Recording ─────────────────────────────────────────────────────────────

def _encode_session_payload(ts: int) -> bytes:
    """Build a SESSION record payload (HDR + body) for the recording tests."""
    hdr = struct.pack(
        "<HBBIQ",
        PERF_LOG_HDR_MAGIC, int(RecordType.SESSION), 0, 0, ts,
    )
    body = struct.pack(
        "<IHHII",
        1_000_000,                # timer_freq_hz
        EXPECTED_SCHEMA_VERSION,  # schema_version
        0,                        # _padding
        0xCAFE_BABE,              # fw_git_short
        0,                        # _reserved
    )
    return hdr + body


def _encode_stamp_payload(epoch: int, ts: int, stage_id: int = 0x10) -> bytes:
    hdr = struct.pack(
        "<HBBIQ",
        PERF_LOG_HDR_MAGIC, int(RecordType.STAMP), 0, epoch, ts,
    )
    body = struct.pack("<B3sIII", stage_id, b"\x00\x00\x00", 0, 0, 0)
    return hdr + body


def _wait_for(predicate, timeout: float = 1.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError("predicate did not become true within timeout")


def test_record_start_stop_finalizes_capture(tmp_path: Path) -> None:
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    ser = captured["ser"]
    try:
        out_dir = tmp_path / "rec1"
        sess.record_start(out_dir)

        ser.feed(frame_encode(_encode_session_payload(ts=1000)))
        ser.feed(frame_encode(_encode_stamp_payload(epoch=1, ts=1100)))
        ser.feed(frame_encode(_encode_stamp_payload(epoch=2, ts=1200)))
        ser.feed(frame_encode(_encode_stamp_payload(epoch=3, ts=1300)))

        _wait_for(lambda: sess._rec is not None and sess._rec.n_frames >= 4)

        result = sess.record_stop()
        assert result is not None
        assert result["n_frames"] == 4
        assert result["bytes_written"] > 0
        assert result["manifest"]["n_records"] == 4
        assert result["manifest"]["source"]["kind"] == "serial"

        # Bin + manifest land on disk in an offline-openable shape.
        manifest_path = out_dir / MANIFEST_NAME
        bin_path = out_dir / BIN_NAME
        assert bin_path.exists() and bin_path.stat().st_size > 0
        assert manifest_path.exists()
        loaded = json.loads(manifest_path.read_text())
        assert loaded["n_records"] == 4
        assert "Session" in loaded["producer_capabilities"]
        assert "Stamp" in loaded["producer_capabilities"]
    finally:
        sess.stop()


def test_record_stop_idempotent() -> None:
    sess = _make_session()
    sess.configure(record_to_dict=lambda rec, *, include_bgr=False: {})
    assert sess.record_stop() is None  # not active, no-op


def test_double_record_start_raises(tmp_path: Path) -> None:
    sess = _make_session()

    def factory(port: str) -> _FakeSerial:
        return _FakeSerial(port)

    sess.start("/dev/null", ser_factory=factory)
    try:
        sess.record_start(tmp_path / "rec_a")
        with pytest.raises(RuntimeError):
            sess.record_start(tmp_path / "rec_b")
    finally:
        sess.stop()


# ─── Snapshot ──────────────────────────────────────────────────────────────


def _snapshot_band(epoch: int, y: int, h: int, *, fill: int, last: bool = False) -> bytes:
    return frame_encode(
        build_strip_payload(
            frame_epoch=epoch,
            kind=int(StripKind.SNAPSHOT),
            x=0, y=y, w=4, h=h,
            flags=STRIP_FLAG_LAST if last else 0,
            fill=fill,
        )
    )


def test_snapshot_assembles_saves_and_does_not_leak(tmp_path: Path) -> None:
    seen: list[str] = []
    sess = _LiveSession()
    sess.configure(
        record_to_dict=lambda rec, *, include_bgr=False: (seen.append(type(rec).__name__), {})[1]
    )
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    ser = captured["ser"]
    try:
        stem = str(tmp_path / "shot")
        sess.request_snapshot(stem)

        # The SNAPSHOT command rode the live link.
        assert ser.sent[-1] == frame_encode(encode_snapshot_payload())
        assert ser.sent[-1][6:-2][2] == PERF_CMD_SNAPSHOT

        ser.feed(_snapshot_band(42, 0, 3, fill=0x11))
        ser.feed(_snapshot_band(42, 3, 2, fill=0x22, last=True))

        _wait_for(lambda: sess.last_snapshot() is not None, timeout=2.0)
        snap = sess.last_snapshot()
        assert snap is not None
        assert (snap.width, snap.height, snap.frame_epoch) == (4, 5, 42)

        # Saved to disk as a single lossless PNG (no raw/sidecar files).
        assert (tmp_path / "shot.png").exists()
        assert not (tmp_path / "shot.bgr").exists()
        assert not (tmp_path / "shot.json").exists()

        # Pending state cleared; bands never entered the normal record stream.
        assert sess._snap is None
        assert "Strip" not in seen
    finally:
        sess.stop()


def test_snapshot_without_out_stem_caches_but_writes_nothing(tmp_path: Path) -> None:
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    ser = captured["ser"]
    try:
        sess.request_snapshot(None)
        ser.feed(_snapshot_band(7, 0, 2, fill=0x33, last=True))
        _wait_for(lambda: sess.last_snapshot() is not None, timeout=2.0)
        assert list(tmp_path.iterdir()) == []  # nothing written
    finally:
        sess.stop()


def test_snapshot_in_progress_conflict() -> None:
    sess = _make_session()

    def factory(port: str) -> _FakeSerial:
        return _FakeSerial(port)

    sess.start("/dev/null", ser_factory=factory)
    try:
        sess.request_snapshot(None)  # latched, no bands fed yet
        with pytest.raises(RuntimeError):
            sess.request_snapshot(None)
    finally:
        sess.stop()


def test_snapshot_when_inactive_raises() -> None:
    sess = _make_session()
    with pytest.raises(RuntimeError):
        sess.request_snapshot(None)


def test_snapshot_times_out(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("marvin_perf.web.live._SNAPSHOT_TIMEOUT_S", 0.05)
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    ser = captured["ser"]
    try:
        sess.request_snapshot(None)
        time.sleep(0.1)  # let the deadline pass
        # Tick the reader loop with an unrelated frame so the timeout check runs.
        ser.feed(frame_encode(_encode_session_payload(ts=1)))
        _wait_for(lambda: sess._snap is None, timeout=2.0)
    finally:
        sess.stop()


# ─── Overlay ─────────────────────────────────────────────────────────────────


def test_set_overlay_round_trips_to_serial_send() -> None:
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    ser = captured["ser"]
    try:
        sess.set_overlay(True)
        assert ser.sent[-1] == frame_encode(encode_set_overlay_payload(PERF_OVERLAY_STRIP))
        assert ser.sent[-1][6:-2][2] == PERF_CMD_SET_OVERLAY
        assert sess.status()["overlay"] is True

        sess.set_overlay(False)
        assert ser.sent[-1] == frame_encode(encode_set_overlay_payload(0))
        assert sess.status()["overlay"] is False
    finally:
        sess.stop()


def test_set_overlay_when_inactive_raises() -> None:
    sess = _make_session()
    with pytest.raises(RuntimeError):
        sess.set_overlay(True)


def test_encode_set_overlay_payload_layout() -> None:
    payload = encode_set_overlay_payload(PERF_OVERLAY_STRIP)
    assert len(payload) == 8  # hdr(magic,cmd,reserved,pad-in-struct) + u32 flags
    magic = payload[0] | (payload[1] << 8)
    assert magic == PERF_CMD_HDR_MAGIC
    assert payload[2] == PERF_CMD_SET_OVERLAY
    flags = int.from_bytes(payload[4:8], "little")
    assert flags == PERF_OVERLAY_STRIP


def test_stop_session_auto_finalizes_recording(tmp_path: Path) -> None:
    sess = _make_session()
    captured: dict[str, _FakeSerial] = {}

    def factory(port: str) -> _FakeSerial:
        ser = _FakeSerial(port)
        captured["ser"] = ser
        return ser

    sess.start("/dev/null", ser_factory=factory)
    ser = captured["ser"]
    out_dir = tmp_path / "rec_auto"
    sess.record_start(out_dir)
    ser.feed(frame_encode(_encode_session_payload(ts=42)))
    _wait_for(lambda: sess._rec is not None and sess._rec.n_frames >= 1)
    sess.stop()
    assert (out_dir / MANIFEST_NAME).exists()
    assert (out_dir / BIN_NAME).stat().st_size > 0
