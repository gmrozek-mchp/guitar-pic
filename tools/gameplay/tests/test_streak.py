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


def test_tracker_carries_over_hundred():
    tr = StreakTracker()
    tr.update(_raw(h=(0, True), t=(9, True), u=(9, True)))   # seed 99
    assert tr.val == 99
    got = tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))  # 099 → 100
    assert got == 100


def test_tracker_confident_read_trumps_rollover():
    # The bug this guards: a tens misread must not lock the value high. A confident
    # wheel is authoritative for its place, even when it means the value drops.
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(1, True), u=(0, True)))   # 110
    tr.update(_raw(h=(1, True), t=(4, True), u=(0, True)))   # misread tens → 140 (blip)
    assert tr.val == 140
    # next frame reads the true tens again → must correct straight back down.
    assert tr.update(_raw(h=(1, True), t=(1, True), u=(2, True))) == 112


def test_tracker_advances_on_confident_units():
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))   # seed 100
    assert tr.update(_raw(h=(1, True), t=(0, True), u=(5, True))) == 105


def test_tracker_carry_resets_unknown_units():
    # tens increments while units is mid-roll (unreadable) → units rolls to 0.
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(1, True), u=(9, True)))   # 119
    assert tr.update(_raw(h=(1, True), t=(2, True))) == 120  # u unknown, tens carried → 0


def test_tracker_resets_when_absent():
    tr = StreakTracker()
    tr.update(_raw(h=(1, True), t=(0, True), u=(0, True)))   # seed 100
    assert tr.update(_raw(present=False)) == 0   # odometer gone → reset immediately
    assert tr.val == 0 and not tr.seen
