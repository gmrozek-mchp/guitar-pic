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
        band=(120, 156, 280, 331),
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


# ─── song_select (fixed-slot) ──────────────────────────────────────────────────
#
# The selected song sits in a fixed highlight slot while the list scrolls under
# it, with one exception per setlist: the *first* song (Slow Ride on main,
# Avalancha on bonus) sits one row lower, because the list can't scroll up past
# the top. So 38/39 main + 24/25 bonus use SONG_SLOT_ROI; each setlist's song 0
# uses SONG_FIRST_ROI. Both ROIs cover the highlighted *title* line only (the
# selected title glyphs, right of the album-art thumbnail), which we match
# (bitmap, not OCR) against the per-song templates.
#
# ROIs in canonical 720x480 space, registered on the selected title: normal songs
# render the title at y≈246, the first song ~45 px lower at y≈291 (measured across
# the corpus). x0 starts right of the album art so only text is sampled. See
# docs/journal.md.
SONG_SLOT_ROI = (183, 233, 385, 266)
SONG_FIRST_ROI = (183, 282, 385, 315)

# Which setlist is active is read from the *page background colour*, not the tabs:
# the "setlist"/"bonus" tabs are only on screen when the first song is selected
# (they scroll off for song 2+), but the page colour is always visible — the main
# setlist's parchment is yellow, the bonus page is whiter. A large background ROI
# (median is robust to the song text over it); warmth = R - B separates them
# (offset-invariant, gain-preserving, so it survives the analog slop).
SETLIST_BG_ROI = (60, 60, 330, 300)


def song_from_filename(filename: str) -> tuple[str, int, str] | None:
    """Parse a song_select corpus filename into (setlist, index, song_id).

    `song_select__04_rock_and_roll_all_nite.png` -> ("main", 4, "rock_and_roll_all_nite")
    `song_select__bonus_07_generation_rock.png`  -> ("bonus", 7, "generation_rock")
    Returns None for non-song_select filenames.
    """
    sel = selected_item_from_filename(filename)
    if sel is None:
        return None
    setlist = "main"
    if sel.startswith("bonus_"):
        setlist = "bonus"
        sel = sel[len("bonus_") :]
    head, _, song_id = sel.partition("_")
    if not head.isdigit() or not song_id:
        return None
    return setlist, int(head), song_id


# ─── in-song score (per-digit glyph OCR) ───────────────────────────────────────
#
# The score is an *open-ended* value, so unlike the menu/song readers it needs
# per-digit glyph recognition rather than a whole-field template match. The
# training font is white and *proportional* (not tabular — a `1` is narrower than
# an `8`, so digit x-positions shift with the value; verified on a 6549-frame
# capture). The digits are cleanly gap-separated, so they are segmented by their
# ink (gaps between digits) rather than a fixed grid, then each is matched against
# 0-9. Per gameplay mode, because the font/box differs (training = white
# proportional; career = green segmented, added later).
#
# The digit search band, relative to the chrome-registered block: the glyph rows,
# spanning the full box interior so up to 6 digits fit (score can exceed 99999).
# Canonical 720x480 space. See docs/journal.md.
SCORE_DIGIT_BAND: dict[str, tuple[int, int, int, int]] = {
    "training": (122, 316, 204, 332),
}

# The whole scoring block. This is the marvin-perf capture region *and* the
# registration fiducial: its chrome (the ornate box frame, inner panel texture,
# and medallion ring) is static — independent of the score digits and identical
# in training and career (verified: both modes' chrome registers to the same
# position) — so the block is located once by matching that chrome, and the digit
# cells sit at fixed offsets inside it. Fits both modes' full scoring display
# (score + multiplier + streak). Canonical 720x480 space.
SCORE_BLOCK_ROI = (114, 309, 210, 414)
# Inner box rectangle whose static chrome drives registration (the mask keeps the
# low-variation chrome pixels inside this rect, excluding the digit/multiplier/
# medallion-interior regions that change). Canonical 720x480 space.
SCORE_CHROME_BOX = (127, 311, 190, 397)

# ─── score multiplier (colour-count classifier) ────────────────────────────────
#
# The multiplier glyph in the medallion has a fixed colour per value: 2x = gold,
# 3x = green, 4x = purple/magenta; 1x shows no digit (the dim portrait). So it's
# read by *colour*, not shape — count bright, saturated purple/green/yellow pixels
# in a small patch over the digit and take the argmax (or 1x if none clears a
# floor). A 16x24 patch is enough (we need only the hue), ~6x smaller than the
# glyph. Mode-independent (colours identical in training/career). 720x480 space.
SCORE_MULT_ROI = (164, 367, 180, 391)
SCORE_MULT_BRIGHT_MIN = 110   # a pixel counts only if max(R,G,B) exceeds this
SCORE_MULT_SAT_MIN = 40       # ...and (max-min) exceeds this (saturated)
SCORE_MULT_MIN_COUNT = 30     # fewer than this of the winning colour => 1x


