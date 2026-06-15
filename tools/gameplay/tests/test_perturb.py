from __future__ import annotations

import numpy as np

from gameplay import perturb
from gameplay.evaluate import recommend_thresholds, robustness_eval
from gameplay.fingerprint import CANONICAL_H, CANONICAL_W, FingerprintConfig

CONFIG = FingerprintConfig()


def _img(seed=0):
    rng = np.random.default_rng(seed)
    return rng.integers(0, 256, size=(CANONICAL_H, CANONICAL_W, 3), dtype=np.uint8)


def test_transforms_preserve_shape_and_dtype():
    img = _img()
    rng = np.random.default_rng(0)
    outs = [
        perturb.adjust_gain(img, 1.2),
        perturb.adjust_offset(img, -15),
        perturb.add_noise(img, 10.0, rng),
        perturb.translate(img, 5, -3),
        perturb.rescale(img, 1.05),
        perturb.rescale(img, 0.95),
    ]
    for o in outs:
        assert o.shape == img.shape
        assert o.dtype == np.uint8


def test_envelope_is_deterministic_given_seed():
    img = _img()
    names_a = [(c, n, o.sum()) for c, n, o in perturb.envelope(img, np.random.default_rng(7))]
    names_b = [(c, n, o.sum()) for c, n, o in perturb.envelope(img, np.random.default_rng(7))]
    assert names_a == names_b


def test_robustness_bar(corpus):
    rec = recommend_thresholds(corpus, CONFIG)
    res = robustness_eval(corpus, CONFIG, t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin)
    assert res.stability >= 0.85, res.per_category
