"""Reassembly + save for PERF_CMD_SNAPSHOT band strips."""

from __future__ import annotations

from pathlib import Path

import pytest

from marvin_perf.decode import decode_record
from marvin_perf.records import STRIP_BPP, STRIP_FLAG_LAST, Strip, StripKind
from marvin_perf.snapshot import (
    CompletedSnapshot,
    SnapshotAssembler,
    SnapshotError,
    save_snapshot,
)

from .conftest import build_strip_payload


def _band(y: int, h: int, *, w: int = 4, fill: int, last: bool = False) -> Strip:
    rec = decode_record(
        build_strip_payload(
            frame_epoch=7,
            kind=int(StripKind.SNAPSHOT),
            x=0,
            y=y,
            w=w,
            h=h,
            flags=STRIP_FLAG_LAST if last else 0,
            fill=fill,
        )
    )
    assert isinstance(rec, Strip)
    return rec


def test_assembles_two_bands_in_order() -> None:
    asm = SnapshotAssembler()
    assert asm.add(_band(0, 3, fill=0x11)) is None
    snap = asm.add(_band(3, 2, fill=0x22, last=True))
    assert isinstance(snap, CompletedSnapshot)
    assert (snap.width, snap.height, snap.frame_epoch) == (4, 5, 7)
    assert len(snap.bgr) == 4 * 5 * STRIP_BPP == snap.n_bytes
    assert snap.bgr[: 4 * 3 * STRIP_BPP] == bytes([0x11]) * (4 * 3 * STRIP_BPP)
    assert snap.bgr[4 * 3 * STRIP_BPP :] == bytes([0x22]) * (4 * 2 * STRIP_BPP)


def test_waits_for_contiguity_when_last_arrives_first() -> None:
    asm = SnapshotAssembler()
    # LAST band first: not contiguous from y=0 yet, so no completion.
    assert asm.add(_band(3, 2, fill=0x22, last=True)) is None
    snap = asm.add(_band(0, 3, fill=0x11))
    assert isinstance(snap, CompletedSnapshot)
    assert snap.height == 5


def test_ignores_non_snapshot_strips() -> None:
    asm = SnapshotAssembler()
    rec = decode_record(build_strip_payload(kind=int(StripKind.SENSING)))
    assert isinstance(rec, Strip)
    assert asm.add(rec) is None


def test_width_mismatch_raises() -> None:
    asm = SnapshotAssembler()
    asm.add(_band(0, 1, w=4, fill=0x11))
    with pytest.raises(SnapshotError):
        asm.add(_band(1, 1, w=8, fill=0x22, last=True))


def test_save_writes_png_only_with_epoch(tmp_path: Path) -> None:
    snap = CompletedSnapshot(width=4, height=2, frame_epoch=7, bgr=bytes(4 * 2 * 3))
    written = save_snapshot(snap, tmp_path / "shot")  # bare stem → .png
    assert written == [tmp_path / "shot.png"]

    # No raw/sidecar files — PNG is the sole, lossless artifact.
    assert not (tmp_path / "shot.bgr").exists()
    assert not (tmp_path / "shot.json").exists()

    from PIL import Image

    with Image.open(tmp_path / "shot.png") as img:
        assert img.size == (4, 2)
        assert img.text.get("frame_epoch") == "7"  # epoch rides in a tEXt chunk


def test_save_png_is_lossless(tmp_path: Path) -> None:
    # A distinguishable BGR pattern must survive the round-trip exactly.
    bgr = bytes(range(4 * 2 * 3))
    snap = CompletedSnapshot(width=4, height=2, frame_epoch=1, bgr=bgr)
    save_snapshot(snap, tmp_path / "lossless.png")

    from PIL import Image

    with Image.open(tmp_path / "lossless.png") as img:
        rgb = img.convert("RGB").tobytes()
    # Source is BGR; PNG holds RGB — rebuild the expected RGB and compare.
    expected = bytes(
        b for i in range(0, len(bgr), 3) for b in (bgr[i + 2], bgr[i + 1], bgr[i])
    )
    assert rgb == expected
