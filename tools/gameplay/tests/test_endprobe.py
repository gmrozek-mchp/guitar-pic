"""End-of-song probe: corpus separation, analog-slop robustness, tracker edges."""

from __future__ import annotations

import numpy as np
import pytest

from gameplay import endprobe as ep
from gameplay import perturb
from gameplay.corpus import load_corpus

END_SCREENS = ("practice_end_menu", "faceoff_end_menu")
GAMEPLAY_SCREENS = ("in_song", "in_song_2p")


@pytest.fixture(scope="module")
def corpus():
    return [s for s in load_corpus() if s.image.shape[:2] == (ep.CANON_H, ep.CANON_W)]


def _of(corpus, screens):
    return [s for s in corpus if s.screen_id in screens]


def test_patches_are_inside_the_frame():
    """Every lattice sample must land in the canonical frame, margins included."""
    for p in ep.BRIGHT + ep.ANCHORS:
        assert p.half <= p.x < ep.CANON_W - p.half
        assert p.half <= p.y < ep.CANON_H - p.half


def test_no_patch_sample_lands_in_an_excluded_zone():
    """Every lattice sample, not just the centre, must clear every exclusion.

    These zones are rules rather than measurements (per-song art, results fields,
    where gameplay puts bright moving content), so the corpus tests above cannot
    catch a violation — a patch sitting on the note highway still reads dark on
    the 6 static gameplay frames we have.
    """
    for kind, patches in (("bright", ep.BRIGHT), ("anchor", ep.ANCHORS)):
        for idx, p in enumerate(patches):
            c = (p.n - 1) // 2
            for i in range(p.n):
                for j in range(p.n):
                    x, y = p.x + (j - c) * p.stride, p.y + (i - c) * p.stride
                    zone = ep.excluded(x, y)
                    assert zone is None, f"{kind}[{idx}] samples ({x},{y}) inside {zone}"


def test_table_shapes():
    assert len(ep.THRESH) == len(ep.BRIGHT)
    assert 0 < ep.K_HITS <= len(ep.BRIGHT)
    assert len(ep.ANCHORS) % 2 == 1, "median anchor reducer needs an odd count"


def test_every_end_frame_fires(corpus):
    ends = _of(corpus, END_SCREENS)
    assert len(ends) >= 8
    for s in ends:
        r = ep.read(s.image)
        assert r.is_end, f"{s.path.name}: only {r.hits} hits, contrast {r.contrast}"


def test_no_gameplay_frame_fires(corpus):
    plays = _of(corpus, GAMEPLAY_SCREENS)
    assert len(plays) >= 6
    for s in plays:
        r = ep.read(s.image)
        assert not r.is_end, f"{s.path.name}: {r.hits} hits, contrast {r.contrast}"


def test_separation_margin(corpus):
    """The gap the design leans on: end contrast far above gameplay contrast.

    A regression here (a re-picked point, a new corpus frame) should fail loudly
    rather than silently eat the headroom the slop tolerance is bought with.
    """
    ends = np.array([ep.contrasts(s.image) for s in _of(corpus, END_SCREENS)])
    plays = np.array([ep.contrasts(s.image) for s in _of(corpus, GAMEPLAY_SCREENS)])
    for i in range(len(ep.BRIGHT)):
        assert ends[:, i].min() - plays[:, i].max() >= 60, f"patch {i} margin too thin"


def test_thresholds_sit_between_the_populations(corpus):
    ends = np.array([ep.contrasts(s.image) for s in _of(corpus, END_SCREENS)])
    plays = np.array([ep.contrasts(s.image) for s in _of(corpus, GAMEPLAY_SCREENS)])
    for i, t in enumerate(ep.THRESH):
        assert plays[:, i].max() < t < ends[:, i].min()


def test_survives_the_analog_slop_envelope(corpus):
    """Gain/offset/noise/translate/scale must not flip either population."""
    rng = np.random.default_rng(7)
    for s in _of(corpus, END_SCREENS) + _of(corpus, GAMEPLAY_SCREENS):
        want = s.screen_id in END_SCREENS
        for _cat, name, img in perturb.envelope(s.image, rng):
            if img.shape[:2] != (ep.CANON_H, ep.CANON_W):
                continue
            got = ep.read(img).is_end
            assert got == want, f"{s.path.name} + {name}: is_end={got}, want {want}"


def test_no_other_screen_would_fire(corpus):
    """Menus must not trip it.

    Not safety-critical (the probe only runs inside the actuation window, and a
    hit on a menu is a correct veto anyway), but a menu firing would mean the
    patches are keying on generic paper brightness rather than the end layout.
    """
    others = [s for s in corpus if s.screen_id not in END_SCREENS + GAMEPLAY_SCREENS]
    for s in others:
        r = ep.read(s.image)
        assert not r.is_end, f"{s.path.name}: {r.hits} hits, contrast {r.contrast}"


def test_wrong_frame_size_is_rejected():
    with pytest.raises(ValueError):
        ep.read(np.zeros((240, 320, 3), dtype=np.uint8))


def test_anchor_is_the_median_not_an_extreme():
    img = np.zeros((ep.CANON_H, ep.CANON_W, 3), dtype=np.uint8)
    lo, mid, hi = ep.ANCHORS
    for a, val in ((lo, 10), (mid, 40), (hi, 200)):
        c = (a.n - 1) // 2
        for i in range(a.n):
            for j in range(a.n):
                img[a.y + (i - c) * a.stride, a.x + (j - c) * a.stride] = val
    # The luma weights sum to 256, so a neutral grey of level v reads back as v.
    assert ep.anchor_level(img) == 40


class TestTracker:
    def _probe(self, hits):
        return ep.EndProbe(hits=hits, contrast=(0,) * len(ep.BRIGHT), anchor=0)

    def test_latches_only_after_confirm_frames(self):
        t = ep.EndProbeTracker()
        for i in range(ep.CONFIRM_FRAMES - 1):
            assert not t.update(self._probe(ep.K_HITS)), f"latched early at frame {i}"
        assert t.update(self._probe(ep.K_HITS))

    def test_one_clean_frame_releases_immediately(self):
        """Fall is immediate so a false veto costs missed notes, never the run."""
        t = ep.EndProbeTracker()
        for _ in range(ep.CONFIRM_FRAMES):
            t.update(self._probe(ep.K_HITS))
        assert t.latched
        assert not t.update(self._probe(0))
        assert t.count == 0

    def test_a_gap_restarts_the_confirm_count(self):
        t = ep.EndProbeTracker()
        t.update(self._probe(ep.K_HITS))
        t.update(self._probe(ep.K_HITS - 1))
        assert not t.update(self._probe(ep.K_HITS))

    def test_below_k_never_latches(self):
        t = ep.EndProbeTracker()
        for _ in range(10):
            assert not t.update(self._probe(ep.K_HITS - 1))
