"""Frame builders shared across tests.

These produce the same bytes the firmware sink would write — they're the host
side of the contract documented in perf_log_sink_cdc.c:234-245. When real
firmware-emitted captures become available (post-producer-wiring), drop them
into tests/fixtures/ as `.bin` files and add a parametrized test asserting the
captured bytes round-trip identically. Until then, building the fixture in
code with the host CRC keeps the algorithm under test.
"""

from __future__ import annotations

import struct

from marvin_perf.framing import crc16_ccitt_false
from marvin_perf.records import (
    Detector,
    Drop,
    HDR_SIZE,
    PERF_LOG_HDR_MAGIC,
    RecordType,
    Session,
    SOF_BYTES,
    STRIP_BPP,
    STRIP_HDR_BYTES,
    Stamp,
    Stage,
    Strip,
    StripKind,
    TaskHighwater,
    TaskRuntime,
    Timing,
    _STRIP_BODY,
)


# ─── Header builder ──────────────────────────────────────────────────────────


def build_header(
    record_type: RecordType,
    *,
    flags: int = 0,
    frame_epoch: int = 0,
    ts_counter: int = 0,
) -> bytes:
    return struct.pack(
        "<HBBIQ",
        PERF_LOG_HDR_MAGIC,
        int(record_type),
        flags,
        frame_epoch,
        ts_counter,
    )


# ─── Per-type payload builders ───────────────────────────────────────────────


def build_session_payload(
    *, timer_freq_hz: int = 266_000_000, schema_version: int = 2, fw_git_short: int = 0
) -> bytes:
    body = Session._BODY.pack(timer_freq_hz, schema_version, 0, fw_git_short, 0)
    return build_header(RecordType.SESSION) + body


def build_stamp_payload(
    *, stage: Stage, frame_epoch: int = 0, ts_counter: int = 0, aux: int = 0
) -> bytes:
    body = Stamp._BODY.pack(int(stage), b"\x00\x00\x00", aux, 0, 0)
    return build_header(
        RecordType.STAMP, frame_epoch=frame_epoch, ts_counter=ts_counter
    ) + body


def build_detector_payload(
    *,
    frame_epoch: int = 1,
    hold_dist: tuple[int, ...] = (10, 20, 30, 40, 50),
    edge_dist: tuple[int, ...] = (1, 2, 3, 4, 5),
    pressed_mask: int = 0x03,
    edge_active_mask: int = 0x05,
) -> bytes:
    body = Detector._BODY.pack(*hold_dist, *edge_dist, pressed_mask, edge_active_mask, 0)
    return build_header(RecordType.DETECTOR, frame_epoch=frame_epoch) + body


def build_timing_payload(
    *,
    frame_epoch: int = 1,
    publish_mask: int = 0x01,
    chord_window_fill: int = 1,
    fifo_depth: int = 2,
    strum_dir: int = 1,
) -> bytes:
    body = Timing._BODY.pack(
        publish_mask, chord_window_fill, fifo_depth, strum_dir, 0, 0, 0
    )
    return build_header(RecordType.TIMING, frame_epoch=frame_epoch) + body


def build_drop_payload(
    *, dropped_state: int = 0, dropped_strip: int = 0, dropped_sink: int = 0
) -> bytes:
    body = Drop._BODY.pack(dropped_state, dropped_strip, dropped_sink, 0)
    return build_header(RecordType.DROP) + body


def build_task_highwater_payload(*, task_id: int, words: int) -> bytes:
    body = TaskHighwater._BODY.pack(task_id, b"\x00\x00\x00", words)
    return build_header(RecordType.TASK_HIGHWATER) + body


def build_task_runtime_payload(
    *, task_id: int, state: int, priority: int, run_time_counter: int = 0
) -> bytes:
    body = TaskRuntime._BODY.pack(task_id, state, priority, 0, run_time_counter, 0)
    return build_header(RecordType.TASK_RUNTIME) + body


def build_strip_payload(
    *,
    frame_epoch: int = 1,
    kind: int = int(StripKind.SENSING),
    x: int = 240,
    y: int = 295,
    w: int = 4,
    h: int = 2,
    fill: int = 0x55,
) -> bytes:
    body = _STRIP_BODY.pack(x, y, w, h, kind, b"\x00\x00\x00")
    bgr = bytes((fill,) * (w * h * STRIP_BPP))
    return build_header(RecordType.STRIP, frame_epoch=frame_epoch) + body + bgr


# ─── Frame wrapper (SOF + LEN + payload + CRC) ───────────────────────────────


def wrap_frame(payload: bytes, *, corrupt_crc: bool = False) -> bytes:
    length = len(payload)
    length_bytes = struct.pack("<H", length)
    crc = crc16_ccitt_false(length_bytes + payload)
    if corrupt_crc:
        crc ^= 0xFFFF
    return SOF_BYTES + length_bytes + payload + struct.pack("<H", crc)
