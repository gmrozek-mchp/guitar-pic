"""2-player amp scoreboard: registration + score digit reader (gameplay/amp2p.py).

Proves the reader offline before any firmware port. The committed labelled corpus is
**right**-side only (one capture), so the strongest test here is cross-side: a bank
built from those frames must read the *left* amp in the screen corpus exactly. The
rest pins the geometry, the parse rules, and the invariances the design claims —
registration absorbs positional slop, and the per-cell relative ink threshold
absorbs analog gain/offset.
"""

from __future__ import annotations

import numpy as np
import pytest

from gameplay import amp2p, perturb
from gameplay.metadata import (
    AMP2P_BAND_H,
    AMP2P_BAND_Y0,
    AMP2P_BLOCK,
    AMP2P_GRID,
    AMP2P_RIGHT_EDGE,
    amp2p_score_from_filename,
)

# Read off the committed 2-player screen corpus by eye; the bank never sees these.
SNAPSHOT_SCORES = {
    "in_song_2p__0200.png": {"left": 6906, "right": 0},
    "in_song_2p__0201.png": {"left": 0, "right": 4376},
    "in_song_2p__0202.png": {"left": 486, "right": 4400},
    "in_song_2p__0203.png": {"left": 2386, "right": 4400},
    "in_song_2p__web0714.png": {"left": 6906, "right": 0},
}

_VALUE_AXES = ("gain", "offset", "noise")  # per-frame realistic on a pixel-locked capture


@pytest.fixture(scope="module")
def bank(amp2p_corpus):
    assert amp2p_corpus, "no 2p score corpus (data/scores/score2p__*.png)"
    return amp2p.build_amp2p_bank(amp2p_corpus)


@pytest.fixture(scope="module")
def calib():
    return {side: amp2p.AmpCalibration(side=side) for side in amp2p.SIDES}


def _snapshots(corpus):
    return {s.path.name: s.image for s in corpus if s.path.name in SNAPSHOT_SCORES}


# ─── bank + geometry ───────────────────────────────────────────────────────────


def test_bank_covers_every_digit(bank):
    assert set(bank.digits) == set(range(10))


def test_bank_keeps_brightness_variants(bank):
    """More templates than digits — the pulse variants are not averaged away."""
    assert len(bank.templates) > 10


def test_cell_bounds_are_right_aligned_and_pitched():
    for side in amp2p.SIDES:
        for n, (w, pitch) in sorted(AMP2P_GRID.items()):
            spans = amp2p.cell_bounds(side, n)
            assert len(spans) == n
            assert spans[-1][1] == AMP2P_RIGHT_EDGE[side]
            assert all(x1 - x0 == w for x0, x1 in spans)
            assert all(b[0] - a[0] == pitch for a, b in zip(spans, spans[1:]))


def test_cells_stay_inside_the_block():
    """Even at the widest grid, no cell runs off the block or into the band edge."""
    for side in amp2p.SIDES:
        x0, y0, x1, y1 = AMP2P_BLOCK[side]
        spans = amp2p.cell_bounds(side, max(AMP2P_GRID))
        assert spans[0][0] >= 0 and spans[-1][1] <= x1 - x0
        assert AMP2P_BAND_Y0 + AMP2P_BAND_H <= y1 - y0


def test_no_grid_entry_beyond_the_measured_layout():
    """6+ digits re-lays-out the strip; the table must not pretend to know it."""
    assert max(AMP2P_GRID) == 5
    with pytest.raises(KeyError):
        amp2p.cell_bounds("right", 6)


def test_filename_parser():
    assert amp2p_score_from_filename("score2p__right__01643__cap1008.png") == ("right", 1643)
    assert amp2p_score_from_filename("score2p__left__00000__x.png") == ("left", 0)
    assert amp2p_score_from_filename("score__training__010584__snap0121.png") is None
    assert amp2p_score_from_filename("score2p__middle__0001__x.png") is None


# ─── reads ─────────────────────────────────────────────────────────────────────


