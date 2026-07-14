"""2-player amp scoreboard location (per-side chrome registration).

Two-player GH3 shows two amp scoreboards near the top of the frame instead of the
single bottom-left block the score/multiplier/streak readers use. This module
locates each amp independently so the (later) 2-player digit/multiplier/streak
readers can sample fixed offsets inside a registered block — the register-once
design of the single-player score block (`score.py`), but per side and at the
2-player scale.

The amp is static on screen (fixed rig), so one reference frame builds the chrome
reference and registration only absorbs minor per-rig offset. The fiducial is the
static dark panel texture, marked by a hand-painted magenta mask
(`amp2p_<side>_mask.png`); the digit strip and medallion interior are excluded
because they change with the score / multiplier / portrait. Matching is masked,
per-frame-normalized SAD over an integer offset search (mirrors `calibrate_score`).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .corpus import load_bgr, score_corpus_dir
from .metadata import AMP2P_BLOCK

SIDES = ("left", "right")

_LUMA_W = np.array([29, 150, 77], dtype=np.float64)  # BGR weights (matches score.py)


def _mask_path(side: str):
    return score_corpus_dir() / f"amp2p_{side}_mask.png"


def _ref_path(side: str):
    return score_corpus_dir() / f"amp2p_{side}_ref.png"


def load_amp2p_mask(side: str) -> np.ndarray:
    """Load the hand-painted registration mask as a (block_h, block_w) bool array.

    Magenta pixels mark the static chrome used for registration. The PNG may be at
    any integer upscale of the block; it is box-averaged down to block resolution.
    """
    x0, y0, x1, y1 = AMP2P_BLOCK[side]
    bw, bh = x1 - x0, y1 - y0
    a = load_bgr(_mask_path(side))  # magenta is channel-symmetric, so BGR/RGB moot
    mag = (a[..., 0] > 200) & (a[..., 2] > 200) & (a[..., 1] < 150)
    H, W = mag.shape
    if H % bh or W % bw:
        raise ValueError(f"amp2p {side} mask {W}x{H} is not an integer multiple of block {bw}x{bh}")
    fy, fx = H // bh, W // bw
    return mag.reshape(bh, fy, bw, fx).mean(axis=(1, 3)) >= 0.5


def _block_luma(image: np.ndarray, side: str, dx: int = 0, dy: int = 0) -> np.ndarray:
    """Float luma of the side's block at its nominal position shifted by (dx, dy)."""
    x0, y0, x1, y1 = AMP2P_BLOCK[side]
    return image[y0 + dy:y1 + dy, x0 + dx:x1 + dx].astype(np.float64) @ _LUMA_W


def _norm_masked(a: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Zero-mean/unit-std over the masked pixels (cancels A2D gain/offset)."""
    m = a[mask]
    return (a - m.mean()) / (m.std() + 1e-6)


@dataclass(frozen=True)
class AmpReference:
    side: str
    ref: np.ndarray   # (block_h, block_w) reference luma
    mask: np.ndarray  # (block_h, block_w) bool: static-chrome registration pixels


@dataclass(frozen=True)
class AmpCalibration:
    side: str
    dx: int = 0
    dy: int = 0


def build_reference(side: str, images: list[np.ndarray] | None = None) -> AmpReference:
    """Build a side's chrome reference (mask + reference luma).

    With no `images`, the reference luma is the committed clean crop
    (`amp2p_<side>_ref.png`, block-sized). Given full frames, it is their median
    block luma at the nominal position (the masked panel is static across frames,
    so the median cleans up the unmasked digit/portrait content).
    """
    mask = load_amp2p_mask(side)
    if images:
        ref = np.median(np.stack([_block_luma(im, side) for im in images]), axis=0)
    else:
        crop = load_bgr(_ref_path(side)).astype(np.float64) @ _LUMA_W
        ref = crop
    return AmpReference(side=side, ref=ref, mask=mask)


def calibrate(
    side: str,
    images: list[np.ndarray],
    reference: AmpReference,
    search: int = 8,
) -> AmpCalibration:
    """Lock the side's block offset for this rig by matching the static chrome.

    Over an integer offset search, sum the masked per-frame-normalized SAD between
    each frame's block and the reference across all frames; the `(dx, dy)` with the
    smallest total wins. Run once at gameplay start.
    """
    ref_n = _norm_masked(reference.ref, reference.mask)
    best: tuple[float, int, int] | None = None
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            total = 0.0
            for img in images:
                tn = _norm_masked(_block_luma(img, side, dx, dy), reference.mask)
                total += float(np.abs((tn - ref_n)[reference.mask]).sum())
            if best is None or total < best[0]:
                best = (total, dx, dy)
    assert best is not None
    return AmpCalibration(side=side, dx=best[1], dy=best[2])
