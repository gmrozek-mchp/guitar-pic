from __future__ import annotations

from gameplay.evaluate import song_eval
from gameplay.metadata import song_from_filename
from gameplay.songselect import build_song_catalog, read_song


def test_song_from_filename():
    assert song_from_filename("song_select__04_rock_and_roll_all_nite.png") == ("main", 4, "rock_and_roll_all_nite")
    assert song_from_filename("song_select__bonus_07_generation_rock.png") == ("bonus", 7, "generation_rock")
    assert song_from_filename("song_select__00_slow_ride.png") == ("main", 0, "slow_ride")
    assert song_from_filename("main_menu__career.png") is None


def test_catalog_has_all_64(corpus):
    cat = build_song_catalog(corpus)
    assert len(cat.templates) == 64
    assert sum(t.setlist == "main" for t in cat.templates) == 39
    assert sum(t.setlist == "bonus" for t in cat.templates) == 25


def test_song_match_clean_and_slop(corpus):
    res = song_eval(corpus)
    assert res.n_total == 64
    assert res.song_acc == 1.0, res.failures  # every song identified (match-all)
    assert res.margin_min > 0
    assert res.slop_acc >= 0.95, res.slop_acc


def test_first_song_uses_lower_roi(corpus):
    # Slow Ride / Avalancha are the per-setlist first songs (lower slot).
    cat = build_song_catalog(corpus)
    firsts = {(t.setlist, t.song_id): t.roi_kind for t in cat.templates if t.index == 0}
    assert firsts[("main", "slow_ride")] == "first"
    assert firsts[("bonus", "avalancha")] == "first"


def test_match_all_yields_correct_setlist(corpus):
    # Without being told the setlist, the match places each song in the right list.
    cat = build_song_catalog(corpus)
    for s in corpus:
        parsed = song_from_filename(s.path.name)
        if parsed is None:
            continue
        setlist, _i, song_id = parsed
        r = read_song(s.image, cat)
        assert (r.setlist, r.song_id) == (setlist, song_id), s.path.name
