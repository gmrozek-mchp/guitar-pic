"""Per-screen menu metadata — the seed of the spec §4.8.3 menu tables.

For each static-list screen the navigator needs to act on, this records the
ordered item list (ids = the corpus filename suffixes, matching the index order
in `gh3_navigation.md`) and the on-screen geometry of the menu: a bounding band
plus the axis the items run along. The highlight reader divides the band into
`len(items)` equal cells and finds the lit one.

Scope today: fixed-item-count static lists. Deferred:
- `section_select` — item count is song-dependent (navigator just saturates to the
  top "FULL SONG"); doesn't fit a fixed-row-index model.
- `song_select` — fixed-slot/scrolling; "reading" the selected song is nearest-
  bitmap matching of the highlight slot against the known per-song templates, a
  separate concern from row reading (and distinct from char-level OCR, which is
  only needed later for open-ended values like scores).
"""

from __future__ import annotations

from dataclasses import dataclass

VERTICAL = "v"
HORIZONTAL = "h"


@dataclass(frozen=True)
class MenuLayout:
    screen_id: str
    items: tuple[str, ...]  # item ids (= filename suffixes) in screen order (top->bottom / left->right)
    band: tuple[int, int, int, int]  # (x0, y0, x1, y1) in canonical 720x480 space
    axis: str = VERTICAL

    @property
    def count(self) -> int:
        return len(self.items)

    def index_of(self, item_id: str) -> int:
        return self.items.index(item_id)


# Bands are in canonical 720x480 space, converged against the labelled corpus by
# auto-locating each selection's highlight (see docs/journal.md). `items` is in
# screen order (top->bottom, or left->right for horizontal), which is what the
# reader maps cells onto.
MENU_LAYOUTS: dict[str, MenuLayout] = {
    "main_menu": MenuLayout(
        "main_menu",
        ("career", "co_op_career", "quickplay", "multiplayer", "training", "options", "nintendo_wfc"),
        band=(349, 87, 574, 282),
    ),
    "difficulty_select": MenuLayout(
        "difficulty_select",
        ("easy", "medium", "hard", "expert"),
        band=(35, 158, 245, 310),
    ),
    "speed_select": MenuLayout(
        "speed_select",
        ("full_speed", "slow", "slower", "slowest"),
        band=(255, 198, 452, 324),
    ),
    "pause_menu": MenuLayout(
        "pause_menu",
        ("resume", "restart", "options", "change_speed", "change_section", "new_song", "quit"),
        band=(277, 156, 433, 322),
    ),
    "practice_end_menu": MenuLayout(
        "practice_end_menu",
        ("continue", "restart", "change_speed", "change_section", "quit"),
        band=(368, 115, 547, 217),
    ),
    "quit_confirm": MenuLayout(
        "quit_confirm",
        ("cancel", "quit"),
        band=(278, 330, 431, 384),
    ),
    "training_menu": MenuLayout(
        "training_menu",
        ("tutorials", "practice"),
        band=(285, 154, 429, 336),
        axis=HORIZONTAL,
    ),
    "part_select": MenuLayout(
        "part_select",
        ("lead", "rhythm"),
        band=(377, 185, 563, 249),
    ),
}


def static_list_screens() -> tuple[str, ...]:
    return tuple(MENU_LAYOUTS)


def selected_item_from_filename(filename: str) -> str | None:
    """The selected item id encoded in a corpus filename, or None.

    `main_menu__co_op_career.png` -> `co_op_career`. Returns None for filenames
    with no `__selection` suffix (single-shot screens) or screens not modelled here.
    """
    stem = filename.rsplit("/", 1)[-1]
    if stem.endswith(".png"):
        stem = stem[: -len(".png")]
    if "__" not in stem:
        return None
    return stem.split("__", 1)[1]
