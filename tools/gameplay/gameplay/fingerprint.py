"""Fixed-region colour fingerprint (the Q10 recognizer primitive).

A frame is partitioned into a coarse `cols x rows` grid of fixed rectangles; each
region collapses to its mean (B,G,R). The fingerprint is the concatenated uint8
vector, length `3 * cols * rows`. Coarse averaging is what makes this tolerant of
the positional slop introduced by the Wii's analog-component -> HDMI path: a
sub-cell shift just moves a few pixels between neighbours and washes out.

Two knobs matter for the firmware port (memory-bound on uncached DDR reads — see
the plan's hardware-cost section):

- `samples_per_region`: None computes a dense mean (touches every pixel ~= a
  full-frame scan, ~7-20 ms on the SAM9X75). An integer N samples an N x N lattice
  per region instead (~cols*rows*N*N reads total, sub-millisecond), trading a little
  estimate noise for speed. The coarse average absorbs that noise.
- `normalize`: when True, the vector is standardized to a fixed mean/std per frame,
  which cancels the global gain/offset value-slop of the analog path. Raw vs
  normalized is chosen empirically in `evaluate.py`.

Everything is shaped to port 1:1 to integer C: accumulate pixel sums over fixed
rectangles (or fixed sample coords), divide, then an L1 compare downstream.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from PIL import Image

CANONICAL_W = 720
CANONICAL_H = 480
BPP = 3  # BGR

# Standardization targets for the normalized variant (uint8 midpoint-ish).
_NORM_MEAN = 128.0
_NORM_STD = 48.0


# Defaults chosen empirically in evaluate.py against the corpus: 12x8 normalized
# subsampled gives 96% LOO (= 100% of multi-sample classes) and ~99.8% slop
# robustness at ~0.1-0.4 ms on the SAM9X75. Normalization is what buys value-slop
# (gain/offset) robustness from the analog-component path.
@dataclass(frozen=True)
class FingerprintConfig:
    cols: int = 12
    rows: int = 8
    samples_per_region: int | None = 5  # None = dense; N = N x N lattice
    normalize: bool = True

    @property
    def length(self) -> int:
        return self.cols * self.rows * BPP

    @property
    def pixel_reads(self) -> int:
        """Pixels touched per classification — the on-device cost driver."""
        if self.samples_per_region is None:
            return CANONICAL_W * CANONICAL_H
        return self.cols * self.rows * self.samples_per_region**2


DEFAULT_CONFIG = FingerprintConfig()

# Regions whose content varies for reasons unrelated to *which screen* this is, as
# (x0, y0, x1, y1). A cell at least half inside one is dropped from the fingerprint:
# excluded from the normalization statistics and emitted as 0.
#
# The end screens (practice_end_menu / faceoff_end_menu) put the song's magazine
# cover here, and it changes per song. Measured on hardware: an unseen song's cover
# moved a real practice_end frame to within 156 of the *other* end screen's centroid
# (t_margin 645), so the margin gate rejected a screen the absolute gate had
# comfortably accepted at 0.30x t_abs -- the run then hung waiting for a decisive
# name. Both corpus end classes happen to share one cover, so what separates their
# centroids is mostly the right page, and a changed cover swamps it.
#
# Dropping the cells from the *normalization* is the load-bearing half. Normalizing
# over the whole frame and then ignoring these cells still leaks: a bright cover
# shifts the frame's mean/std and therefore moves every other cell too. Measured on
# the same frame: zero-after-normalize gives a margin of 343, excluding them from the
# statistics gives 940.
# Stated on 12x8 grid-cell boundaries so "excluded region" and "dropped cells" are
# the same thing. Off-boundary rects leave straddling cells partly over the art,
# which leaks exactly what the mask exists to remove.
EXCLUDED_REGIONS: tuple[tuple[int, int, int, int], ...] = (
    (120, 60, 360, 420),   # left magazine page: per-song cover art, title, artist
)


def cell_keep_mask(config: FingerprintConfig = DEFAULT_CONFIG,
                   min_overlap: float = 0.5) -> np.ndarray:
    """(rows, cols) bool: which grid cells contribute to the fingerprint."""
    ys = _region_bounds(config.rows, CANONICAL_H)
    xs = _region_bounds(config.cols, CANONICAL_W)
    keep = np.ones((config.rows, config.cols), dtype=bool)
    for r, (y0, y1) in enumerate(ys):
        for c, (x0, x1) in enumerate(xs):
            area = (y1 - y0) * (x1 - x0)
            for ex0, ey0, ex1, ey1 in EXCLUDED_REGIONS:
                ox = max(0, min(x1, ex1) - max(x0, ex0))
                oy = max(0, min(y1, ey1) - max(y0, ey0))
                if area and ox * oy >= min_overlap * area:
                    keep[r, c] = False
    return keep


def slot_keep_mask(config: FingerprintConfig = DEFAULT_CONFIG) -> np.ndarray:
    """Per-element (length,) bool mask — the cell mask expanded over B,G,R."""
    return np.repeat(cell_keep_mask(config).reshape(-1), BPP)


def _to_canonical(image: np.ndarray) -> np.ndarray:
    """Ensure a uint8 HxWx3 array at the canonical 720x480 (resize if needed)."""
    if image.dtype != np.uint8:
        image = image.astype(np.uint8)
    if image.shape[:2] == (CANONICAL_H, CANONICAL_W):
        return image
    # Defensive only — real captures are already native 720x480. Resize via PIL
    # (BGR in, BGR out; channel order is irrelevant to a resize).
    im = Image.fromarray(image, mode="RGB")
    im = im.resize((CANONICAL_W, CANONICAL_H), Image.BILINEAR)
    return np.asarray(im, dtype=np.uint8)


def _region_bounds(n: int, extent: int) -> list[tuple[int, int]]:
    """Split [0, extent) into n contiguous integer spans."""
    edges = np.linspace(0, extent, n + 1).round().astype(int)
    return [(int(edges[i]), int(edges[i + 1])) for i in range(n)]


def _lattice(lo: int, hi: int, n: int) -> np.ndarray:
    """n sample coordinates inside [lo, hi), centred in their sub-cells."""
    fracs = (np.arange(n) + 0.5) / n
    return (lo + fracs * (hi - lo)).astype(int).clip(lo, hi - 1)


def fingerprint(image: np.ndarray, config: FingerprintConfig = DEFAULT_CONFIG) -> np.ndarray:
    """Compute the fixed-region fingerprint of a BGR frame.

    Returns a uint8 vector of length `config.length`, region-major (row 0 first),
    each region contributing [B, G, R]. Cells inside EXCLUDED_REGIONS are 0 and take
    no part in the normalization; the vector keeps its full length so centroids, the
    L1 compare and the exported tables all keep one shape (a zero on both sides
    contributes nothing to the distance).
    """
    img = _to_canonical(image)
    ys = _region_bounds(config.rows, CANONICAL_H)
    xs = _region_bounds(config.cols, CANONICAL_W)

    means = np.empty((config.rows, config.cols, BPP), dtype=np.float64)
    for r, (y0, y1) in enumerate(ys):
        for c, (x0, x1) in enumerate(xs):
            if config.samples_per_region is None:
                patch = img[y0:y1, x0:x1, :]
            else:
                yy = _lattice(y0, y1, config.samples_per_region)
                xx = _lattice(x0, x1, config.samples_per_region)
                patch = img[np.ix_(yy, xx)]
            means[r, c] = patch.reshape(-1, BPP).mean(axis=0)

    vec = means.reshape(-1)
    keep = slot_keep_mask(config)
    if config.normalize:
        kept = vec[keep]
        std = kept.std()
        if std > 1e-6:
            vec = (vec - kept.mean()) / std * _NORM_STD + _NORM_MEAN
        else:
            vec = np.full_like(vec, _NORM_MEAN)
    out = vec.round().clip(0, 255).astype(np.uint8)
    out[~keep] = 0
    return out
