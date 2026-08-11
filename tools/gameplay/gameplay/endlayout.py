"""Which end screen is this -- practice_end_menu or faceoff_end_menu?

The screen fingerprint cannot answer this any more. Both end layouts are a
magazine spread over a collage, and the collage, the cover art and the decoration
column down the right page are all *per-song*: measured on hardware, an unseen
song's end screen landed 5192 from `song_select` and did not have
`faceoff_end_menu` in its top four. Masking the varying regions does not recover
it either (best measured margin 369 against t_margin 600, at a cost of 4 LOO
frames), and adding the frame to the corpus makes it worse -- the class centroid
lands between two magazines and `faceoff_end_menu` LOO drops 3/3 -> 0/4.

So this names the screen from *layout*, which is the thing that actually differs:

- practice has 5 menu items and an "N OUT OF M" box above them
- faceoff has 3 menu items and two player stat columns

`A` sits inside the practice-only OUT OF box; `B` sits inside the faceoff-only
player-1 streak block. The test is their difference, so the per-song grading of
the page cancels (one magazine's page white reads 88 where another's reads 167).
Measured over both magazines, all 5 practice and all 3 faceoff menu-selection
states, and a +/-2 px capture offset: practice -111..-94, faceoff +44..+120.

PRECONDITION -- this is only meaningful once something else has established that
an end screen is up. It is *not* a screen classifier: A-B separates the two end
layouts, not end screens from menus. Measured leak on the corpus, so the hazard
is visible rather than assumed:

    speed_select     +145..+163      multiplayer_menu  +45..+46
    training_menu     +43..+50       part_select       -87..-67
    song_select       -79..  +3      character_select_2p -80..+17

`endprobe` does not close that gap either (it fires on most menus by design). The
caller that has the missing information is the controller: it chose the mode, so
it already knows which end screen to expect, and it only asks this question in
the window just after a run it started. Anything outside that window must not
call `name`.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .endprobe import Box, LUMA_B, LUMA_G, LUMA_R, box_mean

CANON_W, CANON_H = 720, 480

PRACTICE = "practice_end_menu"
FACEOFF = "faceoff_end_menu"
UNCERTAIN = "uncertain"

# Inside the practice-only "N OUT OF M" box. In faceoff this straddles the top
# edge of the CONTINUE highlight bar, which is why its faceoff spread is wide --
# it stays on the correct side of the dead band across every measured selection
# state and offset, but it is an edge, so the dead band is generous.
A = Box(426, 80, 462, 96)

# Inside the faceoff-only player-1 streak block; newsprint in practice.
B = Box(390, 230, 426, 246)

# A dead band rather than a single threshold: refusing to name a screen is
# recoverable (the controller re-reads), naming the wrong one sends it down the
# wrong navigation branch. 34 luma of margin on each side.
T_PRACTICE = -60
T_FACEOFF = 10


@dataclass(frozen=True)
class EndLayout:
    name: str
    contrast: int

    @property
    def certain(self) -> bool:
        return self.name != UNCERTAIN


def contrast(image: np.ndarray) -> int:
    """A - B, in integer luma. Offset cancels; only gain scales it."""
    return box_mean(image, A) - box_mean(image, B)


def name(image: np.ndarray) -> EndLayout:
    """Name the end layout. See the module docstring's PRECONDITION."""
    if image.shape[0] != CANON_H or image.shape[1] != CANON_W:
        raise ValueError(f"expected {CANON_W}x{CANON_H}, got {image.shape[1]}x{image.shape[0]}")
    c = contrast(image)
    if c <= T_PRACTICE:
        return EndLayout(PRACTICE, c)
    if c >= T_FACEOFF:
        return EndLayout(FACEOFF, c)
    return EndLayout(UNCERTAIN, c)


def pixel_reads() -> int:
    return A.pixels + B.pixels
