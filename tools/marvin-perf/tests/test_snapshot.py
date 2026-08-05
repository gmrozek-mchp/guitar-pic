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


# ── canvas dumps (PERF_CMD_CANVAS_DUMP) ──────────────────────────────────────
# Same banding as a video snapshot, but kind CANVAS and, for a sub-rect request,
# a non-zero origin the assembler is told about rather than inferring.


def _canvas_band(
    y: int, h: int, *, x: int = 0, w: int = 4, fill: int, last: bool = False
) -> Strip:
    rec = decode_record(
        build_strip_payload(
            frame_epoch=0,
            kind=int(StripKind.CANVAS),
            x=x,
            y=y,
            w=w,
            h=h,
            flags=STRIP_FLAG_LAST if last else 0,
            fill=fill,
        )
    )
    assert isinstance(rec, Strip)
    return rec


def test_canvas_assembler_ignores_video_snapshot_bands() -> None:
    asm = SnapshotAssembler(kind=StripKind.CANVAS)
    # A SNAPSHOT band must not satisfy a canvas dump, even flagged LAST.
    assert asm.add(_band(0, 2, fill=0x11, last=True)) is None


def test_snapshot_assembler_ignores_canvas_bands() -> None:
    asm = SnapshotAssembler()
    assert asm.add(_canvas_band(0, 2, fill=0x11, last=True)) is None


def test_canvas_full_surface_assembles() -> None:
    asm = SnapshotAssembler(kind=StripKind.CANVAS)
    assert asm.add(_canvas_band(0, 2, fill=0x11)) is None
    snap = asm.add(_canvas_band(2, 1, fill=0x22, last=True))
    assert isinstance(snap, CompletedSnapshot)
    assert (snap.width, snap.height, snap.x, snap.y) == (4, 3, 0, 0)
    assert snap.bgr == bytes([0x11]) * (4 * 2 * STRIP_BPP) + bytes([0x22]) * (
        4 * 1 * STRIP_BPP
    )


def test_canvas_subrect_uses_declared_origin() -> None:
    # Bands carry absolute surface coords; the assembler is anchored at the rect.
    asm = SnapshotAssembler(kind=StripKind.CANVAS, origin_x=8, origin_y=100)
    assert asm.add(_canvas_band(100, 2, x=8, fill=0x33)) is None
    snap = asm.add(_canvas_band(102, 2, x=8, fill=0x44, last=True))
    assert isinstance(snap, CompletedSnapshot)
    assert (snap.width, snap.height, snap.x, snap.y) == (4, 4, 8, 100)


def test_canvas_subrect_last_band_first_waits_for_the_gap() -> None:
    # The origin is declared, so a LAST band arriving first cannot complete the
    # region while earlier rows are still missing.
    asm = SnapshotAssembler(kind=StripKind.CANVAS, origin_y=100)
    assert asm.add(_canvas_band(102, 2, fill=0x44, last=True)) is None
    snap = asm.add(_canvas_band(100, 2, fill=0x33))
    assert isinstance(snap, CompletedSnapshot)
    assert (snap.height, snap.y) == (4, 100)


def test_canvas_band_at_unexpected_x_raises() -> None:
    asm = SnapshotAssembler(kind=StripKind.CANVAS, origin_x=8)
    with pytest.raises(SnapshotError):
        asm.add(_canvas_band(0, 1, x=0, fill=0x55))
