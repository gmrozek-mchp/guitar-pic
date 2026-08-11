"""End-of-song probe: corpus separation, analog-slop robustness, tracker edges."""

from __future__ import annotations

import numpy as np
import pytest

from gameplay import endprobe as ep
from gameplay import perturb
from gameplay.corpus import load_corpus

END_SCREENS = ("practice_end_menu", "faceoff_end_menu")
GAMEPLAY_SCREENS = ("in_song", "in_song_2p")

# The end side carries a static per-rig capture offset only; the shake that
# motivates the wider envelope happens while *playing*. See the module docstring.
END_SHIFT = 2


@pytest.fixture(scope="module")
def corpus():
    return [s for s in load_corpus() if s.image.shape[:2] == (ep.CANON_H, ep.CANON_W)]


def _of(corpus, screens):
    return [s for s in corpus if s.screen_id in screens]


def _shift(img, dx, dy):
    out = np.zeros_like(img)
    h, w = img.shape[:2]
    out[max(0, dy):h + min(0, dy), max(0, dx):w + min(0, dx)] = \
        img[max(0, -dy):h - max(0, dy), max(0, -dx):w - max(0, dx)]
    return out


def test_boxes_are_inside_the_frame():
    for b in ep.BRIGHT + ep.DARK:
        assert 0 <= b.x0 < b.x1 <= ep.CANON_W
        assert 0 <= b.y0 < b.y1 <= ep.CANON_H
        assert b.pixels > 0


def test_bright_boxes_clear_every_exclusion():
    """Rules the corpus cannot check: per-song art, the per-magazine decoration
    column, results fields. A box over any of them reads fine on the frames we
    have and fails on the next song."""
    for idx, b in enumerate(ep.BRIGHT):
        for x in range(b.x0, b.x1):
            for y in range(b.y0, b.y1):
                zone = ep.excluded(x, y)
                assert zone is None, f"BRIGHT[{idx}] covers ({x},{y}) inside {zone}"


def test_dark_boxes_sit_in_a_pill_and_clear_the_glyphs():
    """The dark reference is only stable because it is pill black. A box that
    catches a glyph edge moves when the frame shifts, which is what killed the
    point-lattice version."""
    for idx, b in enumerate(ep.DARK):
        assert any(ep._inside(b, p) for p in ep.HINT_BAR_PILLS), \
            f"DARK[{idx}] is not inside a hint-bar pill"
        for g in ep.HINT_BAR_GLYPHS:
            assert not ep._overlaps(b, g), f"DARK[{idx}] overlaps glyph {g}"


def test_table_shapes():
    assert len(ep.THRESH) == len(ep.BRIGHT)
    assert 0 < ep.K_HITS <= len(ep.BRIGHT)
    assert len(ep.DARK) % 2 == 1, "median dark reducer needs an odd count"


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
    """The gap the design leans on. A regression here (a re-picked box, a new
    corpus frame) should fail loudly rather than silently eat the headroom the
    slop tolerance is bought with."""
    ends = np.array([ep.contrasts(s.image) for s in _of(corpus, END_SCREENS)])
    plays = np.array([ep.contrasts(s.image) for s in _of(corpus, GAMEPLAY_SCREENS)])
    for i in range(len(ep.BRIGHT)):
        assert ends[:, i].min() - plays[:, i].max() >= 60, f"box {i} margin too thin"


def test_thresholds_sit_between_the_populations(corpus):
    ends = np.array([ep.contrasts(s.image) for s in _of(corpus, END_SCREENS)])
    plays = np.array([ep.contrasts(s.image) for s in _of(corpus, GAMEPLAY_SCREENS)])
    for i, t in enumerate(ep.THRESH):
        assert plays[:, i].max() < t < ends[:, i].min()


def test_end_frames_survive_value_slop_and_a_static_offset(corpus):
    """Gain, offset and noise in full; position only to the static rig offset."""
    rng = np.random.default_rng(7)
    for s in _of(corpus, END_SCREENS):
        for _cat, name, img in perturb.envelope(s.image, rng):
            if _cat == "translate" or img.shape[:2] != (ep.CANON_H, ep.CANON_W):
                continue
            assert ep.read(img).is_end, f"{s.path.name} + {name}: stopped firing"
        for dx in (-END_SHIFT, 0, END_SHIFT):
            for dy in (-END_SHIFT, 0, END_SHIFT):
                img = perturb.adjust_gain(_shift(s.image, dx, dy), 0.85)
                assert ep.read(img).is_end, \
                    f"{s.path.name} + shift({dx},{dy}) + gain 0.85: stopped firing"


def test_gameplay_frames_survive_the_full_envelope_including_shake(corpus):
    """The asymmetric half: a shaken, over-gained gameplay frame must not veto.

    This is the expensive direction — a false veto mid-song costs actuation until
    the controller's next poll, so it has to hold under the shake that missing a
    note actually produces.
    """
    rng = np.random.default_rng(11)
    for s in _of(corpus, GAMEPLAY_SCREENS):
        for _cat, name, img in perturb.envelope(s.image, rng):
            if img.shape[:2] != (ep.CANON_H, ep.CANON_W):
                continue
            assert not ep.read(img).is_end, f"{s.path.name} + {name}: false veto"
        for dx in (-6, -4, 4, 6):
            for dy in (-6, -4, 4, 6):
                img = perturb.adjust_gain(_shift(s.image, dx, dy), 1.15)
                assert not ep.read(img).is_end, \
                    f"{s.path.name} + shift({dx},{dy}) + gain 1.15: false veto"


def test_menus_may_fire_and_that_is_the_contract(corpus):
    """The probe keys on "bright page above a dark hint bar", which most menus
    also satisfy — measured, not assumed. It is only ever consulted inside the
    actuation window, where a menu appearing is a correct veto, and naming the
    screen is endlayout's job. Asserted so the day this stops being true is
    visible rather than silent."""
    others = [s for s in corpus if s.screen_id not in END_SCREENS + GAMEPLAY_SCREENS]
    fired = sum(1 for s in others if ep.read(s.image).is_end)
    assert fired > 0, "menus no longer fire — the probe got more specific, re-read the docs"


def test_wrong_frame_size_is_rejected():
    with pytest.raises(ValueError):
        ep.read(np.zeros((240, 320, 3), dtype=np.uint8))


def test_anchor_is_the_median_not_an_extreme():
    img = np.zeros((ep.CANON_H, ep.CANON_W, 3), dtype=np.uint8)
    for d, val in zip(ep.DARK, (10, 40, 200), strict=True):
        img[d.y0:d.y1, d.x0:d.x1] = val
    # The luma weights sum to 256, so a neutral grey of level v reads back as v.
    assert ep.anchor_level(img) == 40


def test_pixel_reads_stay_affordable():
    """It runs on every drained frame, so the cost is part of the contract."""
    assert ep.pixel_reads() <= 2000


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
