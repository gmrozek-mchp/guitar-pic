from __future__ import annotations

import numpy as np

from gameplay.evaluate import score_eval
from gameplay.metadata import N_SCORE_DIGITS, SCORE_BLOCK_ROI, score_from_filename
from gameplay.score import (
    BLANK,
    _labels_for_value,
    build_score_catalog,
    calibrate_score,
    load_chrome_mask,
    read_score,
)


def test_score_from_filename():
    assert score_from_filename("score__training__010584__snap0121.png") == ("training", 10584)
    assert score_from_filename("score__training__001138__snap0097.png") == ("training", 1138)
    assert score_from_filename("song_select__04_rock_and_roll_all_nite.png") is None
    assert score_from_filename("in_song__training.png") is None


def test_labels_right_justify():
    assert _labels_for_value(1138, 6) == [BLANK, BLANK, 1, 1, 3, 8]
    assert _labels_for_value(17728, 6) == [BLANK, 1, 7, 7, 2, 8]
    # A full-width value fills every cell.
    assert _labels_for_value(123456, 6) == [1, 2, 3, 4, 5, 6]


def test_corpus_covers_all_digits(score_corpus):
    # The handful of frames must expose every glyph 0-9 to build templates.
    cat = build_score_catalog(score_corpus)
    digits = {t.digit for t in cat.templates}
    assert set(range(10)) <= digits
    assert BLANK in digits  # leading cells of the shorter scores


def test_read_score_clean_exact(score_corpus):
    # Register once on the session, then every frame reads exactly (search-free).
    cat = build_score_catalog(score_corpus)
    calib = calibrate_score([s.image for s in score_corpus], cat)
    for s in score_corpus:
        value = score_from_filename(s.path.name)[1]
        r = read_score(s.image, cat, calib)
        assert r.value == value, (s.path.name, r.digits)
        assert len(r.digits) == N_SCORE_DIGITS


def test_chrome_mask_shape(score_corpus):
    x0, y0, x1, y1 = SCORE_BLOCK_ROI
    mask = load_chrome_mask()
    assert mask.shape == (y1 - y0, x1 - x0)
    assert mask.dtype == bool
    # A meaningful chunk of the block is chrome, but not all of it (digits/interior excluded).
    frac = mask.mean()
    assert 0.1 < frac < 0.6, frac


def test_registration_locks_at_zero(score_corpus):
    # The corpus is one rig at a fixed position -> registration is the nominal block.
    cat = build_score_catalog(score_corpus)
    calib = calibrate_score([s.image for s in score_corpus], cat)
    assert (calib.dx, calib.dy) == (0, 0)


def test_registration_recovers_shift(score_corpus):
    # Roll a frame by a known offset; registration should detect it and still read right.
    cat = build_score_catalog(score_corpus)
    s = score_corpus[0]
    value = score_from_filename(s.path.name)[1]
    for ax, ay in [(3, 2), (-4, 3), (5, -3)]:
        shifted = np.roll(np.roll(s.image, ay, axis=0), ax, axis=1)
        calib = calibrate_score([shifted], cat)
        assert (calib.dx, calib.dy) == (ax, ay)
        assert read_score(shifted, cat, calib).value == value


def test_score_eval_clean_and_slop(score_corpus):
    res = score_eval(score_corpus)
    assert res.n_total == len(score_corpus)
    assert res.exact_acc == 1.0, res.failures       # clean exact match on the handful
    assert res.digit_acc == 1.0                      # every cell classified correctly
    assert res.a2d_acc == 1.0                        # gain/offset/noise cancelled per-frame
    assert res.reg_acc == 1.0                        # auto-registration recovers position shifts
    assert res.margin_min > 0
