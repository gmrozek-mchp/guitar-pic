"""2-player amp scoreboard: registration + score digit reader (gameplay/amp2p.py).

Proves the reader offline before any firmware port. The committed labelled corpus
covers both sides (one gameplay capture each), and the strongest test remains
cross-source: the bank must read the *screen* corpus — different frames, different
capture path — exactly. The rest pins the geometry, the parse rules, and the
invariances the design claims: registration absorbs positional slop, the per-cell
relative ink threshold absorbs analog gain/offset, and the two layouts (1-5 digits at
pitch 9, 6 digits at pitch 8) are told apart by grid agreement and read against their
own font's templates.
"""

from __future__ import annotations

import numpy as np
import pytest

from gameplay import amp2p, evaluate, perturb
from gameplay.metadata import (
    AMP2P_BAND_H,
    AMP2P_BAND_Y0,
    AMP2P_BLOCK,
    AMP2P_CONTAINER_W,
    AMP2P_FONTS,
    AMP2P_GRID,
    AMP2P_REF_CELLS,
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

# The labelled corpus and the registration masks are both cut/painted against a
# specific AMP2P_BLOCK origin, so re-registering a block supersedes both. Skip
# rather than fail in that state: nothing here is broken code, the data is stale.
def _pending_reason() -> str | None:
    """Why this module cannot run yet, or None."""
    stale = amp2p.mask_origin_mismatch()
    if stale is not None:
        return stale
    from gameplay.corpus import load_amp2p_corpus
    if not load_amp2p_corpus():
        return ("no 2p amp digit corpus: re-cut labelled crops from a capture taken at the "
                "current marvin-perf region rects — `marvin-perf export-region <capture> "
                "--kind score-2p-{left,right}`, label by eye, name them with "
                "`amp2p.corpus_name`.")
    return None


pytestmark = pytest.mark.skipif(_pending_reason() is not None, reason=_pending_reason() or "")


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


def test_bank_covers_every_digit_in_every_font(bank):
    """A font with a missing digit cannot read a score that contains it."""
    assert set(bank.fonts) == set(AMP2P_FONTS)
    for font in AMP2P_FONTS:
        assert bank.digits_for_font(font) == set(range(10)), f"font {font}"


def test_bank_keeps_brightness_variants(bank):
    """More templates than digits — the pulse variants are not averaged away."""
    for font in AMP2P_FONTS:
        assert len(bank.for_font(font)) > 10, f"font {font}"


def test_a_cell_read_against_the_wrong_font_fails_the_gate(amp2p_corpus, bank):
    """Why the bank is keyed by font: the other font's templates cannot read this cell.

    Every cell reads in its own font and scores worse in the other one, and for a
    *condensed* frame the worst cell against the wide bank clears AMP2P_UNK_DIST — which
    is why 6-digit frames read as nothing at all before the bank was split (worst-cell
    L1 2703-8585 over the reference capture).

    The claim is per frame, not per cell, and only in that direction. Some individual
    glyphs are near-identical across the fonts — a `1` is a bare centred bar either way
    and scores ~950 against the wrong bank — so a per-cell assertion would be false, and
    a wide cell against the condensed bank is merely worse rather than unreadable.
    """
    from gameplay.metadata import AMP2P_UNK_DIST
    for s in amp2p_corpus:
        side, value = amp2p_score_from_filename(s.path.name)
        n = len(str(value))
        mine = AMP2P_GRID[n][1]
        theirs = 8 if mine == 9 else 9
        band = amp2p._band_luma(s.image, side)
        worst_own = worst_alien = 0.0
        for span in amp2p.cell_bounds(side, n):
            cov = amp2p.cell_cov(band, span)
            _d, own, _m = amp2p.match_cell(bank, cov, mine)
            _d, alien, _m = amp2p.match_cell(bank, cov, theirs)
            assert alien > own, f"{s.path.name}: other font {alien:.0f} <= own {own:.0f}"
            worst_own, worst_alien = max(worst_own, own), max(worst_alien, alien)
        assert worst_own <= AMP2P_UNK_DIST, f"{s.path.name}: own font {worst_own:.0f}"
        if n == 6:
            assert worst_alien > AMP2P_UNK_DIST, (
                f"{s.path.name}: the wide bank could read this condensed frame "
                f"(worst cell {worst_alien:.0f})")


def test_cell_bounds_are_right_aligned_and_pitched():
    for side in amp2p.SIDES:
        for n, (w, pitch) in sorted(AMP2P_GRID.items()):
            spans = amp2p.cell_bounds(side, n)
            assert len(spans) == n
            assert spans[-1][1] == AMP2P_RIGHT_EDGE[side]
            assert all(x1 - x0 == w for x0, x1 in spans)
            assert all(b[0] - a[0] == pitch for a, b in zip(spans, spans[1:]))


def test_cells_stay_inside_the_block():
    """At either layout, no cell runs off the block or into the band edge."""
    for side in amp2p.SIDES:
        x0, y0, x1, y1 = AMP2P_BLOCK[side]
        for n in AMP2P_GRID:
            spans = amp2p.cell_bounds(side, n)
            assert spans[0][0] >= 0 and spans[-1][1] <= x1 - x0
        assert AMP2P_BAND_Y0 + AMP2P_BAND_H <= y1 - y0


def test_both_layouts_fit_the_container_and_the_wide_one_could_not_stretch():
    """The container width is *why* 6 digits re-lay-out, not a matter of taste."""
    for n, (w, pitch) in sorted(AMP2P_GRID.items()):
        assert (n - 1) * pitch + w <= AMP2P_CONTAINER_W, f"{n} digits overflow the strip"
    # Six cells at the wide pitch is exactly what does not fit — hence pitch 8 at 6.
    w9, p9 = AMP2P_GRID[AMP2P_REF_CELLS]
    assert 5 * p9 + w9 > AMP2P_CONTAINER_W
    assert AMP2P_GRID[6][1] < p9
    for side in amp2p.SIDES:
        cx0, cx1 = amp2p.container_span(side)
        assert cx1 == AMP2P_RIGHT_EDGE[side] and cx1 - cx0 == AMP2P_CONTAINER_W
        assert cx0 >= 0
        assert amp2p.cell_bounds(side, 6)[0][0] >= cx0


def test_the_wide_grid_is_the_ink_threshold_reference():
    """grid_span must stay the 5-cell wide grid even though a wider layout exists.

    The ink threshold, the blank gate and their measured values are all set against
    these columns; pinning it to max(AMP2P_GRID) would silently re-tune the 1-5 digit
    path when the 6-digit row was added.
    """
    for side in amp2p.SIDES:
        assert amp2p.grid_span(side) == (
            amp2p.cell_bounds(side, AMP2P_REF_CELLS)[0][0], AMP2P_RIGHT_EDGE[side])


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
    spans = amp2p.cell_bounds(side, AMP2P_REF_CELLS)
    rows = slice(AMP2P_BAND_Y0, AMP2P_BAND_Y0 + AMP2P_BAND_H)
    img[rows, spans[0][0]:spans[0][1]] = img[rows, spans[3][0]:spans[3][1]]
    r = amp2p.read_amp2p_score(img, bank, calib[side], side)
    assert r.value is None and r.layout_unknown, f"got {r.value} ({r.reason})"


def test_the_two_grids_are_mutually_exclusive(amp2p_corpus):
    """No frame may satisfy both layouts — that disjointness *is* the layout decision.

    Holds on every labelled frame here, and over the 19 091 presence-gated frames of
    the reference 222 692-point play (15 708 condensed, 3 381 wide, none both).
    """
    for s in amp2p_corpus:
        side, value = amp2p_score_from_filename(s.path.name)
        band = amp2p._band_luma(s.image, side)
        six = amp2p.is_six_digit(band, side)
        n, aligned = amp2p._powered_cells(band, side)
        wide = aligned and n > 0 and (
            n < AMP2P_REF_CELLS
            or amp2p._grid_aligned(band, side, AMP2P_REF_CELLS, amp2p.AMP2P_WIDE_LAYOUT))
        assert six == (len(str(value)) == 6), s.path.name
        assert not (six and wide), f"{s.path.name} satisfies both grids"


def test_a_read_reports_the_layout_it_used(amp2p_corpus, bank, calib):
    for s in amp2p_corpus:
        side, value = amp2p_score_from_filename(s.path.name)
        r = amp2p.read_amp2p_score(s.image, bank, calib[side], side)
        assert r.layout == AMP2P_GRID[len(str(value))]


def test_a_strip_on_neither_grid_is_flagged(amp2p_corpus, bank, calib):
    """Ink that sits on no measured layout must not be forced onto one."""
    s = next(x for x in amp2p_corpus if len(str(amp2p_score_from_filename(x.path.name)[1])) == 5)
    side, _value = amp2p_score_from_filename(s.path.name)
    block = s.image.copy()
    rows = slice(AMP2P_BAND_Y0, AMP2P_BAND_Y0 + AMP2P_BAND_H)
    # Smear the widest cell across the whole container: ink everywhere, so neither
    # layout's gap columns can be clean.
    cx0, cx1 = amp2p.container_span(side)
    x0, x1 = amp2p.cell_bounds(side, 5)[-1]
    block[rows, cx0:cx1] = np.tile(block[rows, x0:x1], (1, -(-(cx1 - cx0) // (x1 - x0)), 1))[
        :, : cx1 - cx0
    ]
    r = amp2p.read_amp2p_score(block, bank, calib[side], side)
    assert r.value is None and r.layout_unknown, f"got {r.value} ({r.reason})"


def test_alien_glyphs_are_gated_not_guessed(bank, calib):
    """Noise in every cell must fail the match gates rather than yield a number."""
    rng = np.random.default_rng(0)
    img = np.zeros((*amp2p.block_size("right"), 3), dtype=np.uint8)
    for x0, x1 in amp2p.cell_bounds("right", AMP2P_REF_CELLS):
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


# ─── temporal filter ──────────────────────────────────────────────────────────


def _read(value, n_cells=5):
    return amp2p.Amp2pScoreRead(
        side="left", value=value, digits=(), n_cells=n_cells, dist=0.0, margin=0.0,
        layout_unknown=False,
    )


def test_tracker_accepts_a_rise_immediately():
    """Marvin's score only ever climbs during play, so a rise must cost no latency."""
    tr = amp2p.Amp2pTracker(side="left")
    for v in (0, 50, 150, 36362):
        assert tr.update(_read(v)) == v
    assert tr.n_rejected == 0


def test_tracker_rejects_a_single_frame_idle_composite():
    """One frame of the amp's idle art must not drag the score back to zero."""
    tr = amp2p.Amp2pTracker(side="left")
    tr.update(_read(36362))
    assert tr.update(_read(0, n_cells=1)) == 36362      # the artifact frame
    assert tr.update(_read(36362)) == 36362
    assert tr.n_rejected == 1


def test_tracker_accepts_a_sustained_reset():
    """A real song boundary holds the strip at zero, so it clears the confirm count."""
    tr = amp2p.Amp2pTracker(side="left")
    tr.update(_read(58806))
    for _ in range(amp2p.AMP2P_FALL_CONFIRM - 1):
        assert tr.update(_read(0, n_cells=1)) == 58806
    assert tr.update(_read(0, n_cells=1)) == 0


def test_tracker_holds_through_unreadable_frames():
    """An unread frame is not evidence either way — hold, and drop a pending fall."""
    tr = amp2p.Amp2pTracker(side="left")
    tr.update(_read(36362))
    assert tr.update(_read(0, n_cells=1)) == 36362
    assert tr.update(_read(None)) == 36362
    assert tr.update(_read(0, n_cells=1)) == 36362      # the confirm count restarted
    assert tr.value == 36362


def test_bank_is_deterministic(amp2p_corpus):
    """Same corpus in, same templates out — the variant split must not wander."""
    a = amp2p.build_amp2p_bank(amp2p_corpus)
    b = amp2p.build_amp2p_bank(list(reversed(amp2p_corpus)))
    assert a.digits == b.digits
    for ta, tb in zip(a.templates, b.templates):
        assert np.array_equal(ta.vec, tb.vec)


# ─── capture ordering ─────────────────────────────────────────────────────────


def test_capture_frames_orders_by_frame_number(tmp_path):
    """Frame order, not lexicographic order.

    `export-region` zero-pads to a fixed width, so once a capture passes that width
    a plain sort splices low-numbered frames into the middle (`-1001` between
    `-10009` and `-10010`). The ordering and adjacent-delta checks in the monotonic
    evals are meaningless on a shuffled sequence, and the shuffle hides itself: the
    ordering check absorbs each splice as a "song reset".
    """
    numbers = [1, 999, 1000, 1001, 1495, 9999, 10000, 10009, 10010, 14941]
    for n in numbers:
        (tmp_path / f"score-2pR-{n:04d}.png").write_bytes(b"")
    got = [int(p.stem.rsplit("-", 1)[1]) for p in evaluate.capture_frames(tmp_path)]
    assert got == sorted(numbers)
    # The bug this guards against, spelled out: plain sorting really does misorder.
    lex = [int(p.stem.rsplit("-", 1)[1]) for p in sorted(tmp_path.glob("*.png"))]
    assert lex != sorted(numbers)


def test_capture_frames_tolerates_mixed_pad_widths(tmp_path):
    """A directory holding both 4- and 5-digit exports still comes out in order."""
    for name in ("score-2pR-0999.png", "score-2pR-01000.png", "score-2pR-10010.png"):
        (tmp_path / name).write_bytes(b"")
    got = [int(p.stem.rsplit("-", 1)[1]) for p in evaluate.capture_frames(tmp_path)]
    assert got == [999, 1000, 10010]


def test_capture_frames_keeps_unnumbered_files_last(tmp_path):
    """Stray files (montages, notes) sort after the frames instead of interleaving."""
    (tmp_path / "score-2pR-00002.png").write_bytes(b"")
    (tmp_path / "score-2pR-00001.png").write_bytes(b"")
    (tmp_path / "montage.png").write_bytes(b"")
    assert [p.name for p in evaluate.capture_frames(tmp_path)] == [
        "score-2pR-00001.png", "score-2pR-00002.png", "montage.png",
    ]