def test_reads_its_own_corpus(amp2p_corpus, bank, calib):
    for s in amp2p_corpus:
        side, value = amp2p_score_from_filename(s.path.name)
        r = amp2p.read_amp2p_score(s.image, bank, calib[side], side)
        assert r.value == value, f"{s.path.name}: got {r.value} ({r.reason})"
        assert r.n_cells == len(str(value))


def test_reads_both_sides_of_the_screen_corpus(corpus, bank):
    """Cross-side, cross-source: a right-side bank must read the left amp exactly."""
    snaps = _snapshots(corpus)
    assert len(snaps) == len(SNAPSHOT_SCORES), "2p screen corpus frames missing"
    for name, image in sorted(snaps.items()):
        for side, expected in SNAPSHOT_SCORES[name].items():
            ref = amp2p.build_reference(side)
            cal = amp2p.calibrate(side, [image], ref, search=4)
            r = amp2p.read_amp2p_score(image, bank, cal, side)
            assert r.value == expected, f"{name} {side}: got {r.value} ({r.reason})"


def test_registration_locks_the_screen_corpus(corpus):
    """Both amps sit at their nominal ROI in every committed 2p frame."""
    for name, image in sorted(_snapshots(corpus).items()):
        for side in amp2p.SIDES:
            ref = amp2p.build_reference(side)
            cal = amp2p.calibrate(side, [image], ref, search=4)
            assert (cal.dx, cal.dy) == (0, 0), f"{name} {side}: drifted to {cal.dx},{cal.dy}"


def test_block_crop_and_full_frame_agree(corpus, bank, calib):
    """A bare region-strip crop reads the same as the full frame it came from."""
    for name, image in sorted(_snapshots(corpus).items()):
        for side in amp2p.SIDES:
            x0, y0, x1, y1 = AMP2P_BLOCK[side]
            crop = image[y0:y1, x0:x1]
            full = amp2p.read_amp2p_score(image, bank, calib[side], side)
            part = amp2p.read_amp2p_score(crop, bank, calib[side], side)
            assert part.value == full.value, f"{name} {side}"


def test_side_comes_from_the_calibration_when_omitted(amp2p_corpus, bank, calib):
    s = amp2p_corpus[0]
    side, value = amp2p_score_from_filename(s.path.name)
    assert amp2p.read_amp2p_score(s.image, bank, calib[side]).value == value


def test_rejects_an_unknown_side(amp2p_corpus, bank):
    with pytest.raises(ValueError):
        amp2p.read_amp2p_score(amp2p_corpus[0].image, bank, None, "middle")


# ─── parse rules / failure reporting ──────────────────────────────────────────


def test_blank_display_reads_as_no_value(bank, calib):
    """An unpowered strip is reported, not decoded into a number."""
    black = np.zeros((*amp2p.block_size("right"), 3), dtype=np.uint8)
    r = amp2p.read_amp2p_score(black, bank, calib["right"], "right")
    assert r.value is None and r.n_cells == 0 and not r.layout_unknown


def test_non_right_aligned_lit_pattern_is_flagged(amp2p_corpus, bank, calib):
    """A lit cell separated from the value by a blank cannot be a right-aligned score."""
    s = next(x for x in amp2p_corpus if len(str(amp2p_score_from_filename(x.path.name)[1])) == 3)
    side, value = amp2p_score_from_filename(s.path.name)
    assert amp2p.read_amp2p_score(s.image, bank, calib[side], side).value == value
    # A 3-digit value fills the 3 rightmost cells, so copying one of its glyphs into
    # the leftmost cell of the widest grid leaves cell 1 blank — a hole no
    # right-aligned value can produce.
    img = s.image.copy()
    spans = amp2p.cell_bounds(side, max(AMP2P_GRID))
    rows = slice(AMP2P_BAND_Y0, AMP2P_BAND_Y0 + AMP2P_BAND_H)
    img[rows, spans[0][0]:spans[0][1]] = img[rows, spans[3][0]:spans[3][1]]
    r = amp2p.read_amp2p_score(img, bank, calib[side], side)
    assert r.value is None and r.layout_unknown, f"got {r.value} ({r.reason})"


