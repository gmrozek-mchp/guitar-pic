"""score-capture: REGION_STREAM command encode + REGION strip → PNG + out resolver."""

from __future__ import annotations

import struct
from pathlib import Path

import pytest

from marvin_perf.cli import SCORE_BLOCK_RECT, _EXPORT_KINDS, _parse_rect, _score_dir_start
from marvin_perf.decode import decode_record
from marvin_perf.records import (
    PERF_CMD_HDR_MAGIC,
    PERF_CMD_REGION_STREAM,
    REGION_SLOTS,
    REGION_SLOT_BY_ID,
    STRIP_BPP,
    Strip,
    StripKind,
    encode_region_stream_payload,
)
from marvin_perf.snapshot import CompletedSnapshot, save_region_strips, save_snapshot

from .conftest import build_strip_payload

# Mirrors perf_cmd_region_stream_t: magic,cmd,reserved,enable,slot,x,y,w,h
_REGION_FMT = struct.Struct("<HBBBBHHHH")


def test_region_stream_payload_start_round_trip() -> None:
    payload = encode_region_stream_payload(True, 114, 309, 96, 105)
    assert len(payload) == _REGION_FMT.size == 14  # packed struct, no padding
    magic, cmd, _r0, enable, slot, x, y, w, h = _REGION_FMT.unpack(payload)
    assert magic == PERF_CMD_HDR_MAGIC
    assert cmd == PERF_CMD_REGION_STREAM
    assert (enable, slot, x, y, w, h) == (1, 0, 114, 309, 96, 105)


def test_region_stream_payload_stop() -> None:
    magic, cmd, _r0, enable, slot, x, y, w, h = _REGION_FMT.unpack(
        encode_region_stream_payload(False)
    )
    assert cmd == PERF_CMD_REGION_STREAM
    assert enable == 0
    assert slot == 0  # slot 0 is the pre-slot-table default


def test_region_stream_payload_slot_in_reserved_byte() -> None:
    """The slot rides the byte that used to be reserved, so the size is unchanged."""
    payload = encode_region_stream_payload(True, 513, 172, 68, 78, slot=2)
    assert len(payload) == 14
    _magic, _cmd, _r0, enable, slot, x, y, w, h = _REGION_FMT.unpack(payload)
    assert (enable, slot, x, y, w, h) == (1, 2, 513, 172, 68, 78)
    # Stopping a non-zero slot must carry the slot too, or slot 0 would stop.
    _m, _c, _r, enable, slot, *_rect = _REGION_FMT.unpack(
        encode_region_stream_payload(False, slot=2)
    )
    assert (enable, slot) == (0, 2)


def test_region_slots_table_matches_device_layout() -> None:
    """Three slots, contiguous from 0, each with a distinct kind and prefix."""
    assert [s.slot for s in REGION_SLOTS] == [0, 1, 2]
    assert REGION_SLOT_BY_ID["score"].kind is StripKind.REGION
    assert REGION_SLOT_BY_ID["score-2p-left"].kind is StripKind.SCORE_2P_LEFT
    assert REGION_SLOT_BY_ID["score-2p-right"].kind is StripKind.SCORE_2P_RIGHT
    assert len({s.kind for s in REGION_SLOTS}) == 3
    assert len({s.prefix for s in REGION_SLOTS}) == 3
    # The two 2-player amp blocks are the same size, mirrored across the frame.
    left, right = REGION_SLOT_BY_ID["score-2p-left"], REGION_SLOT_BY_ID["score-2p-right"]
    assert left.rect[2:] == right.rect[2:]
    assert left.rect[0] < right.rect[0] and left.rect[1] == right.rect[1]


def test_parse_rect_default_and_explicit() -> None:
    assert _parse_rect(None) == SCORE_BLOCK_RECT
    assert _parse_rect("10,20,30,40") == (10, 20, 30, 40)
    with pytest.raises(ValueError):
        _parse_rect("1,2,3")


def test_parse_rect_defaults_to_slot_rect() -> None:
    slot = REGION_SLOT_BY_ID["score-2p-right"]
    assert _parse_rect(None, default=slot.rect) == (513, 172, 68, 78)


def test_score_dir_autoincrement(tmp_path: Path) -> None:
    d = tmp_path / "scores"
    out, start = _score_dir_start(str(d))
    assert out == d and start == 1
    (d / "score-0001.png").write_bytes(b"")
    (d / "score-0004.png").write_bytes(b"")
    _out, start = _score_dir_start(str(d))
    assert start == 5  # max(existing) + 1


