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
from typing import ClassVar, NamedTuple


EXPECTED_SCHEMA_VERSION = 6

PERF_LOG_HDR_MAGIC = 0x4D56  # 'M','V' little-endian
PERF_CMD_HDR_MAGIC = 0x4D43  # 'M','C' little-endian — host→device commands

PERF_CMD_SET_TYPE_MASK = 0x01
PERF_CMD_SNAPSHOT = 0x02
PERF_CMD_SET_OVERLAY = 0x03
PERF_CMD_REGION_STREAM = 0x04
PERF_CMD_CANVAS_DUMP = 0x05

# Overlay sinks for the per-fret target rings (PERF_CMD_SET_OVERLAY flags).
# STRIP draws rings onto the viewer's SENSING strip copy; PANEL is reserved for
# a future LVDS demo overlay (no-op on the device today).
PERF_OVERLAY_STRIP = 0x01
PERF_OVERLAY_PANEL = 0x02

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
    REGION = 3  # host-selected sub-region, one strip per frame
    CANVAS = 4  # one-shot Legato canvas (UI framebuffer) dump, banded like SNAPSHOT
    # The detector's two band strips come in per-highway pairs — the kind says
    # which geometry cv_marvin_v1 was reading (1p centered vs 2p left/robot).
    SENSING_2P = 5
    STRIKE_2P = 6
    SCORE_2P_LEFT = 7   # 2-player left amp scoreboard  (region slot 1)
    SCORE_2P_RIGHT = 8  # 2-player right amp scoreboard (region slot 2)


# ui_manager CANVAS_* ids, for PERF_CMD_CANVAS_DUMP. Each is a separate surface:
# overlays (drawer, dialogs, keyboard) are NOT part of the base view's buffer, so
# pick the canvas that holds the pixels you want. Mirrors firmware ui_manager.h.
CANVAS_IDS = {
    "dash": 0,
    "navigation": 1,
    "songsel": 2,
    "album_art": 3,
    "wiimotes": 4,
    "keyboard": 5,
    "bus": 6,
}


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
    """One parsed 18-byte fretboard data frame (firmware/fretboard).

    `adc` is a 5-tuple of 12-bit unsigned ADC samples (0–4095, lower means
    a brighter sensor) ordered by `fret_t`: green, red, yellow, blue, orange.

    `fb_sample_seq`, `applied_mask`, and `commanded_mask` are stamped by the
    fretboard itself and carried on the wire (schema v6+): the sequence counter
    (one per fretboard tick, monotonic) reconstructs true sample order and
    exposes frames dropped in transit, `applied_mask` is the 7-bit actuator
    bitmask this node drove during the scan (its own model, or 0 when disarmed),
    and `commanded_mask` is marvin's CV teacher command latched during the scan
    (0 when marvin isn't teaching) — the atomic edge-ai training label, paired
    with the ADC at the source, not joined across marvin's TX/RX clocks.

    At ~240 Hz fretboard rate against 60 Hz video, the same `frame_epoch`
    tags ~4 consecutive records — `fb_sample_seq` (or `hdr.ts_counter`)
    gives sub-frame ordering.
    """

    hdr: Header
    adc: tuple[int, ...]   # length FRET_COUNT, indexed by fret_t
    fb_sample_seq: int
    applied_mask: int
    commanded_mask: int

    # 5×u16 adc + u32 seq + u8 applied_mask + u8 commanded_mask
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


_CMD_SET_OVERLAY_FMT = struct.Struct("<HBBI")  # magic, cmd_id, reserved, flags


def encode_set_overlay_payload(flags: int) -> bytes:
    """Pack a SET_OVERLAY command payload (no SOF/LEN/FCS framing)."""
    return _CMD_SET_OVERLAY_FMT.pack(
        PERF_CMD_HDR_MAGIC, PERF_CMD_SET_OVERLAY, 0, flags & 0xFFFFFFFF
    )


# Default region for the score-block capture (x, y, w, h) in the 720x480 frame,
# matching gameplay's SCORE_BLOCK_ROI. Fits both training and career scoring blocks.
DEFAULT_REGION_RECT = (114, 309, 96, 105)


class RegionSlot(NamedTuple):
    """One device-side region-stream slot (mirrors perf_cmd_region_stream_t)."""

    slot: int
    id: str                          # CLI/API name
    label: str                       # UI label
    kind: StripKind                  # the strip kind this slot emits
    rect: tuple[int, int, int, int]  # default (x, y, w, h)
    prefix: str                      # PNG filename prefix for exports


# The device has PERF_REGION_SLOTS (3) independent slots, each with its own
# enable + rect and a fixed strip kind. Slot 0 is the generic/original one,
# defaulting to the 1-player scoring block; slots 1-2 are the two 2-player amp
# scoreboards, whose rects mirror gameplay's AMP2P_BLOCK. Rects are host-supplied,
# so any slot can be repointed without a firmware rebuild.
REGION_SLOTS: tuple[RegionSlot, ...] = (
    RegionSlot(0, "score", "SCORE", StripKind.REGION, DEFAULT_REGION_RECT, "score"),
    RegionSlot(1, "score-2p-left", "2P SC L", StripKind.SCORE_2P_LEFT,
               (128, 164, 68, 78), "score-2pL"),
    RegionSlot(2, "score-2p-right", "2P SC R", StripKind.SCORE_2P_RIGHT,
               (515, 164, 68, 78), "score-2pR"),
)

REGION_SLOT_BY_ID = {s.id: s for s in REGION_SLOTS}
REGION_SLOT_BY_NUM = {s.slot: s for s in REGION_SLOTS}

# magic, cmd_id, reserved, enable, slot, x, y, w, h  (mirrors perf_cmd_region_stream_t)
_CMD_REGION_STREAM_FMT = struct.Struct("<HBBBBHHHH")


def encode_region_stream_payload(
    enable: bool, x: int = 0, y: int = 0, w: int = 0, h: int = 0, slot: int = 0
) -> bytes:
    """Pack a REGION_STREAM command payload (no SOF/LEN/FCS framing).

    enable=True starts streaming the (x, y, w, h) sub-region as one strip per
    frame; enable=False stops that slot (rect ignored). `slot` selects which of
    the device's independent slots to drive, and fixes the strip kind it emits
    (see REGION_SLOTS). The device ignores an out-of-range slot.
    """
    return _CMD_REGION_STREAM_FMT.pack(
        PERF_CMD_HDR_MAGIC, PERF_CMD_REGION_STREAM, 0, 1 if enable else 0,
        slot & 0xFF, x, y, w, h
    )


# magic, cmd_id, reserved, canvas, reserved, x, y, w, h  (mirrors perf_cmd_canvas_dump_t)
_CMD_CANVAS_DUMP_FMT = struct.Struct("<HBBBBHHHH")


def encode_canvas_dump_payload(
    canvas: int, x: int = 0, y: int = 0, w: int = 0, h: int = 0
) -> bytes:
    """Pack a CANVAS_DUMP command payload (no SOF/LEN/FCS framing).

    One-shot dump of a Legato canvas surface, returned as CANVAS strips with LAST
    set on the final band. The rect is clipped to the surface and w/h = 0 means
    "to the edge", so the default dumps the whole surface.
    """
    return _CMD_CANVAS_DUMP_FMT.pack(
        PERF_CMD_HDR_MAGIC, PERF_CMD_CANVAS_DUMP, 0, canvas & 0xFF, 0, x, y, w, h
    )
