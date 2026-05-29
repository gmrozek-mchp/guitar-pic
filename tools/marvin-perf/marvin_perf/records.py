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


EXPECTED_SCHEMA_VERSION = 2

PERF_LOG_HDR_MAGIC = 0x4D56  # 'M','V' little-endian
PERF_CMD_HDR_MAGIC = 0x4D43  # 'M','C' little-endian — host→device commands

PERF_CMD_SET_TYPE_MASK = 0x01

SOF_BYTES = bytes((0x55, 0x4D, 0x52, 0x56))  # "UMRV"

FRET_COUNT = 5

# Strip is the variable-size BGR888 region-of-interest record. The wire
# format treats (kind, x, y, w, h) as fully variable per record so future
# kinds can carry different rectangles without a schema bump.
#
# STRIP_MAX_BYTES is a sanity bound on decoded pixel data — must mirror
# firmware perf_log_records.h:PERF_STRIP_MAX_BYTES exactly. The firmware
# value lands just under the u16 wire-LEN cap (65535 bytes − record
# header overhead). To raise it, the LEN field needs to grow to u32 —
# wire format break, both ends must update together.
STRIP_BPP = 3
STRIP_MAX_BYTES = 65000  # mirrors firmware PERF_STRIP_MAX_BYTES


class RecordType(IntEnum):
    SESSION = 0x01
    STAMP = 0x02
    DETECTOR = 0x03
    TIMING = 0x04
    STRIP = 0x05
    DROP = 0x06
    TASK_HIGHWATER = 0x07
    TASK_RUNTIME = 0x08


class StripKind(IntEnum):
    SENSING = 0
    STRIKE = 1


class TaskState(IntEnum):
    RUNNING = 0
    READY = 1
    BLOCKED = 2
    SUSPENDED = 3
    DELETED = 4
    INVALID = 5


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
    dropped_strip: int
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


# Strip — variable length: HDR + 12 B body (x, y, w, h, kind, reserved[3]) +
# w*h*3 BGR888 bytes. Today's producers crop 240×32 → 23040 B pixel payload,
# 23068 B total record. Decoder validates len(bgr) == w*h*3 against the on-
# wire dimensions, not against a compile-time constant — future kinds may
# use other rectangles within STRIP_MAX_BYTES.

_STRIP_BODY = struct.Struct("<HHHHB3s")
STRIP_HDR_BYTES = HDR_SIZE + _STRIP_BODY.size  # 28


@dataclass(frozen=True)
class Strip:
    hdr: Header
    x: int
    y: int
    w: int
    h: int
    kind: int  # StripKind value, kept as int for forward-compat
    bgr: bytes

    @property
    def kind_name(self) -> str:
        try:
            return StripKind(self.kind).name.lower()
        except ValueError:
            return f"kind_{self.kind}"


@dataclass(frozen=True)
class TaskRuntime:
    hdr: Header
    task_id: int  # TaskId value, kept as int for forward-compat
    state: int    # TaskState value
    priority: int
    run_time_counter: int

    SIZE: ClassVar[int] = HDR_SIZE + 12
    _BODY: ClassVar[struct.Struct] = struct.Struct("<BBBBII")


@dataclass(frozen=True)
class UnknownRecord:
    hdr: Header
    raw: bytes


# Largest record bytes — used by framing.py to bound LEN sanity check.
MAX_RECORD_BYTES = STRIP_HDR_BYTES + STRIP_MAX_BYTES  # 23068


# ─── Host→device commands ────────────────────────────────────────────────────

# Bit n of the type-mask is RecordType value n. Bit 0 is unused.
RECORD_TYPE_BY_NAME: dict[str, int] = {rt.name: int(rt) for rt in RecordType}

# Common preset masks used by the CLI.
TYPE_MASK_ALL = 0xFFFFFFFF
TYPE_MASK_MIN = (1 << RecordType.SESSION) | (1 << RecordType.DROP)


_CMD_SET_MASK_FMT = struct.Struct("<HBBI")  # magic, cmd_id, reserved, mask


def encode_set_mask_payload(mask: int) -> bytes:
    """Pack a SET_TYPE_MASK command payload (no SOF/LEN/FCS framing)."""
    return _CMD_SET_MASK_FMT.pack(
        PERF_CMD_HDR_MAGIC, PERF_CMD_SET_TYPE_MASK, 0, mask & 0xFFFFFFFF
    )
