from __future__ import annotations

import shutil
from pathlib import Path

import numpy as np

from gameplay.evaluate import score_eval, score_monotonic_eval
from gameplay.metadata import SCORE_BLOCK_ROI, score_from_filename
from gameplay.score import (
    build_score_catalog,
    calibrate_score,
    load_chrome_mask,
    read_score,
)


def test_score_from_filename():
    assert score_from_filename("score__training__010584__cap0121.png") == ("training", 10584)
    assert score_from_filename("score__training__000250__cap0001.png") == ("training", 250)
    assert score_from_filename("song_select__04_rock_and_roll_all_nite.png") is None
    assert score_from_filename("in_song__training.png") is None


def test_corpus_covers_all_digits(score_corpus):
    # The curated frames must expose every glyph 0-9 to build exemplars.
    cat = build_score_catalog(score_corpus)
    assert {t.digit for t in cat.templates} == set(range(10))


def test_segmentation_digit_count(score_corpus):
    # Gap-based segmentation must find exactly len(str(value)) digits per frame.
    cat = build_score_catalog(score_corpus)
    calib = calibrate_score([s.image for s in score_corpus], cat)
    for s in score_corpus:
        value = score_from_filename(s.path.name)[1]
        r = read_score(s.image, cat, calib)
        assert len(r.digits) == len(str(value)), (s.path.name, r.digits)


def test_read_score_clean_exact(score_corpus):
    # Register once on the session, then every frame reads exactly (search-free).
    cat = build_score_catalog(score_corpus)
    calib = calibrate_score([s.image for s in score_corpus], cat)
    for s in score_corpus:
        value = score_from_filename(s.path.name)[1]
        r = read_score(s.image, cat, calib)
        assert r.value == value, (s.path.name, r.digits)


def test_chrome_mask_shape(score_corpus):
    x0, y0, x1, y1 = SCORE_BLOCK_ROI
    mask = load_chrome_mask()
    assert mask.shape == (y1 - y0, x1 - x0)
    assert mask.dtype == bool
    frac = mask.mean()
    assert 0.1 < frac < 0.6, frac


def test_registration_locks_at_zero(score_corpus):
    # The corpus is one rig at a fixed position -> registration is the nominal block.
    cat = build_score_catalog(score_corpus)
    calib = calibrate_score([s.image for s in score_corpus], cat)
    assert (calib.dx, calib.dy) == (0, 0)


def test_score_eval_clean_and_slop(score_corpus):
    res = score_eval(score_corpus)
    assert res.n_total == len(score_corpus)
    assert res.exact_acc == 1.0, res.failures     # clean exact match on the corpus
    assert res.digit_acc == 1.0                    # every digit classified correctly
    assert res.loo_exact_acc == 1.0                # generalizes (leave-one-out)
    assert res.a2d_acc >= 0.99                      # gain/offset/noise slop robustness
    assert res.margin_min > 0


def test_monotonic_eval_clean(score_corpus, tmp_path: Path):
    # A value-sorted sequence of real frames must read non-decreasing (0 violations).
    ordered = sorted(score_corpus, key=lambda s: score_from_filename(s.path.name)[1])
    for i, s in enumerate(ordered):
        shutil.copy(s.path, tmp_path / f"f{i:04d}.png")
    res = score_monotonic_eval(tmp_path, samples=score_corpus)
    assert res.n_frames == len(ordered)
    assert res.n_violations == 0, res.violations
    assert res.first == score_from_filename(ordered[0].path.name)[1]
    assert res.last == score_from_filename(ordered[-1].path.name)[1]
