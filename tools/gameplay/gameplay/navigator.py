"""Navigator (M10): plan high-level verbs and execute them closed-loop.

The navigator turns a verb ("practice-run song i on difficulty d, LEAD") into a
`Plan` of `Step`s along the `navgraph`, then drives it through an `Observer` +
`Actuator` — the firmware seam: offline these are backed by `simgame`, on the
device by the CV observer (slices 1–3) and the fretboard link.

Control is **closed-loop, observation-driven** (gh3_navigation.md's load-bearing
rule): each iteration observes the current screen and runs whichever plan step
matches it. That single rule gives three behaviours for free:
- normal progress — the screen after a step matches the next step;
- skip — if `part_select` is absent, the observed `difficulty_select` matches a
  later step and the part step is simply never run;
- recovery — an unexpected screen matches no step, so we press RED to back up
  until a known screen reappears, then resume (re-descending if needed).

Selections are resolved from the *observed* current selection (strum the signed
delta), so a sticky/non-zero default doesn't desync us; FULL SONG / FULL SPEED
strum up until the selection stops moving rather than a blind count.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from .metadata import MENU_LAYOUTS
from .navgraph import (
    Action,
    MenuInput,
    Press,
    SaturateTop,
    SelectIndex,
    SelectItem,
    SelectSong,
    WaitFor,
    shortest_path,
)

_MAX_ITERS = 60       # global step budget for a run
_MAX_RECOVER = 12     # consecutive RED presses before giving up
_MAX_MOVE = 64        # strum guard for a single selection move / saturation


@dataclass(frozen=True)
class ObservedState:
    screen: str
    selection: int | None = None


class Observer(Protocol):
    def observe(self) -> ObservedState: ...


class Actuator(Protocol):
    def send(self, inputs: list[MenuInput]) -> None: ...


@dataclass(frozen=True)
class Step:
    expected_from: str
    action: Action
    expected_to: str
    desc: str = ""


@dataclass
class Plan:
    steps: list[Step]
    goal: str  # the screen that means "done"


class NavError(Exception):
    pass


# ─── Planning ──────────────────────────────────────────────────────────────────


def plan_practice_run(song_index: int, difficulty: str, part: str = "lead") -> Plan:
    """The nav-doc worked path: main_menu -> … -> in_song, for a main-setlist song."""
    if difficulty not in MENU_LAYOUTS["difficulty_select"].items:
        raise ValueError(f"unknown difficulty {difficulty!r}")
    if part not in MENU_LAYOUTS["part_select"].items:
        raise ValueError(f"unknown part {part!r}")
    steps = [
        Step("main_menu", SelectItem("training"), "training_menu", "open TRAINING"),
        Step("training_menu", SelectItem("practice"), "song_select", "open PRACTICE"),
        Step("song_select", SelectIndex(song_index), "part_select", f"select song #{song_index}"),
        Step("part_select", SelectItem(part), "difficulty_select", f"select {part} (skipped if absent)"),
        Step("difficulty_select", SelectItem(difficulty), "section_select", f"select {difficulty}"),
        Step("section_select", SaturateTop(), "speed_select", "FULL SONG (saturate top)"),
        Step("speed_select", SaturateTop(), "loading", "FULL SPEED (saturate top)"),
        Step("loading", WaitFor("in_song"), "in_song", "wait for gameplay"),
    ]
    return Plan(steps=steps, goal="in_song")


def plan_goto(start: str, target: str) -> Plan:
    """Generic navigation / recovery target via the graph's concrete edges."""
    path = shortest_path(start, target)
    if path is None:
        raise NavError(f"no route {start} -> {target}")
    steps = [Step(s.frm, s.action, s.to, f"{s.frm}->{s.to}") for s in path]
    return Plan(steps=steps, goal=target)


# ─── Closed-loop controller ─────────────────────────────────────────────────────


@dataclass
class NavController:
    observer: Observer
    actuator: Actuator
    trace: list[str] = field(default_factory=list)

    def _send(self, *inputs: MenuInput) -> None:
        self.actuator.send(list(inputs))

    def _step_for(self, plan: Plan, screen: str) -> Step | None:
        # Each screen appears at most once as an expected_from in a linear plan.
        for step in plan.steps:
            if step.expected_from == screen:
                return step
        return None

    def run(self, plan: Plan) -> ObservedState:
        recover = 0
        for _ in range(_MAX_ITERS):
            obs = self.observer.observe()
            if obs.screen == plan.goal:
                self.trace.append(f"reached {obs.screen}")
                return obs
            step = self._step_for(plan, obs.screen)
            if step is not None:
                self.trace.append(f"{obs.screen}: {step.desc or type(step.action).__name__}")
                self._execute(step.action)
                recover = 0
            else:
                # Unknown / off-plan screen: back up one level and re-evaluate.
                recover += 1
                if recover > _MAX_RECOVER:
                    raise NavError(f"cannot recover from {obs.screen!r} toward {plan.goal!r}")
                self.trace.append(f"{obs.screen}: recover (RED)")
                self._send(MenuInput.RED)
        raise NavError(f"step budget exhausted before reaching {plan.goal!r}")

    # ── action execution ──────────────────────────────────────────────────────
    def _execute(self, action: Action) -> None:
        if isinstance(action, Press):
            self._send(action.input)
        elif isinstance(action, SelectItem):
            screen = self.observer.observe().screen
            self._select_to(MENU_LAYOUTS[screen].index_of(action.item))
        elif isinstance(action, SelectIndex):
            self._select_to(action.index)
        elif isinstance(action, SaturateTop):
            self._saturate_top()
        elif isinstance(action, WaitFor):
            self._wait_for(action.screen)
        elif isinstance(action, SelectSong):  # pragma: no cover - planning uses SelectIndex
            raise NavError("SelectSong needs a concrete index (use SelectIndex)")

    def _select_to(self, target: int) -> None:
        """Move the cursor to an absolute index from wherever it currently is."""
        cur = self.observer.observe().selection
        if cur is None:
            raise NavError("selection unreadable; cannot select by index")
        delta = target - cur
        move = MenuInput.STRUM_DOWN if delta > 0 else MenuInput.STRUM_UP
        for _ in range(min(abs(delta), _MAX_MOVE)):
            self._send(move)
        self._send(MenuInput.GREEN)

    def _saturate_top(self) -> None:
        """Strum up until the selection stops moving (top), then GREEN — robust to
        a sticky entry position; no blind strum count."""
        prev = None
        for _ in range(_MAX_MOVE):
            cur = self.observer.observe().selection
            if cur == 0 or cur == prev:
                break
            prev = cur
            self._send(MenuInput.STRUM_UP)
        self._send(MenuInput.GREEN)

    def _wait_for(self, screen: str) -> None:
        for _ in range(_MAX_ITERS):
            if self.observer.observe().screen == screen:
                return
        raise NavError(f"timed out waiting for {screen!r}")
