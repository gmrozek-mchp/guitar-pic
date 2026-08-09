"""Load the labelled GH3 screen corpus.

The corpus is the renamed snapshot set at `firmware/marvin/docs/gh3_screens/`
(720x480 PNGs). Each image's screen-class label comes from its filename via
`screens.screen_id_for_filename`. Images are returned as uint8 HxWx3 arrays in
**BGR** order, matching marvin's native framebuffer layout (capture is BGR888
packed; see marvin journal 2026-05-02).
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

from .screens import screen_id_for_filename

# tools/gameplay/gameplay/corpus.py -> repo root is parents[3].
_REPO_ROOT = Path(__file__).resolve().parents[3]
_DEFAULT_CORPUS_DIR = _REPO_ROOT / "firmware" / "marvin" / "docs" / "gh3_screens"
# The score corpus is a small hand-labelled set kept alongside the prototype
# (tools/gameplay/data/scores/), separate from the screen corpus: score frames
# carry their numeric value in the filename, not a screen/menu label.
_DEFAULT_SCORE_CORPUS_DIR = Path(__file__).resolve().parents[1] / "data" / "scores"
# The streak corpus (tools/gameplay/data/streak/) is the hand-labelled odometer set:
# each frame is a scoring-block crop whose 3-digit value is in the filename (with
# `x` for a mid-roll/unreadable wheel). See metadata.streak_from_filename.
_DEFAULT_STREAK_CORPUS_DIR = Path(__file__).resolve().parents[1] / "data" / "streak"


def corpus_dir() -> Path:
    """Resolve the corpus directory (overridable via $GAMEPLAY_CORPUS_DIR)."""
    env = os.environ.get("GAMEPLAY_CORPUS_DIR")
    return Path(env) if env else _DEFAULT_CORPUS_DIR


def score_corpus_dir() -> Path:
    """Resolve the score corpus directory (overridable via $GAMEPLAY_SCORE_CORPUS_DIR)."""
    env = os.environ.get("GAMEPLAY_SCORE_CORPUS_DIR")
    return Path(env) if env else _DEFAULT_SCORE_CORPUS_DIR


def streak_corpus_dir() -> Path:
    """Resolve the streak corpus directory (overridable via $GAMEPLAY_STREAK_CORPUS_DIR)."""
    env = os.environ.get("GAMEPLAY_STREAK_CORPUS_DIR")
    return Path(env) if env else _DEFAULT_STREAK_CORPUS_DIR


@dataclass(frozen=True)
class Sample:
    screen_id: str
    path: Path
    image: np.ndarray  # uint8, HxWx3, BGR


def load_bgr(path: str | Path) -> np.ndarray:
    """Load an image file as a uint8 HxWx3 BGR array."""
    with Image.open(path) as im:
        rgb = np.asarray(im.convert("RGB"), dtype=np.uint8)
    return rgb[:, :, ::-1].copy()  # RGB -> BGR


def load_corpus(directory: str | Path | None = None) -> list[Sample]:
    """Load every `*.png` in the corpus directory as labelled `Sample`s.

    Sorted by filename for determinism. Raises FileNotFoundError if the
    directory is empty or missing.
    """
    d = Path(directory) if directory is not None else corpus_dir()
    files = sorted(d.glob("*.png"))
    if not files:
        raise FileNotFoundError(f"no PNGs found in corpus dir {d}")
    return [
        Sample(screen_id=screen_id_for_filename(f.name), path=f, image=load_bgr(f))
        for f in files
    ]


def load_score_corpus(directory: str | Path | None = None) -> list[Sample]:
    """Load the labelled score frames (`score__<mode>__<value>__*.png`).

    Labelled by numeric value in the filename (see `metadata.score_from_filename`),
    so these are returned as `in_song` samples; the score value is parsed from the
    path by the score reader/eval. Returns [] if the directory is missing/empty
    (the score corpus is optional relative to the screen corpus).
    """
    d = Path(directory) if directory is not None else score_corpus_dir()
    files = sorted(d.glob("score__*.png"))
    return [Sample(screen_id="in_song", path=f, image=load_bgr(f)) for f in files]


def load_amp2p_corpus(directory: str | Path | None = None) -> list[Sample]:
    """Load the labelled 2-player amp score frames (`score2p__<side>__<value>__*.png`).

    Amp-block crops labelled by side + numeric value in the filename (see
    `metadata.amp2p_score_from_filename`). Both sides live in one corpus and feed
    one digit bank — same glyph art at the same scale, only the block origin
    differs. Returned as `in_song_2p` samples. Returns [] if missing/empty.
    """
    d = Path(directory) if directory is not None else score_corpus_dir()
    files = sorted(d.glob("score2p__*.png"))
    return [Sample(screen_id="in_song_2p", path=f, image=load_bgr(f)) for f in files]


def load_streak_corpus(directory: str | Path | None = None) -> list[Sample]:
    """Load the labelled streak frames (`streak__<hundreds><tens><units>__*.png`).

    Scoring-block crops labelled by the odometer's 3-digit value in the filename
    (see `metadata.streak_from_filename`). Returned as `in_song` samples. Returns
    [] if the directory is missing/empty (the streak corpus is optional).
    """
    d = Path(directory) if directory is not None else streak_corpus_dir()
    files = sorted(d.glob("streak__*.png"))
    return [Sample(screen_id="in_song", path=f, image=load_bgr(f)) for f in files]
