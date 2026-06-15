from __future__ import annotations

import numpy as np

from gameplay.fingerprint import CANONICAL_H, CANONICAL_W, FingerprintConfig, fingerprint


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
    # Raw (un-normalized) means of a solid frame equal the frame's colour.
    fp = fingerprint(_solid((10, 20, 30)), FingerprintConfig(8, 6, normalize=False))
    assert np.all(fp.reshape(-1, 3) == np.array([10, 20, 30]))


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
