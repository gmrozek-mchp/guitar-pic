"""Real hardware frames that once broke something, kept as held-out regressions.

These live in `data/regression/`, deliberately *not* in the `gh3_screens/` corpus:
folding them in would move the very centroids and thresholds they exist to test,
so each would stop being evidence the moment it was added. Filenames follow the
corpus convention (`<screen_id>__<variant>.png`), so the expected screen comes
from the name and adding a frame needs no new code.

Provenance of the frames here:

- `practice_end_menu__anarchy_in_the_uk.png` — marvin's own capture, 2026-08-10.
  A 1P ROBOT run finished and the classifier returned UNKNOWN, so `play_until_done`
  waited for a decisive screen name that never came and the run hung until STOP.
  The cause was the per-song magazine cover: a large bright region on a screen
  whose corpus exemplars all share one dark cover, and because the fingerprint
  normalizes per frame it moved *every* cell. 156 from the other end screen's
  centroid against t_margin 645 — rejected by the margin gate while sitting at
  0.30x t_abs.

- `faceoff_end_menu__one_metallica.png` — marvin's own capture, 2026-08-11. A 2P
  run ended on a *different magazine* (The Flaming Pick, dark red comic collage,
  against the corpus's Backwater Rocker on bright sketch paper). This retired the
  assumption the previous frame's fix rested on: the collage is not page
  furniture, it is per-song. It broke both readers at once — the end probe scored
  1/6 because its bright patches sat on the collage, and the fingerprint put this
  frame 5192 from `song_select` without `faceoff_end_menu` in its top four.
  That is what moved end-screen naming out of the fingerprint and into
  `endlayout` (page layout) with `endprobe` (page furniture) as the veto.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from gameplay import endlayout as el
from gameplay import endprobe as ep
from gameplay.classifier import build_templates, classify_image
from gameplay.corpus import load_bgr, load_corpus
from gameplay.evaluate import labelled_fps, recommend_thresholds
from gameplay.fingerprint import FingerprintConfig
from gameplay.screens import screen_id_for_filename

_DIR = Path(__file__).resolve().parent.parent / "data" / "regression"
_END_SCREENS = (el.PRACTICE, el.FACEOFF)


def _frames():
    return sorted(_DIR.glob("*.png"))


@pytest.fixture(scope="module")
def classifier():
    cfg = FingerprintConfig()
    corpus = load_corpus()
    templates = build_templates(labelled_fps(corpus, cfg), cfg)
    rec = recommend_thresholds(corpus, cfg)
    return templates, rec


def test_there_are_regression_frames():
    assert _frames(), f"no regression frames in {_DIR}"


@pytest.mark.parametrize("path", _frames(), ids=lambda p: p.stem)
def test_the_end_probe_fires_on_every_held_out_end_screen(path):
    """Each of these is a song the corpus has never seen, which is the only real
    test of "does this survive a new magazine"."""
    if screen_id_for_filename(path.name) not in _END_SCREENS:
        pytest.skip("not an end screen")
    r = ep.read(load_bgr(path))
    assert r.is_end, f"probe missed a real end screen: {r.hits} hits, {r.contrast}"


@pytest.mark.parametrize("path", _frames(), ids=lambda p: p.stem)
def test_the_layout_namer_gets_every_held_out_end_screen_right(path):
    expected = screen_id_for_filename(path.name)
    if expected not in _END_SCREENS:
        pytest.skip("not an end screen")
    got = el.name(load_bgr(path))
    assert got.name == expected, \
        f"{path.name}: named {got.name} (want {expected}), contrast {got.contrast}"


@pytest.mark.parametrize("path", _frames(), ids=lambda p: p.stem)
def test_non_end_frames_still_classify_by_fingerprint(path, classifier):
    """The fingerprint keeps its job for everything that is not an end screen."""
    expected = screen_id_for_filename(path.name)
    if expected in _END_SCREENS:
        pytest.skip("end screens are named by endlayout, not the fingerprint")
    templates, rec = classifier
    got = classify_image(load_bgr(path), templates,
                         t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin)
    assert got.screen_id == expected, (
        f"{path.name}: classified {got.screen_id} (want {expected}); "
        f"d={got.best_dist} margin={got.margin}"
    )


def test_the_fingerprint_cannot_name_a_new_magazine(classifier):
    """Pinned, because it is the entire reason `endlayout` exists.

    If this ever starts passing, the fingerprint has become magazine-independent
    and the naming path is worth revisiting — but it should be a deliberate
    decision, not a silent change nobody notices.
    """
    path = _DIR / "faceoff_end_menu__one_metallica.png"
    if not path.exists():
        pytest.skip("frame not present")
    templates, rec = classifier
    got = classify_image(load_bgr(path), templates,
                         t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin)
    assert got.screen_id != el.FACEOFF, (
        "the fingerprint now names a new magazine's end screen — "
        "re-read endlayout's docstring and decide whether it is still needed"
    )
