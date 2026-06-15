from __future__ import annotations

from gameplay import navgraph as ng
from gameplay.navgraph import SelectItem, SaturateTop, WaitFor, Press, MenuInput
from gameplay.metadata import MENU_LAYOUTS
from gameplay.screens import SCREEN_IDS


def test_edges_reference_known_screens():
    for frm, outs in ng.FORWARD.items():
        assert frm in SCREEN_IDS, frm
        for to in outs:
            assert to in SCREEN_IDS, to
    for child, parent in ng.PARENT.items():
        assert child in SCREEN_IDS and parent in SCREEN_IDS


def test_select_item_ids_exist_in_layouts():
    # Every SelectItem edge names an item that exists on its source screen's layout.
    for frm, outs in ng.FORWARD.items():
        for action in outs.values():
            if isinstance(action, SelectItem):
                assert frm in MENU_LAYOUTS, frm
                assert action.item in MENU_LAYOUTS[frm].items, (frm, action.item)


def test_shortest_path_main_to_song_select():
    path = ng.shortest_path("main_menu", "song_select")
    assert [s.to for s in path] == ["training_menu", "song_select"]
    assert all(isinstance(s.action, SelectItem) for s in path)


def test_shortest_path_uses_red_to_back_up():
    # From difficulty_select back to main_menu is pure RED climbing.
    path = ng.shortest_path("difficulty_select", "main_menu")
    assert path is not None
    assert all(s.action == Press(MenuInput.RED) for s in path)
    assert [s.to for s in path][-1] == "main_menu"


def test_ancestors_climb_to_root():
    assert ng.ancestors("speed_select")[-1] == "main_menu"
    assert ng.ancestors("main_menu") == []
