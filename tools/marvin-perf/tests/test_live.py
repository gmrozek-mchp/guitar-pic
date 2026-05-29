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
    HDR_SIZE,
    PERF_CMD_HDR_MAGIC,
    PERF_CMD_SET_TYPE_MASK,
    PERF_LOG_HDR_MAGIC,
    EXPECTED_SCHEMA_VERSION,
    RecordType,
    Session,
    encode_set_mask_payload,
)
from marvin_perf.web.live import _LiveSession


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
