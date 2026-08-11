"""Frame-rate end-of-song detector: is the results screen up?

The scoreboard-presence probe (present.py) answers "is this a gameplay screen"
well enough to start a run, but it is a masked SAD over a ~10k-pixel block, so
the observer only runs it on request and the controller polls it at 300 ms with a
3-sample confirm. That is ~1.0-1.35 s of actuation after a song ends, and the
notes still being strummed land on the results screen's menu.

This probe is the cheap counterpart, sized to run on *every* frame: 2 bright
boxes and 3 dark boxes, ~1400 integer luma reads, no scratch. It answers only one
question -- "has a results-screen-shaped frame appeared" -- and it is used as a
fast veto on actuation. It does not name the screen: `endlayout.py` does that,
because the probe fires on most menus too (see below).

Geometry. Both end layouts (practice_end_menu, faceoff_end_menu) are a magazine
spread over a collage, and *the collage is per-song*: Slow Ride gets bright sketch
paper, One gets dark red comic art. So is the left page (cover art), and so is
the decoration column down the right of the right page (doodled letters on one
magazine, a flame on another). What survives across magazines is the right page's
own furniture. The only part of it that is white in *both* layouts is the top
margin above the menu block -- faceoff has 3 menu items, practice has 5 plus an
"N OUT OF M" box, so everything below y~90 is menu text in one layout or the
other, and below that are results fields.

The dark reference is the black interior of the SELECT / UP-DOWN hint bar, which
is a UI overlay composited after the scene's colour grade -- it reads 2..5 on
every end frame measured, where the page white itself is graded per song (88 on
one magazine against 167 on another). It is also the fret-button row during play,
so it goes *bright* mid-song and drives the contrast further negative exactly
when a false positive would cost the most.

Decision is a contrast, `bright_i - median(dark) >= THRESH[i]`, counted, and the
screen is called at K of 2. A difference cancels the analog feed's offset exactly
and only scales with gain.

Boxes, not point lattices. The glyphs inside the hint bar are the most stable
thing on the screen (the UP/DOWN word box varies 1.2 luma across every end frame
we have), but their strokes are ~3 px wide, so point samples on them collapse
under the +/-2 px static capture offset -- 1 of 6 patches surviving at +2,+2. A
box mean barely moves when shifted, which is what makes the same regions usable.

Asymmetric positional tolerance, as before: the end side carries +/-2 px for a
static per-rig capture offset, the gameplay side the full +/-6 px envelope. The
shake comes from missing a note, so it only happens during gameplay; the results
screen is static. Applied symmetrically, the pairing this module is built on
fails outright.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

LUMA_B, LUMA_G, LUMA_R = 29, 150, 77   # == present._LUMA / gp_probes luma weights

# Frames the probe is defined against (canonical capture size).
CANON_W, CANON_H = 720, 480


@dataclass(frozen=True)
class Box:
    """A dense rectangle of luma samples, [x0, x1) x [y0, y1).

    Dense rather than a sampled lattice: the regions are small and the whole
    point is that a *mean over an area* survives a shift that moves individual
    samples off a glyph stroke or a page edge.
    """

    x0: int
    y0: int
    x1: int
    y1: int

    @property
    def pixels(self) -> int:
        return (self.x1 - self.x0) * (self.y1 - self.y0)


# Regions refused for *bright* boxes, as (x0, y0, x1, y1). These are the
# provenance of the table below -- re-picking boxes without them reintroduces the
# failures they encode. Each is a rule about what varies for reasons unrelated to
# which screen this is, not a measurement.
#
# Note what is deliberately absent: the blanket "where gameplay puts bright
# moving content" refusal the point-lattice version carried. The bright boxes are
# justified by measured gameplay contrast instead (see the PLAY column in the
# journal's table). What that measurement cannot cover is star power, a bright
# camera cut, or a venue whose backdrop is light behind the band -- 6 static
# gameplay frames cannot show any of them.
SELECTION_EXCLUSIONS: dict[str, tuple[tuple[int, int, int, int], ...]] = {
    # Black letterbox bars: clipped, so they track neither gain nor offset.
    "letterbox": ((0, 0, 720, 10), (0, 468, 720, 480)),
    # The collage the magazine sits on: per-song, and it is most of the frame.
    "per_song_collage": ((0, 0, 720, 36), (0, 36, 110, 480), (620, 36, 720, 480),
                         (0, 444, 720, 480)),
    # Left magazine page: cover art and the song/artist text change per song.
    "per_song_art": ((110, 36, 358, 418),),
    # Decoration column down the right page: doodled letters on Backwater Rocker,
    # a flame on Flaming Pick.
    "per_magazine_decoration": ((528, 52, 616, 410),),
    # Results fields, both layouts -- these move with the score/streak/notes-hit.
    "results_practice": ((366, 80, 532, 224), (390, 240, 472, 312), (524, 58, 602, 104)),
    "results_faceoff": ((366, 80, 516, 366),),
}

# Where dark boxes are allowed: the interiors of the two hint-bar pills. Scoped
# separately because the rules above are about the *magazine*, and the bar is a
# UI overlay on top of it -- the one thing on this screen the collage cannot
# reach. Glyph rects are the ink a dark box must clear.
HINT_BAR_PILLS: tuple[tuple[int, int, int, int], ...] = (
    (246, 414, 344, 442),
    (352, 414, 478, 442),
)
HINT_BAR_GLYPHS: tuple[tuple[int, int, int, int], ...] = (
    (258, 420, 272, 438),   # green button icon
    (276, 424, 332, 437),   # "SELECT"
    (364, 425, 392, 435),   # dash icon
    (396, 424, 466, 437),   # "UP/DOWN"
)


def excluded(x: int, y: int) -> str | None:
    """The bright-box exclusion zone containing (x, y), or None."""
    for name, rects in SELECTION_EXCLUSIONS.items():
        for x0, y0, x1, y1 in rects:
            if x0 <= x < x1 and y0 <= y < y1:
                return name
    return None


def _inside(b: Box, rect: tuple[int, int, int, int]) -> bool:
    x0, y0, x1, y1 = rect
    return b.x0 >= x0 and b.y0 >= y0 and b.x1 <= x1 and b.y1 <= y1


def _overlaps(b: Box, rect: tuple[int, int, int, int]) -> bool:
    x0, y0, x1, y1 = rect
    return b.x0 < x1 and x0 < b.x1 and b.y0 < y1 and y0 < b.y1


# Bright boxes: the right page's top margin, above the menu block. Split in two
# so one being stepped on does not lose the call. The band's right end reaches
# something bright during play (gameplay contrast up to 47 against these two's 5)
# and is left out.
BRIGHT: tuple[Box, ...] = (
    Box(369, 65, 398, 78),
    Box(398, 65, 428, 78),
)

# Dark reference: black interior of the two hint-bar pills, clear of the glyphs.
# Median of three so one box landing on something unexpected cannot drag it.
DARK: tuple[Box, ...] = (
    Box(248, 418, 258, 440),   # left of the green button icon
    Box(334, 418, 344, 440),   # right of "SELECT"
    Box(468, 418, 478, 440),   # right of "UP/DOWN"
)

# Per-box contrast threshold, midway between the worst end-screen contrast and
# the best gameplay contrast (see `calibrate`). Regenerate if the boxes change.
THRESH: tuple[int, ...] = (33, 37)

# How many of the bright boxes must clear their threshold. 1 of 2 is deliberate:
# a false positive costs the notes missed in one controller poll and self-clears,
# a false negative costs presses that can select RESTART or QUIT.
K_HITS = 1

# Consecutive frames required before acting. Two frames (~33 ms) costs nothing
# against the 300 ms poll it replaces and rejects a single-frame decode artefact.
CONFIRM_FRAMES = 2


def box_mean(image: np.ndarray, b: Box) -> int:
    """Integer mean luma over the box (floor, as the C does)."""
    px = image[b.y0:b.y1, b.x0:b.x1].astype(np.int32)
    lum = (LUMA_B * px[:, :, 0] + LUMA_G * px[:, :, 1] + LUMA_R * px[:, :, 2]) >> 8
    return int(lum.sum()) // b.pixels


def anchor_level(image: np.ndarray) -> int:
    """The frame's dark reference: median of the three hint-bar boxes."""
    vals = sorted(box_mean(image, d) for d in DARK)
    return vals[len(vals) // 2]


def contrasts(image: np.ndarray) -> list[int]:
    """Per-bright-box contrast against the anchor level."""
    a = anchor_level(image)
    return [box_mean(image, b) - a for b in BRIGHT]


@dataclass(frozen=True)
class EndProbe:
    hits: int
    contrast: tuple[int, ...]
    anchor: int

    @property
    def is_end(self) -> bool:
        return self.hits >= K_HITS


def read(image: np.ndarray) -> EndProbe:
    """Evaluate the probe on one BGR888 frame."""
    if image.shape[0] != CANON_H or image.shape[1] != CANON_W:
        raise ValueError(f"expected {CANON_W}x{CANON_H}, got {image.shape[1]}x{image.shape[0]}")
    a = anchor_level(image)
    con = tuple(box_mean(image, b) - a for b in BRIGHT)
    hits = sum(1 for c, t in zip(con, THRESH, strict=True) if c >= t)
    return EndProbe(hits=hits, contrast=con, anchor=a)


def pixel_reads() -> int:
    """Luma reads per frame — the on-device cost driver."""
    return sum(b.pixels for b in BRIGHT) + sum(d.pixels for d in DARK)


class EndProbeTracker:
    """CONFIRM_FRAMES-of-a-kind gate over successive frames.

    Rising edge only latches after the confirm count; the fall is immediate, so a
    false veto releases actuation again on the next clean frame rather than
    ending a run.
    """

    def __init__(self) -> None:
        self.count = 0
        self.latched = False

    def update(self, probe: EndProbe) -> bool:
        if probe.is_end:
            self.count += 1
            if self.count >= CONFIRM_FRAMES:
                self.latched = True
        else:
            self.count = 0
            self.latched = False
        return self.latched

    def reset(self) -> None:
        self.count = 0
        self.latched = False
