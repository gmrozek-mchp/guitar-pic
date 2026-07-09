"""score-capture: REGION_STREAM command encode + REGION strip → PNG + out resolver."""

from __future__ import annotations

import struct
from pathlib import Path

import pytest

from marvin_perf.cli import SCORE_BLOCK_RECT, _parse_rect, _score_dir_start
from marvin_perf.decode import decode_record
from marvin_perf.records import (
    PERF_CMD_HDR_MAGIC,
    PERF_CMD_REGION_STREAM,
    STRIP_BPP,
    Strip,
    StripKind,
    encode_region_stream_payload,
)
from marvin_perf.snapshot import CompletedSnapshot, save_region_strips, save_snapshot

from .conftest import build_strip_payload

# Mirrors perf_cmd_region_stream_t: magic,cmd,reserved,enable,reserved,x,y,w,h
_REGION_FMT = struct.Struct("<HBBBBHHHH")


def test_region_stream_payload_start_round_trip() -> None:
    payload = encode_region_stream_payload(True, 114, 309, 96, 105)
    assert len(payload) == _REGION_FMT.size == 14  # packed struct, no padding
    magic, cmd, _r0, enable, _r1, x, y, w, h = _REGION_FMT.unpack(payload)
    assert magic == PERF_CMD_HDR_MAGIC
    assert cmd == PERF_CMD_REGION_STREAM
    assert (enable, x, y, w, h) == (1, 114, 309, 96, 105)


def test_region_stream_payload_stop() -> None:
    magic, cmd, _r0, enable, _r1, x, y, w, h = _REGION_FMT.unpack(
        encode_region_stream_payload(False)
    )
    assert cmd == PERF_CMD_REGION_STREAM
    assert enable == 0


def test_parse_rect_default_and_explicit() -> None:
    assert _parse_rect(None) == SCORE_BLOCK_RECT
    assert _parse_rect("10,20,30,40") == (10, 20, 30, 40)
    with pytest.raises(ValueError):
        _parse_rect("1,2,3")


def test_score_dir_autoincrement(tmp_path: Path) -> None:
    d = tmp_path / "scores"
    out, start = _score_dir_start(str(d))
    assert out == d and start == 1
    (d / "score-0001.png").write_bytes(b"")
    (d / "score-0004.png").write_bytes(b"")
    _out, start = _score_dir_start(str(d))
    assert start == 5  # max(existing) + 1


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
