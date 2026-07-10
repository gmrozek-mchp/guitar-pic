"""Streak odometer reader + tracker.

Two halves, mirroring the firmware split:
- `read_streak` (stateless CV) is checked against the labelled corpus: every frame
  reads as present, and *confident* per-wheel reads are almost always correct — the
  few that aren't are what the tracker's monotonic/carry logic exists to absorb.
- `StreakTracker` (stateful reconcile) is checked on synthetic read sequences that
  isolate its rules: seeding, holding an unreadable wheel, carrying (099→100),
  rejecting a backward/implausible read, and resetting after a run of absent reads.

The end-to-end validation over the full 6549-frame capture (monotonic within the
run, single reset, 46/47 hand-read anchors exact) is recorded in the journal — that
capture is gitignored, so CI relies on the committed corpus + these synthetic cases.
"""

from __future__ import annotations

from gameplay.corpus import load_streak_corpus
from gameplay.metadata import streak_from_filename
from gameplay.score import (
    StreakRaw,
    StreakTracker,
    build_streak_catalog,
    read_streak,
)


def _raw(present=True, h=(0, False), t=(0, False), u=(0, False)) -> StreakRaw:
    """StreakRaw helper: each place is (digit, known)."""
    return StreakRaw(present, (h[0], t[0], u[0]), (h[1], t[1], u[1]))


# ─── stateless reader against the labelled corpus ───────────────────────────────


def test_labeled_frames_all_present():
    cat = build_streak_catalog(load_streak_corpus())
    for s in load_streak_corpus():
        assert read_streak(s.image, cat).present, s.path.name


def test_confident_reads_mostly_correct():
    corpus = load_streak_corpus()
    cat = build_streak_catalog(corpus)
    ok = tot = 0
    for s in corpus:
        lab = streak_from_filename(s.path.name)
        r = read_streak(s.image, cat)
        for i in range(3):
            if lab[i] is not None and r.known[i]:
                tot += 1
                ok += r.digits[i] == lab[i]
    # In-sample confident accuracy is ~95%; the tracker tolerates the rest. Guard
    # against a regression that would break the monotonic reconciliation.
    assert tot > 0 and ok / tot >= 0.90, f"{ok}/{tot}"


# ─── stateful tracker logic ─────────────────────────────────────────────────────


def test_tracker_seeds_from_two_confident_wheels():
    tr = StreakTracker()
    # hundreds=1, tens=0 confident; units unknown → seed to the smallest matching = 100.
    assert tr.update(_raw(h=(1, True), t=(0, True), u=(0, False))) == 100
    assert tr.seen


def test_tracker_needs_two_wheels_to_seed():
    tr = StreakTracker()
    assert tr.update(_raw(h=(1, True))) == 0  # one confident wheel is not enough
    assert not tr.seen


def test_tracker_holds_when_unreadable():
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True)))          # seed 100
    assert tr.update(_raw()) == 100                     # all wheels unreadable → hold


def test_tracker_units_is_immediate():
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))   # seed 100
    # units debounce is 1 → a confident units read applies at once.
    assert tr.update(_raw(h=(1, True), t=(0, True), u=(5, True))) == 105


def test_tracker_debounces_tens_misread():
    # A single-frame tens misread must not move the value (the 0↔8 aliasing flip).
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))   # 100
    assert tr.update(_raw(h=(1, True), t=(8, True), u=(0, True))) == 100  # blip ignored
    assert tr.update(_raw(h=(1, True), t=(0, True), u=(0, True))) == 100  # back to true


def test_tracker_flicker_never_commits():
    # Alternating 0,8,0,8 on the tens must never commit the 8 (debounce resets on the 0).
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))
    for t in (8, 0, 8, 0, 8):
        tr.update(_raw(h=(1, True), t=(t, True), u=(0, True)))
    assert tr.val == 100


def test_tracker_commits_sustained_tens_change():
    # A real tens change commits after debounce (2 confident reads); the rolled units
    # (unknown) resets to 0 on the carry.
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(1, True), u=(9, True)))   # 119
    assert tr.update(_raw(h=(1, True), t=(2, True))) == 119  # 1st read of change → held
    assert tr.update(_raw(h=(1, True), t=(2, True))) == 120  # confirmed → carry, units 0


def test_tracker_no_lock_corrects_downward():
    # The lock bug: a value stuck high must be pullable back down by a sustained read.
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(4, True), u=(0, True)))   # seed 140 (as if misread-high)
    tr.update(_raw(h=(1, True), t=(1, True), u=(0, True)))   # 1st read of lower tens
    assert tr.update(_raw(h=(1, True), t=(1, True), u=(0, True))) == 110  # confirmed down


def test_tracker_clamp_rejects_implausible_jump():
    # A carry-roll misread that clears debounce (hundreds reads 9 for 2 frames) would
    # commit 900 from 90 -- a +810 jump the streak can't make. The clamp rejects it.
    tr = StreakTracker()
    tr.update(_raw(h=(0, True), t=(9, True), u=(0, True)))   # seed 90
    tr.update(_raw(h=(9, True), t=(0, True), u=(0, True)))   # 1st read (pending)
    assert tr.update(_raw(h=(9, True), t=(0, True), u=(0, True))) == 90  # 2nd would be 900 → rejected


def test_tracker_resets_when_absent():
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))   # seed 100
    assert tr.update(_raw(present=False)) == 0   # odometer gone → reset immediately
    assert tr.val == 0 and not tr.seen
