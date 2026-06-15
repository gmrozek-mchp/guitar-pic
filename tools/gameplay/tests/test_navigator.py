from __future__ import annotations

import pytest

from gameplay.navigator import NavController, NavError, plan_practice_run, plan_goto
from gameplay.simgame import SimActuator, SimConfig, SimGame, SimObserver


def _run(cfg: SimConfig, song=19, difficulty="hard", part="lead"):
    game = SimGame("main_menu", cfg)
    ctrl = NavController(SimObserver(game), SimActuator(game))
    end = ctrl.run(plan_practice_run(song, difficulty, part))
    return game, ctrl, end


def test_plan_matches_nav_doc_path():
    steps = plan_practice_run(5, "hard").steps
    assert [s.expected_from for s in steps] == [
        "main_menu", "training_menu", "song_select", "part_select",
        "difficulty_select", "section_select", "speed_select", "loading",
    ]
    assert steps[-1].expected_to == "in_song"


def test_practice_run_reaches_in_song():
    game, _ctrl, end = _run(SimConfig())
    assert end.screen == "in_song"
    assert game.state.chosen["song"] == 19
    assert game.state.chosen["difficulty"] == 2  # hard
    assert game.state.chosen["section"] == 0  # FULL SONG
    assert game.state.chosen["speed"] == 0  # FULL SPEED
    assert game.state.chosen["part"] == "lead"


def test_part_select_absent_is_skipped():
    game, ctrl, end = _run(SimConfig(part_present=False))
    assert end.screen == "in_song"
    assert "part" not in game.state.chosen  # never visited part_select
    assert not any("select lead" in t for t in ctrl.trace)


def test_sticky_defaults_handled():
    # Cursors don't enter at 0; selection-by-delta and saturate must still land right.
    cfg = SimConfig(sticky={"difficulty_select": 3, "section_select": 5, "speed_select": 2, "song_select": 7})
    game, _ctrl, end = _run(cfg, difficulty="medium")
    assert end.screen == "in_song"
    assert game.state.chosen["difficulty"] == 1  # medium, despite entering at 3
    assert game.state.chosen["section"] == 0  # saturated to FULL SONG from 5
    assert game.state.chosen["speed"] == 0


def test_recovers_from_offplan_misfire():
    # First GREEN off difficulty mis-lands on tutorials_menu (off the practice path);
    # the controller must RED back to a known screen and re-descend.
    game, ctrl, end = _run(SimConfig(misfire=("difficulty_select", "tutorials_menu")))
    assert end.screen == "in_song"
    assert any("recover" in t for t in ctrl.trace)


def test_saturation_has_no_blind_count():
    # With a deep sticky section default, saturate must still reach the top (0).
    game, _ctrl, end = _run(SimConfig(section_count=12, sticky={"section_select": 11}))
    assert end.screen == "in_song"
    assert game.state.chosen["section"] == 0


def test_goto_plan_runs():
    game = SimGame("main_menu")
    ctrl = NavController(SimObserver(game), SimActuator(game))
    end = ctrl.run(plan_goto("main_menu", "song_select"))
    assert end.screen == "song_select"


def test_unrecoverable_raises():
    # Goal unreachable from a dead end with the recovery budget -> clear error.
    game = SimGame("in_song")
    ctrl = NavController(SimObserver(game), SimActuator(game))
    with pytest.raises(NavError):
        ctrl.run(plan_goto("main_menu", "training_menu"))
