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
    # Origin of the assembled region within its source. Always (0, 0) for a video
    # snapshot; a canvas dump of a sub-rect reports where in the surface it came from.
    x: int = 0
    y: int = 0

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

    `add()` ignores strips of other kinds and returns None until a region's LAST
    band has arrived and every row from the origin down is covered, at which point
    it returns the assembled image (and forgets that frame_epoch).

    Bands carry x/y in *source* coordinates, so a dump of a sub-rect starts at a
    non-zero origin. The origin is passed in rather than inferred from the bands:
    inferring it from the smallest y seen so far would complete a region early if
    its LAST band arrived before an earlier one. All bands must sit at `origin_x`
    and share a width.

    `kind` selects the strip kind to assemble — SNAPSHOT (video frame) by default,
    CANVAS for a UI framebuffer dump.
    """

    def __init__(
        self,
        kind: StripKind = StripKind.SNAPSHOT,
        origin_x: int = 0,
        origin_y: int = 0,
    ) -> None:
        self._kind = int(kind)
        self._x0 = origin_x
        self._y0 = origin_y
        self._pending: dict[int, _Pending] = {}

    def add(self, strip: Strip) -> CompletedSnapshot | None:
        if strip.kind != self._kind:
            return None
        if strip.x != self._x0:
            raise SnapshotError(f"band at x={strip.x}, expected {self._x0}")

        epoch = strip.hdr.frame_epoch
        p = self._pending.get(epoch)
        if p is None:
            p = _Pending(width=strip.w)
            self._pending[epoch] = p
        elif strip.w != p.width:
            raise SnapshotError(
                f"band width {strip.w} != {p.width} (frame_epoch {epoch})"
            )

        p.bands[strip.y] = strip
        if strip.is_last:
            p.last_y = strip.y

        if p.last_y is None:
            return None
        return self._try_finalize(epoch, p)

    def _try_finalize(self, epoch: int, p: _Pending) -> CompletedSnapshot | None:
        # Walk contiguous bands from the origin; bail (keep waiting) on any gap.
        canvas = bytearray()
        y = self._y0
        bottom = self._y0
        while y in p.bands:
            band = p.bands[y]
            canvas += band.bgr
            bottom = y + band.h
            y += band.h
        if p.last_y is None or bottom <= p.last_y:
            return None  # not yet contiguous through the LAST band

        del self._pending[epoch]
        return CompletedSnapshot(
            width=p.width,
            height=bottom - self._y0,
            frame_epoch=epoch,
            bgr=bytes(canvas),
            x=self._x0,
            y=self._y0,
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

    Numbering is zero-padded to 5 digits so a lexicographic sort matches frame order
    for captures up to 99999 frames; past that, consumers must sort on the number
    (gameplay's `evaluate.capture_frames` does).
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
        written.extend(save_snapshot(snap, out / f"{prefix}-{n:05d}.png"))
    return written