def score_from_filename(filename: str) -> tuple[str, int] | None:
    """Parse a score corpus filename into (mode, value).

    `score__training__010584__snap0121.png` -> ("training", 10584).
    Returns None for non-score filenames.
    """
    stem = filename.rsplit("/", 1)[-1]
    if stem.endswith(".png"):
        stem = stem[: -len(".png")]
    parts = stem.split("__")
    if len(parts) < 3 or parts[0] != "score" or not parts[2].isdigit():
        return None
    return parts[1], int(parts[2])


# ─── note-streak counter (3-tumbler odometer, dual polarity + monotonic tracker) ─
#
# The streak counter is a mechanical odometer (a note icon + 3 digit tumblers) in
# the scoring block, right of the multiplier medallion. It only appears once the
# streak is ~25+, and the wheels *roll* into place — the units constantly, the
# tens/hundreds at carries — so any single frame can show one or more mid-roll
# (unreadable) digits. Each place is read independently at a fixed cell and the
# noisy per-frame reads are reconciled by a stateful tracker that assumes the count
# is monotonic increasing within a run (see StreakTracker).
#
# Polarity differs by wheel: the units is the *highlighted* wheel — a dark digit on
# a light tumbler — while the tens/hundreds are white-on-dark like the score. So
# each cell extracts ink by its polarity (bright pixels vs dark pixels), then the
# coverage mask is matched against a per-polarity bank of 0-9 templates.
#
# Presence ("odometer settled at its locked position") is detected from the note
# icon, NOT from the digits: the whole counter *slides in from the bottom, overshoots,
# and bounces down* to its final spot, and mid-slide the digit cells are misaligned
# (reading them gives garbage). The fixed gold note glyph at the odometer's left is
# a position-sensitive fiducial — its coverage matches the locked template only when
# the counter is settled (absent → dark; sliding → glyph off-position). So we only
# read digits when the note-icon match clears the threshold; otherwise the streak is
# treated as not-shown (→ 0). (score-0335.png is the reference locked frame.)
#
# Cells are in canonical 720x480 space, fixed offsets inside SCORE_BLOCK_ROI
# (origin x0=114, y0=309): (x0, y0, x1, y1). Measured on the 6549-frame capture.
STREAK_NOTE_CELL = (150, 392, 160, 407)  # gold note-icon fiducial (locked-position detector)
STREAK_CELL_H = (165, 392, 173, 407)   # hundreds — white-on-dark
STREAK_CELL_T = (179, 392, 187, 407)   # tens     — white-on-dark
STREAK_CELL_U = (194, 392, 202, 407)   # units    — dark-on-light (highlighted wheel)

STREAK_GLYPH_ROWS = 16       # canonical glyph grid (each cell's ink bbox resized to this)
STREAK_GLYPH_COLS = 10
STREAK_INK_NUM = 1           # relative ink threshold within a cell = num/den of the
STREAK_INK_DEN = 2           # min..max luma range (1/2 = midpoint; gain/offset robust)
STREAK_NOTE_MAX_SAD = 2000   # note-icon coverage L1 below this => odometer locked/settled (present)
STREAK_UNK_DIST = 9500       # per-cell best L1 above this => digit unreadable (rolling)
STREAK_UNK_MARGIN = 1200     # runner-up gap below this => digit unreadable (ambiguous)
# Tracker debounce: consecutive confident reads a *changed* digit needs before it
# commits, per place [hundreds, tens, units]. The slow wheels (h, t) require 2 — that
# kills a single-frame misread (e.g. the tens 0↔8 aliasing flip) without locking,
# since a sustained real change still commits. The units wheel rolls fast, so it is
# immediate (1) — debouncing it would freeze it.
STREAK_DEBOUNCE = (2, 2, 1)
# Tracker plausibility clamp: reject a committed per-frame value change larger than
# this. A streak can't gain ~50 in one ~3 Hz poll, so a jump this big is a misread —
# e.g. the hundreds wheel read mid-roll during a carry, which debounce alone can let
# through if it persists 2 frames. Symmetric (unlike a forward-only window), so it
# corrects downward and never locks; the seed bypasses it, so a real reappearance
# still jumps straight to the value.
STREAK_MAX_STEP = 50
# Tracker units-wrap carry: a confident units read that dropped by at least this
# much (9→0-ish) is a wheel wrap → step the tens. Large enough to ignore a small
# units misread, small enough to catch a wrap even if the last units seen was ~5.
STREAK_WRAP_MIN = 5


def streak_from_filename(filename: str) -> tuple[int | None, int | None, int | None] | None:
    """Parse a streak corpus filename into (hundreds, tens, units) digits.

    `streak__101__cap1623.png` -> (1, 0, 1). A place is `None` when its label is
    `x` (the wheel was mid-roll / unreadable when the frame was captured, so it has
    no ground-truth digit). Returns None for non-streak filenames.
    """
    stem = filename.rsplit("/", 1)[-1]
    if stem.endswith(".png"):
        stem = stem[: -len(".png")]
    parts = stem.split("__")
    if len(parts) < 2 or parts[0] != "streak" or len(parts[1]) != 3:
        return None
    return tuple(None if ch == "x" else int(ch) for ch in parts[1])  # type: ignore[return-value]
