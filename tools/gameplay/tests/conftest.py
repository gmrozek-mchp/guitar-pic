"""Shared fixtures: the loaded corpus (session-scoped — PNG decode is the cost)."""

from __future__ import annotations

import pytest

import numpy as np

from gameplay import amp2p
from gameplay.metadata import AMP2P_BAND_H, AMP2P_BAND_Y0
from gameplay.corpus import (
    load_amp2p_corpus,
    load_corpus,
    load_score_corpus,
    load_streak_corpus,
)


@pytest.fixture(scope="session")
def corpus():
    return load_corpus()


@pytest.fixture(scope="session")
def amp2p_corpus():
    return load_amp2p_corpus()


@pytest.fixture(scope="session")
def score_corpus():
    return load_score_corpus()


@pytest.fixture(scope="session")
def streak_corpus():
    return load_streak_corpus()


def _relay_six_digits(image, side, value, layout):
    """Re-lay a labelled 5-digit block as a 6-digit strip at `layout`.

    Returns `(block, expected_value)`. The 6th digit is a copy of the value's own
    leading glyph, so the cells are real LED renderings at real brightness — only
    their *positions* are synthetic, which is exactly the unmeasured part. The
    container is cleared by tiling its own leftmost column, so the panel behind the
    digits stays real too.
    """
    block = image.copy()
    rows = slice(AMP2P_BAND_Y0, AMP2P_BAND_Y0 + AMP2P_BAND_H)
    src = amp2p.cell_bounds(side, 5)
    dst = amp2p.cell_bounds(side, 6, layout)
    cx0, cx1 = amp2p.container_span(side)
    block[rows, cx0:cx1] = np.repeat(block[rows, cx0:cx0 + 1], cx1 - cx0, axis=1)
    w = layout[0]
    for k, (sx0, sx1) in enumerate([src[0]] + src):
        glyph = image[rows, sx0:sx0 + min(w, sx1 - sx0)]
        block[rows, dst[k][0]:dst[k][0] + glyph.shape[1]] = glyph
    return block, int(str(value)[0] + str(value))


@pytest.fixture(scope="session")
def relay_six():
    """Build a synthetic 6-digit amp strip from a labelled 5-digit frame.

    Shared because both the host reader test and the firmware cross-check need the
    same synthetic: the 6-digit pitch has never been captured, so this is the only
    way to exercise that path, and the two must exercise it identically.
    """
    return _relay_six_digits
