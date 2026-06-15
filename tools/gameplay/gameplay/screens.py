"""Canonical GH3 screen identifiers and the corpus filename -> id mapping.

Single source of truth aligning the prototype's screen ids with the screen names
in `firmware/marvin/docs/gh3_navigation.md`. The classifier emits one of these ids
(or the sentinel UNKNOWN); the firmware `gameplay_engine` port will reuse the same
names.

Corpus files are named `<prefix>__<selection>.png` (or just `<prefix>.png` for
single-shot screens). The screen *class* is the prefix; the selection suffix is
the highlighted item, which a later slice (the highlight reader) handles. Two
prefixes are renamed here to match the nav-doc node names.
"""

from __future__ import annotations

# Sentinel for "no recognized screen" — the navigator's recovery path keys off it.
UNKNOWN = "unknown"

# Canonical screen ids, matching the gh3_navigation.md catalog. Order is the
# nominal practice-run flow, then the overlays/dialogs.
SCREEN_IDS: tuple[str, ...] = (
    "main_menu",
    "training_menu",
    "tutorials_menu",
    "song_select",
    "part_select",
    "difficulty_select",
    "section_select",
    "speed_select",
    "loading",
    "in_song",
    "pause_menu",
    "quit_confirm",
    "practice_end_menu",
)

# Corpus filename prefixes whose canonical id differs from the prefix itself.
# Everything else maps by identity.
_PREFIX_RENAMES: dict[str, str] = {
    "difficulty": "difficulty_select",
    "speed": "speed_select",
}


def screen_id_for_filename(filename: str) -> str:
    """Map a corpus PNG filename to its canonical screen id.

    `main_menu__training.png` -> `main_menu`; `difficulty__hard.png` ->
    `difficulty_select`; `loading.png` -> `loading`. Raises ValueError if the
    resulting id isn't a known screen.
    """
    stem = filename.rsplit("/", 1)[-1]
    if stem.endswith(".png"):
        stem = stem[: -len(".png")]
    prefix = stem.split("__", 1)[0]
    screen_id = _PREFIX_RENAMES.get(prefix, prefix)
    if screen_id not in SCREEN_IDS:
        raise ValueError(f"{filename!r}: unknown screen prefix {prefix!r}")
    return screen_id
