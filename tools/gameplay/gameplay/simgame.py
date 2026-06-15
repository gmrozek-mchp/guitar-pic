"""Simulated GH3 menu — the offline test oracle for the navigator.

`SimGame` implements the menu graph (`navgraph`) forward as a state machine, so
the closed-loop controller can be driven end-to-end without a console. It is
deliberately configurable with the conditions that make closed-loop control
non-trivial, so the controller's handling of them is actually tested:

- `part_present` — whether `part_select` appears after a song (it's song-dependent
  and sometimes skipped); exercises the skip branch.
- `sticky` — a screen's cursor doesn't always enter at index 0; exercises reading
  the current selection before moving (and saturating to the top).
- `misfire` — a one-shot wrong landing on a GREEN, to exercise RED recovery.

`SimObserver` / `SimActuator` adapt it to the navigator's Observer/Actuator
protocols. Cursor moves are clamped at the list ends (no wrap, per the nav-doc
assumption), so strum-up saturates at the top.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .metadata import MENU_LAYOUTS
from .navgraph import MenuInput

# Static-list sizes come from the menu metadata; the scrolling/transient screens
# get sizes from the sim's song/section parameters or are sizeless.
_STATIC_SIZES = {sid: lay.count for sid, lay in MENU_LAYOUTS.items()}
_LOADING_TICKS = 2  # observes spent in `loading` before it advances to `in_song`


@dataclass
class SimConfig:
    setlist: str = "main"
    song_count: int = 39  # main setlist; bonus is 25
    section_count: int = 8  # FULL SONG = index 0
    part_present: bool = True
    sticky: dict[str, int] = field(default_factory=dict)  # screen -> entry selection
    misfire: tuple[str, str] | None = None  # (screen, wrong_to): first GREEN off `screen` mislands


@dataclass
class SimState:
    screen: str
    selection: int
    chosen: dict[str, int | str] = field(default_factory=dict)


class SimGame:
    def __init__(self, start: str = "main_menu", config: SimConfig | None = None):
        self.cfg = config or SimConfig()
        self._loading = 0
        self._misfired = False
        self.state = SimState(screen=start, selection=self._entry_selection(start))

    # ── geometry ────────────────────────────────────────────────────────────
    def _size(self, screen: str) -> int:
        if screen == "song_select":
            return self.cfg.song_count
        if screen == "section_select":
            return self.cfg.section_count
        return _STATIC_SIZES.get(screen, 1)

    def _entry_selection(self, screen: str) -> int:
        return min(self.cfg.sticky.get(screen, 0), max(0, self._size(screen) - 1))

    def _enter(self, screen: str, **chosen: int | str) -> None:
        prev = self.state.chosen
        self.state = SimState(screen, self._entry_selection(screen), {**prev, **chosen})
        if screen == "loading":
            self._loading = _LOADING_TICKS

    # ── observation (advances `loading` as time passes) ───────────────────────
    def observe(self) -> SimState:
        if self.state.screen == "loading":
            if self._loading > 0:
                self._loading -= 1
            if self._loading == 0:
                self._enter("in_song")
        return self.state

    # ── input application ─────────────────────────────────────────────────────
    def apply(self, inp: MenuInput) -> None:
        s = self.state
        if inp is MenuInput.STRUM_DOWN:
            s.selection = min(s.selection + 1, self._size(s.screen) - 1)
        elif inp is MenuInput.STRUM_UP:
            s.selection = max(s.selection - 1, 0)
        elif inp is MenuInput.RED:
            from .navgraph import PARENT
            if s.screen in PARENT:
                self._enter(PARENT[s.screen])
        elif inp is MenuInput.PLUS:
            if s.screen == "in_song":
                self._enter("pause_menu")
        elif inp is MenuInput.GREEN:
            self._green()
        # BLUE/YELLOW (setlist toggle) omitted: song identity handled via match-all.

    def _green(self) -> None:
        s = self.state
        target = self._green_target(s.screen, s.selection)
        if target is None:
            return  # selecting an unmapped item is a no-op in the sim
        screen, chosen = target
        if self.cfg.misfire and not self._misfired and self.cfg.misfire[0] == s.screen:
            self._misfired = True
            self._enter(self.cfg.misfire[1])
            return
        self._enter(screen, **chosen)

    def _green_target(self, screen: str, sel: int) -> tuple[str, dict[str, int | str]] | None:
        if screen == "main_menu":
            return ("training_menu", {}) if sel == MENU_LAYOUTS["main_menu"].index_of("training") else None
        if screen == "training_menu":
            return ("song_select", {}) if sel == 1 else ("tutorials_menu", {})
        if screen == "song_select":
            nxt = "part_select" if self.cfg.part_present else "difficulty_select"
            return (nxt, {"song": sel, "setlist": self.cfg.setlist})
        if screen == "part_select":
            return ("difficulty_select", {"part": "lead" if sel == 0 else "rhythm"})
        if screen == "difficulty_select":
            return ("section_select", {"difficulty": sel})
        if screen == "section_select":
            return ("speed_select", {"section": sel})
        if screen == "speed_select":
            return ("loading", {"speed": sel})
        if screen == "pause_menu":
            return [
                ("in_song", {}), ("loading", {}), None, ("speed_select", {}),
                ("section_select", {}), ("song_select", {}), ("quit_confirm", {}),
            ][sel]
        if screen == "quit_confirm":
            return ("pause_menu", {}) if sel == 0 else ("main_menu", {})
        if screen == "practice_end_menu":
            return [("song_select", {}), ("loading", {}), ("speed_select", {}),
                    ("section_select", {}), ("main_menu", {})][sel]
        return None


@dataclass
class SimObserver:
    game: SimGame

    def observe(self):
        from .navigator import ObservedState
        s = self.game.observe()
        return ObservedState(screen=s.screen, selection=s.selection)


@dataclass
class SimActuator:
    game: SimGame

    def send(self, inputs):
        for inp in inputs:
            self.game.apply(inp)
