"""Integration: the real observer (slices 1-3) drives the navigator.

A `CorpusObserver` uses the `SimGame` for menu *dynamics* but routes every
observation through a real corpus frame + the real vision stack (classifier +
highlight/song readers). In the default sim config the cursors enter at index 0,
so every state the controller observes during a practice run has a captured frame
— the whole closed loop runs on real screen readings, end to end.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from gameplay.classifier import build_templates, classify_image
from gameplay.evaluate import labelled_fps
from gameplay.fingerprint import FingerprintConfig
from gameplay.highlight import build_selection_calibration, read_selection
from gameplay.metadata import MENU_LAYOUTS, selected_item_from_filename, song_from_filename
from gameplay.navigator import NavController, ObservedState, plan_practice_run
from gameplay.simgame import SimActuator, SimConfig, SimGame, SimObserver
from gameplay.songselect import build_song_catalog, read_song


def _frame_index(corpus):
    """(screen, selection_index) -> corpus image, for the practice-run states."""
    idx = {}
    for s in corpus:
        sid = s.screen_id
        if sid in MENU_LAYOUTS:
            item = selected_item_from_filename(s.path.name)
            lay = MENU_LAYOUTS[sid]
            if item in lay.items:
                idx[(sid, lay.index_of(item))] = s.image
        elif sid == "song_select":
            parsed = song_from_filename(s.path.name)
            if parsed and parsed[0] == "main":
                idx[("song_select", parsed[1])] = s.image
        else:  # single-frame screens: loading, in_song, section_select, tutorials_menu
            idx.setdefault((sid, 0), s.image)
    return idx


@dataclass
class CorpusObserver:
    game: SimGame
    frames: dict
    templates: object
    calibration: object
    catalog: object
    fallbacks: int = 0
    real_reads: int = 0

    def observe(self) -> ObservedState:
        gt = self.game.observe()
        sel = gt.selection if gt.selection is not None else 0
        img = self.frames.get((gt.screen, sel))
        if img is None:  # uncaptured cursor position -> ground-truth fallback
            self.fallbacks += 1
            return ObservedState(gt.screen, gt.selection)
        self.real_reads += 1
        screen = classify_image(img, self.templates).screen_id
        read_sel = gt.selection
        if screen in MENU_LAYOUTS:
            read_sel = read_selection(img, MENU_LAYOUTS[screen], self.calibration).index
        elif screen == "song_select":
            read_sel = read_song(img, self.catalog).index
        return ObservedState(screen, read_sel)


def test_real_observer_drives_practice_run(corpus):
    cfg = FingerprintConfig()
    templates = build_templates(labelled_fps(corpus, cfg), cfg)
    calibration = build_selection_calibration(corpus)
    catalog = build_song_catalog(corpus)
    frames = _frame_index(corpus)

    game = SimGame("main_menu", SimConfig())  # cursors enter at 0
    obs = CorpusObserver(game, frames, templates, calibration, catalog)
    ctrl = NavController(obs, SimActuator(game))
    end = ctrl.run(plan_practice_run(19, "hard", "lead"))

    assert end.screen == "in_song"
    assert game.state.chosen["song"] == 19
    assert game.state.chosen["difficulty"] == 2
    assert obs.real_reads > 0
    assert obs.fallbacks == 0  # every observed state had a real frame


def test_observer_reads_each_static_state(corpus):
    """The observer's screen+selection readings match what the navigator expects."""
    cfg = FingerprintConfig()
    templates = build_templates(labelled_fps(corpus, cfg), cfg)
    calibration = build_selection_calibration(corpus)
    frames = _frame_index(corpus)
    for (screen, sel), img in frames.items():
        if screen not in MENU_LAYOUTS:
            continue
        assert classify_image(img, templates).screen_id == screen, (screen, sel)
        assert read_selection(img, MENU_LAYOUTS[screen], calibration).index == sel, (screen, sel)
