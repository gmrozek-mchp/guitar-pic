from __future__ import annotations

import numpy as np

from gameplay.classifier import build_templates, classify_fp, classify_image
from gameplay.evaluate import labelled_fps, loo_eval, recommend_thresholds
from gameplay.fingerprint import FingerprintConfig, fingerprint
from gameplay.screens import UNKNOWN

CONFIG = FingerprintConfig()  # 12x8, samples=5, normalized


def test_loo_accuracy_bar(corpus):
    rec = recommend_thresholds(corpus, CONFIG)
    res = loo_eval(corpus, CONFIG, t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin)
    # Single-sample classes can't be LOO-matched (their centroid is removed), so
    # 97/101 = 96% is the ceiling; every multi-sample class should be perfect.
    assert res.accuracy >= 0.93, res.per_class_acc


def test_thresholds_accept_in_class(corpus):
    rec = recommend_thresholds(corpus, CONFIG)
    # t_abs must accept every legitimate in-class match (with headroom)...
    assert rec.rec_t_abs >= rec.in_class_dist_max
    # ...while still rejecting at least some genuinely-unseen (held-out) screens.
    assert rec.impostor_leak < rec.n_impostors, rec


def test_held_out_class_is_unknown(corpus):
    # `loading` is visually distinct (tattoo art / beige), so with its template
    # removed a loading frame should reject as UNKNOWN rather than leak to a peer.
    rec = recommend_thresholds(corpus, CONFIG)
    kept = labelled_fps([s for s in corpus if s.screen_id != "loading"], CONFIG)
    templates = build_templates(kept, CONFIG)
    loading = next(s for s in corpus if s.screen_id == "loading")
    result = classify_fp(
        fingerprint(loading.image, CONFIG), templates,
        t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin,
    )
    assert result.screen_id == UNKNOWN, result


def test_resubstitution_is_correct(corpus):
    # Trained on everything, every sample classifies to its own class.
    rec = recommend_thresholds(corpus, CONFIG)
    templates = build_templates(labelled_fps(corpus, CONFIG), CONFIG)
    wrong = [
        s.path.name
        for s in corpus
        if classify_image(s.image, templates, t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin).screen_id
        != s.screen_id
    ]
    assert not wrong, wrong
