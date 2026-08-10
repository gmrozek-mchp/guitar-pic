"""Frame-rate end-of-song detector: is the results screen up?

The scoreboard-presence probe (present.py) answers "is this a gameplay screen"
well enough to start a run, but it is a masked SAD over a ~10k-pixel block, so
the observer only runs it on request and the controller polls it at 300 ms with a
3-sample confirm. That is ~1.0-1.35 s of actuation after a song ends, and the
notes still being strummed land on the results screen's menu.

This probe is the cheap counterpart, sized to run on *every* frame: 9 fixed
patches, 25 luma samples each (225 reads/frame), integer throughout. It answers
only one question -- "has the results screen appeared" -- and it is used as a
fast veto on actuation, not as the verdict on whether the run ended (gp_classify
still owns that).

Geometry. Both end screens (practice_end_menu, faceoff_end_menu) are a magazine
spread over a sketch collage. The collage's *right* side is light and its *left*
side is dark, and that split is page furniture: it is identical across both end
layouts and no results field can move onto it. So the 6 bright patches sit on the
right collage/notes column and the 3 dark anchors on the left. Excluded by rule
rather than by measurement: the left magazine page (cover art and the song/artist
text vary per song), every results field, and every region where gameplay puts
bright moving content (both note highways, the scoreboards, the centre performer)
-- the 6 corpus gameplay frames cannot show a note passing under a patch, star
power, or the performer walking past, so those areas are refused outright.

Decision is a *contrast*, not a level: `bright_i - median(anchors) >= THRESH[i]`,
counted, and the screen is called at K of 6. The analog feed carries gain/offset
slop (present.py), and out on the collage the bright patches only reach ~110-160
luma, so an absolute threshold does not survive it -- 0.85 gain with -20 offset
collapses the margin to nothing. A difference cancels offset exactly and only
scales with gain, which the measured margin (65-90 luma of separation) absorbs.

Not shake-tolerant on purpose, on the end side. Missing a note shakes the frame,
which is why present.py's probes need positional headroom and why the controller
confirms 3 times -- but the shake happens during *gameplay*, and the end screen
is static. A shake cannot make dark content bright; it can only drag something
bright onto a patch, so the conservative max-over-shift bound belongs on the
gameplay side alone. The end side carries a +/-2 px allowance for a static
per-rig capture offset, nothing more.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

LUMA_B, LUMA_G, LUMA_R = 29, 150, 77   # == present._LUMA / gp_probes luma weights

# Frames the probe is defined against (canonical capture size).
CANON_W, CANON_H = 720, 480


@dataclass(frozen=True)
class Patch:
    """An n x n lattice of single-pixel samples at `stride` px spacing, centred (x, y).

    A lattice rather than a solid block so a wide footprint stays cheap: 25 samples
    cover a 9x9 (stride 2) or 17x17 (stride 4) extent. The firmware samples exactly
    these coordinates, so the host must model the lattice, not a box mean.
    """

    x: int
    y: int
    n: int
    stride: int

    @property
    def half(self) -> int:
        return ((self.n - 1) // 2) * self.stride


# Regions the point search refuses, as (x0, y0, x1, y1). Kept here because they are
# the provenance of the tables below: re-picking points without them silently
# reintroduces the failures they encode. Every one is a *rule*, not a measurement --
# the corpus has 6 static gameplay frames and 8 end frames from two plays of one
# song, so it cannot show a note passing under a patch, star power, the 2-player
# performer walking past, or a results field moving with its value.
SELECTION_EXCLUSIONS: dict[str, tuple[tuple[int, int, int, int], ...]] = {
    # Black letterbox bars: clipped, so they track neither gain nor offset.
    "letterbox": ((0, 0, 720, 10), (0, 468, 720, 480)),
    # Left magazine page: album art and the song/artist text change per song.
    "per_song_art": ((110, 36, 358, 418),),
    # Results fields, both layouts -- these move with the score/streak/notes-hit.
    "results_practice": ((366, 60, 532, 224), (390, 240, 472, 312), (524, 58, 602, 104)),
    "results_faceoff": ((366, 80, 516, 366),),
    # Where 1-player gameplay puts bright moving content: the note highway, the
    # scoring block, the section-name text.
    "gameplay_1p": ((180, 185, 520, 470), (108, 300, 216, 420), (280, 110, 440, 165)),
    # Same for 2 players: both highways, both amp scoreboards, the centre performer.
    "gameplay_2p": ((80, 235, 625, 470), (117, 166, 205, 256), (507, 166, 595, 256),
                    (225, 0, 475, 305)),
}


def excluded(x: int, y: int) -> str | None:
    """The exclusion zone containing (x, y), or None. Used by the point-set tests."""
    for name, rects in SELECTION_EXCLUSIONS.items():
        for x0, y0, x1, y1 in rects:
            if x0 <= x < x1 and y0 <= y < y1:
                return name
    return None


# Bright patches: right-hand collage + the notes column of the right page.
BRIGHT: tuple[Patch, ...] = (
    Patch(669, 56, 5, 2),
    Patch(634, 216, 5, 2),
    Patch(637, 121, 5, 2),
    Patch(582, 143, 5, 2),
    Patch(599, 26, 5, 2),
    Patch(703, 117, 5, 2),
)

# Dark anchors: left collage. Wider stride (17x17 extent) because their job is to
# report the frame's dark level, not to resolve any feature.
ANCHORS: tuple[Patch, ...] = (
    Patch(47, 392, 5, 4),
    Patch(44, 66, 5, 4),
    Patch(91, 175, 5, 4),
)

# Per-patch contrast threshold, midway between the worst end-screen contrast and
# the best gameplay contrast measured on the corpus (see `calibrate`). Regenerate
# with `gameplay endprobe-calibrate` if the point table changes.
THRESH: tuple[int, ...] = (54, 54, 55, 56, 47, 45)

# How many of the 6 must clear their threshold. 5 leaves one patch free to be
# stepped on (a stray bright object, a dead pixel run) without losing the call.
K_HITS = 5

# Consecutive frames required before acting. Two frames (~33 ms) costs nothing
# against the 300 ms poll it replaces and rejects a single-frame decode artefact.
CONFIRM_FRAMES = 2


def patch_mean(image: np.ndarray, p: Patch) -> int:
    """Integer mean luma over the patch's lattice samples (floor, as the C does)."""
    c = (p.n - 1) // 2
    offs = [(i - c) * p.stride for i in range(p.n)]
    total = 0
    for dy in offs:
        row = image[p.y + dy]
        for dx in offs:
            b, g, r = row[p.x + dx]
            total += (LUMA_B * int(b) + LUMA_G * int(g) + LUMA_R * int(r)) >> 8
    return total // (p.n * p.n)


def anchor_level(image: np.ndarray) -> int:
    """The frame's dark reference: median of the three anchor patches.

    Median rather than min/max so one anchor landing on something unexpected
    (a light-coloured object drifting into the left of frame) cannot drag the
    reference on its own.
    """
    vals = sorted(patch_mean(image, a) for a in ANCHORS)
    return vals[len(vals) // 2]


def contrasts(image: np.ndarray) -> list[int]:
    """Per-bright-patch contrast against the anchor level."""
    a = anchor_level(image)
    return [patch_mean(image, p) - a for p in BRIGHT]


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
    con = tuple(patch_mean(image, p) - a for p in BRIGHT)
    hits = sum(1 for c, t in zip(con, THRESH, strict=True) if c >= t)
    return EndProbe(hits=hits, contrast=con, anchor=a)


class EndProbeTracker:
    """CONFIRM_FRAMES-of-a-kind gate over successive frames.

    Rising edge only latches after the confirm count; the fall is immediate, so a
    false veto releases actuation again on the next clean frame rather than
    ending a run. That asymmetry is what makes an aggressive K safe: a false
    positive costs a fraction of a second of missed notes, a false negative costs
    errant presses on the results menu.
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
