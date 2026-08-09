"""Integer ink-coverage primitives shared by the digit readers.

All coverage math is integer, so the firmware C reproduces it bit-for-bit (float
would differ by rounding mode + float32/64 between host and device). Luma keeps
the raw B*29+G*150+R*77 sum (no /256 — the divide cancels in the relative ink
threshold). The ink threshold is a rational num/den. Grid edges and the per-cell
coverage use one integer round-half-up rule mirrored in gameplay_score.c.

Used by `score.py` (1-player proportional font, ink-segmented) and `amp2p.py`
(2-player amp LED font, fixed grid).
"""

from __future__ import annotations

import numpy as np

LUMA_WI = np.array([29, 150, 77], dtype=np.int64)  # BGR weights; luma has no /256


def luma_i(patch: np.ndarray) -> np.ndarray:
    return patch.astype(np.int64) @ LUMA_WI


def edge(i: int, extent: int, n: int) -> int:
    """round(i*extent/n) half-up, integer (matches gameplay_score.c gp_edge)."""
    return (2 * i * extent + n) // (2 * n)


def ink_mask(
    band: np.ndarray, num: int, den: int, range_from: np.ndarray | None = None
) -> np.ndarray:
    """Ink mask at a relative threshold `num/den` of the min..max luma range.

    Relative (not absolute) is what makes the coverage gain/offset robust, so no
    per-frame normalization is needed upstream. `range_from` takes the range from a
    different array than the one being masked — for callers whose band is wider than
    the region the threshold should be derived from.
    """
    src = band if range_from is None else range_from
    lo, hi = int(src.min()), int(src.max())
    return den * (band - lo) > num * (hi - lo)


def cov_grid(m: np.ndarray, rows: int, cols: int) -> np.ndarray:
    """Resize a cropped bool ink mask to a rows×cols uint8 coverage grid (0-255).

    Each cell = round(ink_fraction * 255) via `(count*255 + npx//2)//npx` — the
    same integer form gameplay_score.c uses.
    """
    if m.size == 0:
        return np.zeros(rows * cols, dtype=np.uint8)
    rh, rw = m.shape
    mi = m.astype(np.int64)
    g = np.empty(rows * cols, dtype=np.uint8)
    for r in range(rows):
        ya = edge(r, rh, rows)
        yb = min(rh, max(ya + 1, edge(r + 1, rh, rows)))  # clamp past the bbox → empty
        for c in range(cols):
            xa = edge(c, rw, cols)
            xb = min(rw, max(xa + 1, edge(c + 1, rw, cols)))
            blk = mi[ya:yb, xa:xb]
            npx = blk.size
            g[r * cols + c] = ((int(blk.sum()) * 255 + npx // 2) // npx) if npx else 0
    return g