def test_score_dir_autoincrement_is_per_prefix(tmp_path: Path) -> None:
    """Two slots writing into one directory must not share a counter."""
    d = tmp_path / "scores"
    _out, start = _score_dir_start(str(d), "score-2pL")
    assert start == 1
    (d / "score-0001.png").write_bytes(b"")
    (d / "score-0002.png").write_bytes(b"")
    (d / "score-2pL-0007.png").write_bytes(b"")
    assert _score_dir_start(str(d), "score-2pL")[1] == 8
    assert _score_dir_start(str(d), "score")[1] == 3


def test_region_strip_saves_as_png(tmp_path: Path) -> None:
    # One REGION strip is a complete region frame — no reassembly.
    x, y, w, h = SCORE_BLOCK_RECT
    rec = decode_record(
        build_strip_payload(
            frame_epoch=42, kind=int(StripKind.REGION), x=x, y=y, w=w, h=h, fill=0xAB
        )
    )
    assert isinstance(rec, Strip)
    assert rec.kind == int(StripKind.REGION)
    assert (rec.x, rec.y, rec.w, rec.h) == (x, y, w, h)
    assert len(rec.bgr) == w * h * STRIP_BPP

    snap = CompletedSnapshot(width=rec.w, height=rec.h, frame_epoch=rec.hdr.frame_epoch, bgr=rec.bgr)
    out = save_snapshot(snap, tmp_path / "score-0001.png")[0]

    from PIL import Image

    with Image.open(out) as im:
        assert im.size == (w, h)
        assert im.getpixel((0, 0)) == (0xAB, 0xAB, 0xAB)
        assert im.text["frame_epoch"] == "42"


def test_save_region_strips_extracts_only_region(tmp_path: Path) -> None:
    x, y, w, h = SCORE_BLOCK_RECT

    def strip(kind: int, epoch: int, fill: int):
        rec = decode_record(
            build_strip_payload(frame_epoch=epoch, kind=kind, x=x, y=y, w=w, h=h, fill=fill)
        )
        assert isinstance(rec, Strip)
        return rec

    records = [
        strip(int(StripKind.SENSING), 1, 0x10),   # ignored (wrong kind)
        strip(int(StripKind.REGION), 2, 0x20),
        strip(int(StripKind.SNAPSHOT), 3, 0x30),   # ignored
        strip(int(StripKind.REGION), 4, 0x40),
    ]
    written = save_region_strips(records, tmp_path / "scores")
    names = sorted(p.name for p in written)
    assert names == ["score-0001.png", "score-0002.png"]  # only the 2 REGION strips

    from PIL import Image

    with Image.open(tmp_path / "scores" / "score-0002.png") as im:
        assert im.size == (w, h)
        assert im.getpixel((0, 0)) == (0x40, 0x40, 0x40)


def test_export_kinds_separate_the_two_2p_scoreboards(tmp_path: Path) -> None:
    """A 2-player capture's left and right amp blocks extract independently."""
    left = REGION_SLOT_BY_ID["score-2p-left"]
    right = REGION_SLOT_BY_ID["score-2p-right"]

    def strip(kind: StripKind, rect, epoch: int, fill: int):
        x, y, w, h = rect
        rec = decode_record(
            build_strip_payload(
                frame_epoch=epoch, kind=int(kind), x=x, y=y, w=w, h=h, fill=fill
            )
        )
        assert isinstance(rec, Strip)
        return rec

    records = [
        strip(left.kind, left.rect, 1, 0x11),
        strip(right.kind, right.rect, 1, 0x22),
        strip(left.kind, left.rect, 2, 0x33),
        strip(StripKind.SENSING_2P, (150, 300, 155, 32), 2, 0x44),
    ]

    for slot, expect in ((left, 2), (right, 1)):
        kind, prefix = _EXPORT_KINDS[slot.id]
        written = save_region_strips(records, tmp_path / slot.id, kind=kind, prefix=prefix)
        assert [p.name for p in written] == [
            f"{prefix}-{i:04d}.png" for i in range(1, expect + 1)
        ]

    kind, prefix = _EXPORT_KINDS["sensing-2p"]
    written = save_region_strips(records, tmp_path / "bands", kind=kind, prefix=prefix)
    assert [p.name for p in written] == ["sensing-2p-0001.png"]
