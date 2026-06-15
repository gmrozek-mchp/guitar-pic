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


def corpus_dir() -> Path:
    """Resolve the corpus directory (overridable via $GAMEPLAY_CORPUS_DIR)."""
    env = os.environ.get("GAMEPLAY_CORPUS_DIR")
    return Path(env) if env else _DEFAULT_CORPUS_DIR


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
