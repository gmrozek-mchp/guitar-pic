"""Schema mirror of firmware/marvin/default/src/perf_log/perf_log_records.h.

Hand-mirror — bump EXPECTED_SCHEMA_VERSION here whenever
PERF_LOG_SCHEMA_VERSION is bumped on the firmware side, and keep every
struct.Struct format string in lockstep with the C __attribute__((packed))
layouts. All multi-byte fields are little-endian.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import ClassVar


EXPECTED_SCHEMA_VERSION = 1

PERF_LOG_HDR_MAGIC = 0x4D56  # 'M','V' little-endian

SOF_BYTES = bytes((0x55, 0x4D, 0x52, 0x56))  # "UMRV"

FRET_COUNT = 5

PATCH_DIM = 5
PATCH_PIXELS = PATCH_DIM * PATCH_DIM
PATCH_BYTES = PATCH_PIXELS * 3  # 75 B per BGR patch


class RecordType(IntEnum):
    SESSION = 0x01
    STAMP = 0x02
    DETECTOR = 0x03
    TIMING = 0x04
    PATCH = 0x05
    DROP = 0x06
    TASK_HIGHWATER = 0x07


class Stage(IntEnum):
    ISC_IRQ = 0x10
    VIDEO_PUBLISH = 0x11
    CV_START = 0x20
    CV_END = 0x21
    TP_TICK = 0x30
    FBL_SEND = 0x40
    CDC_WRITE_COMPLETE = 0x41


class TaskId(IntEnum):
    VIDEO = 0
    CV_MARVIN_V1 = 1
    DETECTOR_DRAIN = 2
    TIMING = 3
    FRETBOARD_LINK = 4
    PERF_DRAIN = 5


class PerfFlag(IntEnum):
    FROM_ISR = 0x01


# ─── Common header ────────────────────────────────────────────────────────────

_HDR_FMT = struct.Struct("<HBBIQ")
HDR_SIZE = _HDR_FMT.size  # 16


@dataclass(frozen=True)
class Header:
    magic: int
    type: int
    flags: int
    frame_epoch: int
    ts_counter: int

    @classmethod
    def unpack(cls, buf: bytes | memoryview) -> "Header":
        return cls(*_HDR_FMT.unpack_from(buf, 0))


# ─── Record dataclasses ───────────────────────────────────────────────────────


@dataclass(frozen=True)
class Session:
    hdr: Header
    timer_freq_hz: int
    schema_version: int
    fw_git_short: int

    SIZE: ClassVar[int] = HDR_SIZE + 16
    _BODY: ClassVar[struct.Struct] = struct.Struct("<IHHII")


@dataclass(frozen=True)
class Stamp:
    hdr: Header
    stage_id: int  # Stage value, kept as int for forward-compat
    aux: int

    SIZE: ClassVar[int] = HDR_SIZE + 16
    _BODY: ClassVar[struct.Struct] = struct.Struct("<B3sIII")


@dataclass(frozen=True)
class Detector:
    hdr: Header
    hold_dist: tuple[int, int, int, int, int]
    edge_dist: tuple[int, int, int, int, int]
    pressed_mask: int
    edge_active_mask: int

    SIZE: ClassVar[int] = HDR_SIZE + 24
    _BODY: ClassVar[struct.Struct] = struct.Struct("<5H5HBBH")


@dataclass(frozen=True)
class Timing:
    hdr: Header
    publish_mask: int
    chord_window_fill: int
    fifo_depth: int
    strum_dir: int

    SIZE: ClassVar[int] = HDR_SIZE + 16
    _BODY: ClassVar[struct.Struct] = struct.Struct("<BBBBIII")


@dataclass(frozen=True)
class Drop:
    hdr: Header
    dropped_state: int
    dropped_patch: int
    dropped_sink: int

    SIZE: ClassVar[int] = HDR_SIZE + 16
    _BODY: ClassVar[struct.Struct] = struct.Struct("<IIII")


@dataclass(frozen=True)
class TaskHighwater:
    hdr: Header
    task_id: int  # TaskId value, kept as int for forward-compat
    words: int

    SIZE: ClassVar[int] = HDR_SIZE + 8
    _BODY: ClassVar[struct.Struct] = struct.Struct("<B3sI")


# Patch is a fixed 814 B record carrying 5 fret entries of 158 B each. The 5x5
# BGR pixel payloads are intentionally stored as the raw bytes-slice (not
# decoded into ndarrays) — v0 only needs counts; the v1 web UI materializes
# pixels on demand.

_PATCH_FRET_FMT = struct.Struct(f"<HHHH{PATCH_BYTES}s{PATCH_BYTES}s")
PATCH_FRET_SIZE = _PATCH_FRET_FMT.size  # 158


@dataclass(frozen=True)
class PatchFret:
    hx: int
    hy: int
    ex: int
    ey: int
    hold_bgr: bytes
    edge_bgr: bytes


@dataclass(frozen=True)
class Patch:
    hdr: Header
    frame_w: int
    frame_h: int
    fret: tuple[PatchFret, PatchFret, PatchFret, PatchFret, PatchFret]

    SIZE: ClassVar[int] = HDR_SIZE + 8 + FRET_COUNT * PATCH_FRET_SIZE  # 814
    _PRELUDE: ClassVar[struct.Struct] = struct.Struct("<HHI")


@dataclass(frozen=True)
class UnknownRecord:
    hdr: Header
    raw: bytes


# Largest record bytes — used by framing.py to bound LEN sanity check.
MAX_RECORD_BYTES = Patch.SIZE  # 814
