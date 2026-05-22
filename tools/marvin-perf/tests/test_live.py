"""Single-tenant live session — reader thread lifecycle + control plane."""

from __future__ import annotations

import threading
import time
from typing import Any

import pytest

from marvin_perf.framing import FrameStats, frame_encode, iter_frames
from marvin_perf.records import (
    PERF_CMD_HDR_MAGIC,
    PERF_CMD_SET_TYPE_MASK,
    RecordType,
    encode_set_mask_payload,
)
from marvin_perf.web.live import _LiveSession


class _FakeSerial:
    """Stand-in for SerialSource — context-managed iterable + send_command capture."""

    def __init__(self, port: str) -> None:
        self.port = port
        self.sent: list[bytes] = []
        self._closed = threading.Event()
        self._entered = False

    def __enter__(self) -> "_FakeSerial":
        self._entered = True
        return self

    def __exit__(self, *_exc: object) -> None:
        self._closed.set()

    def send_command(self, framed: bytes) -> int:
        self.sent.append(framed)
        return len(framed)

    def __iter__(self):
        # Block until the session closes us. Yields nothing — reader thread
        # idles in iter_frames waiting for chunks that never come.
        while not self._closed.is_set():
            time.sleep(0.005)
        return
        yield  # pragma: no cover  (make this a generator)


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
