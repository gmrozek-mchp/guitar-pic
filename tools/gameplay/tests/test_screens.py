from __future__ import annotations

import pytest

from gameplay.corpus import corpus_dir
from gameplay.screens import SCREEN_IDS, screen_id_for_filename


def test_renamed_prefixes_map_to_nav_doc_names():
    assert screen_id_for_filename("difficulty__hard.png") == "difficulty_select"
    assert screen_id_for_filename("speed__slow.png") == "speed_select"


def test_song_select_covers_main_and_bonus():
    assert screen_id_for_filename("song_select__00_slow_ride.png") == "song_select"
    assert screen_id_for_filename("song_select__bonus_24_through_the_fire_and_flames.png") == "song_select"


def test_single_shot_screen_without_suffix():
    assert screen_id_for_filename("loading.png") == "loading"


def test_unknown_prefix_raises():
    with pytest.raises(ValueError):
        screen_id_for_filename("totally_made_up__x.png")


def test_every_corpus_file_maps_to_a_known_id():
    files = sorted(corpus_dir().glob("*.png"))
    assert files, "corpus directory has no PNGs"
    for f in files:
        assert screen_id_for_filename(f.name) in SCREEN_IDS