def test_alien_glyphs_are_gated_not_guessed(bank, calib):
    """Noise in every cell must fail the match gates rather than yield a number."""
    rng = np.random.default_rng(0)
    img = np.zeros((*amp2p.block_size("right"), 3), dtype=np.uint8)
    for x0, x1 in amp2p.cell_bounds("right", max(AMP2P_GRID)):
        patch = rng.integers(0, 256, size=(AMP2P_BAND_H, x1 - x0, 3), dtype=np.uint8)
        img[AMP2P_BAND_Y0:AMP2P_BAND_Y0 + AMP2P_BAND_H, x0:x1] = patch
    r = amp2p.read_amp2p_score(img, bank, calib["right"], "right")
    assert r.value is None and r.reason


# ─── invariances the design claims ────────────────────────────────────────────


def test_value_slop_never_produces_a_wrong_read(corpus, bank, calib):
    """Across the whole value-slop envelope a read is correct or None — never wrong.

    That is what the match gates buy. Two axes are genuinely destructive and are
    expected to land on None for some frames: `offset-neg20` clips about half the
    digit band to black, and added noise lets a single hot pixel set a cell's range.
    Both are outside a pixel-locked capture (`capture_pipeline.md`), and these
    screen-corpus frames are lower-contrast than a raw region strip — the reference
    capture reads 3027/3027 with a minimum margin of 510.
    """
    rng = np.random.default_rng(0)
    for name, image in sorted(_snapshots(corpus).items()):
        for side, expected in SNAPSHOT_SCORES[name].items():
            for cat, label, img in perturb.envelope(image, rng):
                if cat not in _VALUE_AXES:
                    continue
                r = amp2p.read_amp2p_score(img, bank, calib[side], side)
                assert r.value in (expected, None), f"{name} {side} {label}: got {r.value}"


@pytest.mark.parametrize("axis", ["gain-0.85", "gain-1.15", "offset-pos20"])
def test_benign_value_slop_reads_exactly(corpus, bank, calib, axis):
    """Gain either way and positive offset leave every read intact.

    These are the axes the per-cell relative ink threshold cancels outright: they
    rescale or lift the cell's luma range without clipping it.
    """
    rng = np.random.default_rng(0)
    for name, image in sorted(_snapshots(corpus).items()):
        img = next(i for _c, lbl, i in perturb.envelope(image, rng) if lbl == axis)
        for side, expected in SNAPSHOT_SCORES[name].items():
            r = amp2p.read_amp2p_score(img, bank, calib[side], side)
            assert r.value == expected, f"{name} {side} {axis}: got {r.value} ({r.reason})"


@pytest.mark.parametrize("dx,dy", [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, -2), (-3, 3)])
def test_registration_recovers_a_shifted_amp(corpus, bank, dx, dy):
    """Shift the frame, re-register, and the read is unchanged."""
    image = _snapshots(corpus)["in_song_2p__0200.png"]
    shifted = np.roll(np.roll(image, dy, axis=0), dx, axis=1)
    for side, expected in SNAPSHOT_SCORES["in_song_2p__0200.png"].items():
        ref = amp2p.build_reference(side)
        cal = amp2p.calibrate(side, [shifted], ref, search=4)
        assert (cal.dx, cal.dy) == (dx, dy), f"{side}: registered {cal.dx},{cal.dy}"
        r = amp2p.read_amp2p_score(shifted, bank, cal, side)
        assert r.value == expected, f"{side} shift {dx},{dy}: got {r.value}"


def test_bank_is_deterministic(amp2p_corpus):
    """Same corpus in, same templates out — the variant split must not wander."""
    a = amp2p.build_amp2p_bank(amp2p_corpus)
    b = amp2p.build_amp2p_bank(list(reversed(amp2p_corpus)))
    assert a.digits == b.digits
    for ta, tb in zip(a.templates, b.templates):
        assert np.array_equal(ta.vec, tb.vec)
