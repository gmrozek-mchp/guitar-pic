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


EXPECTED_SCHEMA_VERSION = 5

PERF_LOG_HDR_MAGIC = 0x4D56  # 'M','V' little-endian
PERF_CMD_HDR_MAGIC = 0x4D43  # 'M','C' little-endian — host→device commands

PERF_CMD_SET_TYPE_MASK = 0x01
PERF_CMD_SNAPSHOT = 0x02

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
    DETECTOR_CONFIG = 0x09
    ACTUATOR = 0x0A
    FRETBOARD_RAW = 0x0B


class StripKind(IntEnum):
    SENSING = 0
    STRIKE = 1
    SNAPSHOT = 2


# Strip flags byte (Strip.flags). SNAPSHOT bands set LAST on the final
# (bottom) band so a reassembler knows the frame is complete.
STRIP_FLAG_LAST = 0x01


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
    FBL_READ_COMPLETE = 0x42


class TaskId(IntEnum):
    VIDEO = 0
    CV_MARVIN_V1 = 1
    DETECTOR_DRAIN = 2
    TIMING = 3
    FRETBOARD_LINK = 4
    PERF_DRAIN = 5
    IDLE = 6
    LEGATO = 7
    XLCDC = 8
    MAXTOUCH = 9
    SYS_INPUT = 10
    USB_DEVICE = 11
    USB_HOST = 12
    DRV_USB_UDPHS = 13
    DRV_USB_HOST = 14
    APP = 15
    OTHER = 16  # pseudo-slot — Σ all − Σ registered tasks (host CPU% closure)
    FRETBOARD_RX = 17


class ActuatorProducer(IntEnum):
    NONE = 0
    TIMING = 1
    MANUAL = 2


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
    """v3 per-frame snapshot of timing_pipeline state.

    Replaces the v2 counts-only record. Deadlines (`*_at_ms`) are absolute
    in the pipeline's clock (`now_ms`); for empty queues they are 0 and
    the corresponding `*_count` is 0. `chord_age_ms` is meaningful only
    when `chord_open == 1`.
    """

    hdr: Header
    now_ms: int
    chord_open: int
    chord_mask: int
    chord_age_ms: int
    note_q_count: int
    note_head_mask: int
    note_tail_mask: int
    note_head_at_ms: int
    strum_q_count: int
    strum_head_mask: int
    strum_dir_next: int
    strum_head_at_ms: int
    frets_active: int
    strum_active: int
    release_pending_mask: int
    publish_mask: int
    strum_release_at_ms: int
    release_min_at_ms: int

    # Body: now_ms + chord(open,mask,age) + note(count,head,tail,_pad,at) +
    #       strum(count,head,dir,_pad,at) + (frets,strum_active,rel_pend,publish) +
    #       (strum_rel_at, release_min_at)
    _BODY: ClassVar[struct.Struct] = struct.Struct(
        "<I BBH BBBBI BBBBI BBBB II"
    )
    SIZE: ClassVar[int] = HDR_SIZE + _BODY.size  # 16 + 35 = 51


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


# Strip — variable length: HDR + 12 B body (x, y, w, h, kind, flags,
# reserved[2]) + w*h*3 BGR888 bytes. SENSING/STRIKE producers crop 240×32
# → 23040 B pixel payload; SNAPSHOT bands run up to ~65 KB. The decoder
# validates len(bgr) == w*h*3 against the on-wire dimensions, not against a
# compile-time constant — future kinds may use other rectangles within
# STRIP_MAX_BYTES.

_STRIP_BODY = struct.Struct("<HHHHBB2s")
STRIP_HDR_BYTES = HDR_SIZE + _STRIP_BODY.size  # 28


