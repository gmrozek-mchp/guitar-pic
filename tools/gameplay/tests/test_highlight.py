from __future__ import annotations

import numpy as np

from gameplay.evaluate import selection_eval
from gameplay.highlight import build_selection_calibration, cell_colors, read_selection
from gameplay.metadata import MENU_LAYOUTS, selected_item_from_filename


def test_every_static_list_screen_is_calibratable(corpus):
    cal = build_selection_calibration(corpus)
    for sid, layout in MENU_LAYOUTS.items():
        assert sid in cal.baselines, sid
        assert cal.baselines[sid].shape == (layout.count, 3)


def test_clean_selection_accuracy(corpus):
    # Every labelled static-list frame's selection is read correctly.
    res = selection_eval(corpus)
    assert res.n_total >= 33
    assert res.accuracy == 1.0, res.failures


def test_selection_robust_to_analog_slop(corpus):
    res = selection_eval(corpus, perturb_envelope=True)
    assert res.accuracy >= 0.95, res.per_screen


def test_two_item_screens_disambiguated(corpus):
    # The K=2 screens (where a median baseline is degenerate) must still resolve.
    cal = build_selection_calibration(corpus)
    for sid in ("quit_confirm", "part_select", "training_menu"):
        layout = MENU_LAYOUTS[sid]
        preds = {}
        for s in corpus:
            if s.screen_id != sid:
                continue
            item = selected_item_from_filename(s.path.name)
            preds[item] = read_selection(s.image, layout, cal).item
        # each item read back as itself => the two cells are distinguished
        assert all(k == v for k, v in preds.items()), (sid, preds)


def test_cell_colors_normalized_zero_mean(corpus):
    layout = MENU_LAYOUTS["speed_select"]
    img = next(s.image for s in corpus if s.screen_id == "speed_select")
    cc = cell_colors(img, layout)
    assert np.allclose(cc.mean(axis=0), 0.0, atol=1e-9)  # per-frame mean removed
