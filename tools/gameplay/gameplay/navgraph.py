"""GH3 menu graph as data — the spec §4.8.3 "menu graph" the navigator plans over.

Nodes are screen ids (`screens.SCREEN_IDS`); edges are transcribed from
`firmware/marvin/docs/gh3_navigation.md`. An edge carries the `Action` that
traverses it, of which there are a few kinds:

- `Press(input)`   — a single fixed input (RED back, PLUS pause, GREEN, …).
- `SelectItem(id)` — on a static-list screen, move the cursor to that item and
  GREEN (index resolved from `metadata.MENU_LAYOUTS`).
- `SelectSong()`   — parameterized (the song index is supplied when planning a
  practice run); not routed by the generic `shortest_path`.
- `SaturateTop()`  — strum to the top item and GREEN (FULL SONG / FULL SPEED).
- `WaitFor(screen)`— observe without input until the screen advances (loading).

`MenuInput` is symbolic here; mapping to fret/strum/`+` controller bits is a
firmware-port concern (the fretboard MCU doesn't distinguish menu from gameplay
inputs — gh3_navigation.md §Inputs).
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from enum import Enum


class MenuInput(Enum):
    GREEN = "green"        # confirm / enter
    RED = "red"            # back up one level
    STRUM_UP = "strum_up"  # move selection up
    STRUM_DOWN = "strum_down"  # move selection down
    BLUE = "blue"          # song_select: toggle to bonus setlist
    YELLOW = "yellow"      # song_select: back to main setlist
    PLUS = "plus"          # in_song: open pause menu


@dataclass(frozen=True)
class Press:
    input: MenuInput


@dataclass(frozen=True)
class SelectItem:
    item: str  # a static-list item id (see metadata.MENU_LAYOUTS)


@dataclass(frozen=True)
class SelectIndex:
    index: int  # select an absolute list index (e.g. a song ordinal), then GREEN


@dataclass(frozen=True)
class SelectSong:
    pass  # parameterized placeholder in the static graph; planning supplies SelectIndex


@dataclass(frozen=True)
class SaturateTop:
    pass  # strum up to the top item, then GREEN


@dataclass(frozen=True)
class WaitFor:
    screen: str


Action = Press | SelectItem | SelectIndex | SelectSong | SaturateTop | WaitFor

RED = Press(MenuInput.RED)

# Forward edges: from_screen -> {to_screen: Action}. Only the practice path and the
# pause/end overlays are modelled (other main-menu items are out of scope).
FORWARD: dict[str, dict[str, Action]] = {
    "main_menu": {"training_menu": SelectItem("training")},
    "training_menu": {
        "song_select": SelectItem("practice"),
        "tutorials_menu": SelectItem("tutorials"),  # out of scope; here for completeness
    },
    "song_select": {"part_select": SelectSong()},  # parameterized; may skip to difficulty_select
    "part_select": {"difficulty_select": SelectItem("lead")},
    "difficulty_select": {"section_select": SelectItem("easy")},  # difficulty supplied at plan time
    "section_select": {"speed_select": SaturateTop()},  # FULL SONG (top)
    "speed_select": {"loading": SaturateTop()},  # FULL SPEED (top)
    "loading": {"in_song": WaitFor("in_song")},
    "in_song": {"pause_menu": Press(MenuInput.PLUS)},
    "pause_menu": {
        "in_song": SelectItem("resume"),
        "loading": SelectItem("restart"),
        "speed_select": SelectItem("change_speed"),
        "section_select": SelectItem("change_section"),
        "song_select": SelectItem("new_song"),
        "quit_confirm": SelectItem("quit"),
    },
    "quit_confirm": {
        "pause_menu": SelectItem("cancel"),
        "main_menu": SelectItem("quit"),
    },
    "practice_end_menu": {
        "song_select": SelectItem("continue"),
        "loading": SelectItem("restart"),
        "speed_select": SelectItem("change_speed"),
        "section_select": SelectItem("change_section"),
        "main_menu": SelectItem("quit"),
    },
}

# RED backs up one level. Parents transcribed from the nav doc (back: RED).
PARENT: dict[str, str] = {
    "training_menu": "main_menu",
    "tutorials_menu": "training_menu",
    "song_select": "training_menu",
    "part_select": "song_select",
    "difficulty_select": "part_select",
    "section_select": "difficulty_select",
    "speed_select": "section_select",
    "quit_confirm": "pause_menu",
    "pause_menu": "in_song",
}


def ancestors(screen: str) -> list[str]:
    """Screens reachable by pressing RED repeatedly, nearest first."""
    out: list[str] = []
    cur = PARENT.get(screen)
    while cur is not None:
        out.append(cur)
        cur = PARENT.get(cur)
    return out


@dataclass(frozen=True)
class GraphStep:
    frm: str
    action: Action
    to: str


def shortest_path(start: str, goal: str) -> list[GraphStep] | None:
    """BFS shortest action path start -> goal over concrete edges.

    Concrete = fixed forward edges (Press / SelectItem / SaturateTop / WaitFor)
    plus RED-to-parent. Parameterized `SelectSong` edges are skipped — generic
    routing can't choose a song; the practice-run planner supplies that.
    """
    if start == goal:
        return []
    prev: dict[str, GraphStep] = {}
    seen = {start}
    q: deque[str] = deque([start])
    while q:
        cur = q.popleft()
        edges: list[GraphStep] = []
        for to, action in FORWARD.get(cur, {}).items():
            if not isinstance(action, SelectSong):
                edges.append(GraphStep(cur, action, to))
        parent = PARENT.get(cur)
        if parent is not None:
            edges.append(GraphStep(cur, RED, parent))
        for step in edges:
            if step.to in seen:
                continue
            seen.add(step.to)
            prev[step.to] = step
            if step.to == goal:
                path: list[GraphStep] = []
                node = goal
                while node != start:
                    s = prev[node]
                    path.append(s)
                    node = s.frm
                path.reverse()
                return path
            q.append(step.to)
    return None