@dataclass(frozen=True)
class Strip:
    hdr: Header
    x: int
    y: int
    w: int
    h: int
    kind: int  # StripKind value, kept as int for forward-compat
    flags: int  # PERF_STRIP_FLAG_* bitfield
    bgr: bytes

    @property
    def kind_name(self) -> str:
        try:
            return StripKind(self.kind).name.lower()
        except ValueError:
            return f"kind_{self.kind}"

    @property
    def is_last(self) -> bool:
        return bool(self.flags & STRIP_FLAG_LAST)


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
class DetectorConfig:
    """cv_marvin_v1 per-fret configuration. Static today; becomes runtime-
    tunable when M6 calibration UI lands. Floats are IEEE 754 little-endian
    on the wire — same representation Python's struct delivers.

    Sample coords are in capture-frame space, matching STRIP record (x, y)
    anchors so host overlay code computes strip-relative pixels as
    `sample_x - strip.x`, `sample_y - strip.y`.
    """

    hdr: Header
    sensor_hx: tuple[int, ...]   # length FRET_COUNT
    sensor_hy: tuple[int, ...]
    sensor_ex: tuple[int, ...]
    sensor_ey: tuple[int, ...]
    hold_thresh: float
    hold_release_frac: float
    edge_thresh: float
    color_target_b: tuple[float, ...]
    color_target_g: tuple[float, ...]
    color_target_r: tuple[float, ...]
    color_reject_b: tuple[float, ...]
    color_reject_g: tuple[float, ...]
    color_reject_r: tuple[float, ...]

    # 4 × (5×u16) + 3 × f32 + 6 × (5×f32) = 40 + 12 + 120 = 172 B body
    _BODY: ClassVar[struct.Struct] = struct.Struct(
        "<5H5H5H5H fff 5f5f5f 5f5f5f"
    )
    SIZE: ClassVar[int] = HDR_SIZE + _BODY.size  # 16 + 172 = 188


@dataclass(frozen=True)
class Actuator:
    """Emitted on every FretboardLink_Send call (one per producer intent).

    `producer_id` is the arbitration signal that's invisible from the wire
    byte alone — when manual_control takes the wire from timing_pipeline,
    nothing about the byte itself changes. `last_ack_*` snapshot the most
    recent CDC_WRITE_COMPLETE so host can compute Send-to-ack latency
    without joining FBL_SEND/CDC_WRITE_COMPLETE stamps.
    """

    hdr: Header
    intended_mask: int
    asserted_mask: int
    strum_dir: int        # 0=none, 1=down, 2=up
    producer_id: int      # ActuatorProducer value
    last_ack_result: int  # USB_HOST_CDC_RESULT_*, signed
    last_ack_ts_counter: int

    _BODY: ClassVar[struct.Struct] = struct.Struct("<BBBBiQ")
    SIZE: ClassVar[int] = HDR_SIZE + _BODY.size  # 16 + 16 = 32

    @property
    def producer_name(self) -> str:
        try:
            return ActuatorProducer(self.producer_id).name.lower()
        except ValueError:
            return f"producer_{self.producer_id}"


@dataclass(frozen=True)
class FretboardRaw:
    """One parsed 17-byte fretboard data frame (firmware/fretboard).

    `adc` is a 5-tuple of 12-bit unsigned ADC samples (0–4095, lower means
    a brighter sensor) ordered by `fret_t`: green, red, yellow, blue, orange.

    `fb_sample_seq` and `applied_mask` are stamped by the fretboard itself
    and carried on the wire (schema v4+): the sequence counter (one per
    fretboard tick, monotonic) reconstructs true sample order and exposes
    frames dropped in transit, and `applied_mask` is the 7-bit actuator
    bitmask the fretboard was driving during this scan — sensor and actuator
    paired atomically at the source, not joined across marvin's TX/RX clocks.

    At ~240 Hz fretboard rate against 60 Hz video, the same `frame_epoch`
    tags ~4 consecutive records — `fb_sample_seq` (or `hdr.ts_counter`)
    gives sub-frame ordering.
    """

    hdr: Header
    adc: tuple[int, ...]   # length FRET_COUNT, indexed by fret_t
    fb_sample_seq: int
    applied_mask: int

    # 5×u16 adc + u32 seq + u8 applied_mask + u8 reserved
    _BODY: ClassVar[struct.Struct] = struct.Struct("<5H I B B")
    SIZE: ClassVar[int] = HDR_SIZE + _BODY.size  # 16 + 16 = 32


@dataclass(frozen=True)
class UnknownRecord:
    hdr: Header
    raw: bytes


# Largest record bytes — used by framing.py to bound LEN sanity check.
MAX_RECORD_BYTES = STRIP_HDR_BYTES + STRIP_MAX_BYTES  # 65028


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


_CMD_HDR_FMT = struct.Struct("<HBB")  # magic, cmd_id, reserved


def encode_snapshot_payload() -> bytes:
    """Pack a SNAPSHOT command payload (header-only; no SOF/LEN/FCS framing)."""
    return _CMD_HDR_FMT.pack(PERF_CMD_HDR_MAGIC, PERF_CMD_SNAPSHOT, 0)
