from __future__ import annotations

import numpy as np

from gameplay.fingerprint import (
    CANONICAL_H,
    CANONICAL_W,
    EXCLUDED_REGIONS,
    FingerprintConfig,
    cell_keep_mask,
    fingerprint,
    slot_keep_mask,
)


def _solid(color_bgr) -> np.ndarray:
    img = np.empty((CANONICAL_H, CANONICAL_W, 3), dtype=np.uint8)
    img[:] = color_bgr
    return img


def test_length_matches_grid():
    for cfg in (FingerprintConfig(8, 6), FingerprintConfig(12, 8, samples_per_region=None)):
        fp = fingerprint(_solid((10, 20, 30)), cfg)
        assert fp.shape == (cfg.length,)
        assert fp.dtype == np.uint8


def test_solid_image_reproduces_color():
    # Raw (un-normalized) means of a solid frame equal the frame's colour — in the
    # contributing cells. Cells over an EXCLUDED_REGION are deliberately 0.
    cfg = FingerprintConfig(8, 6, normalize=False)
    fp = fingerprint(_solid((10, 20, 30)), cfg)
    keep = slot_keep_mask(cfg)
    assert np.all(fp[keep].reshape(-1, 3) == np.array([10, 20, 30]))
    assert np.all(fp[~keep] == 0)


def test_excluded_cells_are_zero_and_excluded_from_normalization():
    """A change confined to an excluded region must not move any kept cell.

    This is the property the end-screen fix rests on: the classifier normalizes
    per frame, so before the mask a bright per-song magazine cover shifted the
    frame's mean/std and moved *every* cell, not just the ones over the cover.
    """
    cfg = FingerprintConfig()
    keep = slot_keep_mask(cfg)
    base = _solid((60, 60, 60))
    bright = base.copy()
    for x0, y0, x1, y1 in EXCLUDED_REGIONS:
        bright[y0:y1, x0:x1] = 240

    a, b = fingerprint(base, cfg), fingerprint(bright, cfg)
    assert np.all(a[~keep] == 0) and np.all(b[~keep] == 0)
    assert np.array_equal(a[keep], b[keep]), "an excluded region leaked into kept cells"


def test_mask_covers_the_expected_cells():
    cfg = FingerprintConfig()
    keep = cell_keep_mask(cfg)
    dropped = int((~keep).sum())
    assert 0 < dropped < cfg.cols * cfg.rows // 2, "mask should be a minority of the frame"
    # The excluded rect is contiguous, so the dropped cells must be too.
    rows = {r for r in range(cfg.rows) if not keep[r].all()}
    assert rows == set(range(min(rows), max(rows) + 1))


def test_deterministic():
    rng = np.random.default_rng(0)
    img = rng.integers(0, 256, size=(CANONICAL_H, CANONICAL_W, 3), dtype=np.uint8)
    cfg = FingerprintConfig()
    assert np.array_equal(fingerprint(img, cfg), fingerprint(img, cfg))


def test_values_in_range():
    rng = np.random.default_rng(1)
    img = rng.integers(0, 256, size=(CANONICAL_H, CANONICAL_W, 3), dtype=np.uint8)
    fp = fingerprint(img, FingerprintConfig(normalize=True))
    assert fp.min() >= 0 and fp.max() <= 255


def test_resizes_off_canonical_input():
    img = np.full((240, 360, 3), (5, 6, 7), dtype=np.uint8)
    fp = fingerprint(img, FingerprintConfig(8, 6))
    assert fp.shape == (FingerprintConfig(8, 6).length,)


def test_pixel_reads_accounting():
    assert FingerprintConfig(8, 6, samples_per_region=5).pixel_reads == 8 * 6 * 25
    assert FingerprintConfig(samples_per_region=None).pixel_reads == CANONICAL_W * CANONICAL_H
