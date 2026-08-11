"""Naming the end layout: both magazines, every selection state, and the leak."""

from __future__ import annotations

import numpy as np
import pytest

from gameplay import endlayout as el
from gameplay import endprobe as ep
from gameplay import perturb
from gameplay.corpus import load_corpus

END_SHIFT = 2


@pytest.fixture(scope="module")
def corpus():
    return [s for s in load_corpus() if s.image.shape[:2] == (el.CANON_H, el.CANON_W)]


def _shift(img, dx, dy):
    out = np.zeros_like(img)
    h, w = img.shape[:2]
    out[max(0, dy):h + min(0, dy), max(0, dx):w + min(0, dx)] = \
        img[max(0, -dy):h - max(0, dy), max(0, -dx):w - max(0, dx)]
    return out


def test_boxes_are_on_the_right_page_and_clear_the_varying_regions():
    """Both boxes must dodge what changes per song: the cover art, the collage
    and the decoration column. A box over any of them names by magazine."""
    for tag, b in (("A", el.A), ("B", el.B)):
        for x in range(b.x0, b.x1):
            for y in range(b.y0, b.y1):
                zone = ep.excluded(x, y)
                # The results-field rects are expected: naming the layout is
                # exactly reading which results fields exist.
                assert zone is None or zone.startswith("results_"), \
                    f"{tag} covers ({x},{y}) inside {zone}"


def test_names_every_end_frame_correctly(corpus):
    ends = [s for s in corpus if s.screen_id in (el.PRACTICE, el.FACEOFF)]
    assert len(ends) >= 8
    for s in ends:
        got = el.name(s.image)
        assert got.name == s.screen_id, \
            f"{s.path.name}: got {got.name}, contrast {got.contrast}"


def test_survives_value_slop_and_a_static_offset(corpus):
    rng = np.random.default_rng(3)
    ends = [s for s in corpus if s.screen_id in (el.PRACTICE, el.FACEOFF)]
    for s in ends:
        for cat, pname, img in perturb.envelope(s.image, rng):
            if cat == "translate" or img.shape[:2] != (el.CANON_H, el.CANON_W):
                continue
            assert el.name(img).name == s.screen_id, f"{s.path.name} + {pname}"
        for dx in (-END_SHIFT, 0, END_SHIFT):
            for dy in (-END_SHIFT, 0, END_SHIFT):
                img = _shift(s.image, dx, dy)
                assert el.name(img).name == s.screen_id, \
                    f"{s.path.name} + shift({dx},{dy}): {el.contrast(img)}"


def test_the_dead_band_has_margin_on_both_sides(corpus):
    prac = [el.contrast(s.image) for s in corpus if s.screen_id == el.PRACTICE]
    face = [el.contrast(s.image) for s in corpus if s.screen_id == el.FACEOFF]
    assert max(prac) < el.T_PRACTICE - 20, "practice side has no headroom"
    assert min(face) > el.T_FACEOFF + 20, "faceoff side has no headroom"


def test_other_screens_leak_and_that_is_why_the_precondition_exists(corpus):
    """Pinned deliberately. A-B separates the two end *layouts*; it is not a
    screen classifier, and endprobe does not close the gap either. The day
    someone calls this without establishing an end screen is up, this test is
    the record that it was known."""
    others = [s for s in corpus
              if s.screen_id not in (el.PRACTICE, el.FACEOFF)]
    leaked = [s.screen_id for s in others if el.name(s.image).certain]
    assert leaked, "no leak — did the boxes get more specific? re-read the docstring"


def test_wrong_frame_size_is_rejected():
    with pytest.raises(ValueError):
        el.name(np.zeros((240, 320, 3), dtype=np.uint8))


def test_pixel_reads_are_negligible():
    assert el.pixel_reads() <= 1200
