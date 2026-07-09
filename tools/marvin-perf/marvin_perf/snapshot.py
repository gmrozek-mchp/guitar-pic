"""Reassemble PERF_CMD_SNAPSHOT band strips into a full frame and save it.

The device answers a snapshot command with a top-to-bottom run of full-width
`STRIP` records (kind SNAPSHOT), all sharing one `frame_epoch`; the final band
sets `STRIP_FLAG_LAST`. This module groups bands by `frame_epoch`, pastes them
onto a canvas, and writes the result as a lossless `.png` (frame_epoch in a
tEXt chunk).
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

from .records import STRIP_BPP, Strip, StripKind


@dataclass(frozen=True)
class CompletedSnapshot:
    width: int
    height: int
    frame_epoch: int
    bgr: bytes  # width * height * 3, row-major, top-to-bottom

    @property
    def n_bytes(self) -> int:
        return self.width * self.height * STRIP_BPP


class SnapshotError(Exception):
    pass


@dataclass
class _Pending:
    width: int
    bands: dict[int, Strip] = field(default_factory=dict)  # keyed by band y
    last_y: int | None = None  # y of the band flagged LAST


class SnapshotAssembler:
    """Feed decoded `Strip` records; get a `CompletedSnapshot` when done.

    `add()` ignores non-SNAPSHOT strips and returns None until a snapshot's
    LAST band has arrived and every row from 0..height is covered, at which
    point it returns the assembled frame (and forgets that frame_epoch).
    """

    def __init__(self) -> None:
        self._pending: dict[int, _Pending] = {}

    def add(self, strip: Strip) -> CompletedSnapshot | None:
        if strip.kind != int(StripKind.SNAPSHOT):
            return None
        if strip.x != 0:
            raise SnapshotError(f"snapshot band at x={strip.x}, expected full-width")

        epoch = strip.hdr.frame_epoch
        p = self._pending.get(epoch)
        if p is None:
            p = _Pending(width=strip.w)
            self._pending[epoch] = p
        elif strip.w != p.width:
            raise SnapshotError(
                f"snapshot band width {strip.w} != {p.width} (frame_epoch {epoch})"
            )

        p.bands[strip.y] = strip
        if strip.is_last:
            p.last_y = strip.y

        if p.last_y is None:
            return None
        return self._try_finalize(epoch, p)

    def _try_finalize(self, epoch: int, p: _Pending) -> CompletedSnapshot | None:
        # Walk contiguous bands from y=0; bail (keep waiting) on any gap.
        canvas = bytearray()
        y = 0
        height = 0
        while y in p.bands:
            band = p.bands[y]
            canvas += band.bgr
            height = y + band.h
            y += band.h
        if p.last_y is None or height <= p.last_y:
            return None  # not yet contiguous through the LAST band

        del self._pending[epoch]
        return CompletedSnapshot(
            width=p.width, height=height, frame_epoch=epoch, bgr=bytes(canvas)
        )


def save_snapshot(snap: CompletedSnapshot, out: str | Path) -> list[Path]:
    """Write the snapshot as a lossless PNG and return its path.

    `out` may carry a .png suffix (or a stale .bgr/.json, stripped) or be a bare
    stem; the result is always `<stem>.png`. PNG is lossless for the packed RGB
    pixels, so no separate raw buffer is needed; `frame_epoch` rides along in a
    tEXt chunk rather than a sidecar JSON.
    """
    base = Path(out)
    if base.suffix.lower() in (".bgr", ".png", ".json"):
        base = base.with_suffix("")
    base.parent.mkdir(parents=True, exist_ok=True)

    try:
        from PIL import Image, PngImagePlugin
    except ImportError as e:  # pragma: no cover - Pillow is a base dependency
        raise SnapshotError("Pillow is required to save snapshots as PNG") from e

    # Pillow's "raw" decoder maps BGR → RGB without a manual per-pixel swap.
    img = Image.frombytes("RGB", (snap.width, snap.height), snap.bgr, "raw", "BGR")
    meta = PngImagePlugin.PngInfo()
    meta.add_text("frame_epoch", str(snap.frame_epoch))

    png_path = base.with_suffix(".png")
    img.save(png_path, pnginfo=meta)
    return [png_path]


def save_region_strips(
    records: Iterable[object],
    out_dir: str | Path,
    *,
    kind: StripKind = StripKind.REGION,
    prefix: str = "score",
) -> list[Path]:
    """Save each REGION (or given-kind) strip in `records` as a numbered PNG.

    Each such strip is one complete region frame (single record), so it maps
    straight to a `CompletedSnapshot` and reuses `save_snapshot`. Returns the
    written paths, ordered as encountered.
    """
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for rec in records:
        if not isinstance(rec, Strip) or rec.kind != int(kind):
            continue
        snap = CompletedSnapshot(
            width=rec.w, height=rec.h, frame_epoch=rec.hdr.frame_epoch, bgr=rec.bgr
        )
        n = len(written) + 1
        written.extend(save_snapshot(snap, out / f"{prefix}-{n:04d}.png"))
    return written
