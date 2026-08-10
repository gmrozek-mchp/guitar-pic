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
  The cause was the per-song magazine cover: it is a large, bright region on a
  screen whose corpus exemplars all share one dark cover, and because the
  fingerprint normalizes per frame it moved *every* cell. The frame landed 156 from
  the other end screen's centroid against a t_margin of 645 — rejected by the
  margin gate while sitting at 0.30x t_abs. Fixed by excluding that region from the
  fingerprint (fingerprint.EXCLUDED_REGIONS).
"""

from __future__ import annotations

from pathlib import Path

import pytest

from gameplay import endprobe as ep
from gameplay.classifier import build_templates, classify_image
from gameplay.corpus import load_bgr, load_corpus
from gameplay.evaluate import labelled_fps, recommend_thresholds
from gameplay.fingerprint import FingerprintConfig
from gameplay.screens import screen_id_for_filename

_DIR = Path(__file__).resolve().parent.parent / "data" / "regression"


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
def test_held_out_frame_classifies(path, classifier):
    templates, rec = classifier
    expected = screen_id_for_filename(path.name)
    got = classify_image(load_bgr(path), templates,
                         t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin)
    assert got.screen_id == expected, (
        f"{path.name}: classified {got.screen_id} (want {expected}); "
        f"d={got.best_dist} margin={got.margin} "
        f"vs t_abs={rec.rec_t_abs} t_margin={rec.rec_t_margin}"
    )


def test_the_end_probe_fires_on_an_unseen_song(classifier):
    """Independent value: this frame is a *different song* to every corpus frame.

    The 6 bright patches sit on the collage surround, which we assumed — rather
    than measured — is constant across songs. This is the first real evidence for
    that assumption, so it is asserted rather than left implicit.
    """
    path = _DIR / "practice_end_menu__anarchy_in_the_uk.png"
    if not path.exists():
        pytest.skip("frame not present")
    r = ep.read(load_bgr(path))
    assert r.is_end, f"probe missed a real end screen: {r.hits} hits, {r.contrast}"
    assert r.hits == len(ep.BRIGHT), f"expected all patches to fire, got {r.hits}"
