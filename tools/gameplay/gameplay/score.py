"""In-song score reader: per-digit glyph OCR of the open-ended score value.

Unlike the menu/song readers (closed-set template matches), the score is an
open-ended number, so it is read digit by digit. The score is right-aligned in a
fixed HUD box and grows leftward; GH3 renders tabular (fixed-advance) digits, so
the score ROI (`metadata.SCORE_ROI[mode]`) splits into `N_SCORE_DIGITS` equal
cells anchored at the right edge. Each cell is fingerprinted (low-res normalized
luma grid, the same idea as the song reader) and matched against a per-mode bank
of digit templates 0-9 plus a `BLANK` class for the unused leading cells.
Non-blank cells, left to right, assemble the integer value.

Two stages, because the field position is fixed on a given rig but the analog
capture varies frame to frame:

- **Registration (once).** The whole scoring block (`metadata.SCORE_BLOCK_ROI`)
  is located by matching its *chrome* — the static box frame, inner panel texture,
  and medallion ring, which are digit-independent and identical in training and
  career. A masked (`metadata` + hand-drawn `score_block_mask.png`) normalized-SAD
  offset search finds the block's `(dx, dy)` for this rig (`calibrate_score` →
  `ScoreCalibration`); the digit cells are then fixed offsets inside it. This is a
  better anchor than the digits themselves: it can't alias onto a neighbouring
  digit, needs no digit templates to register, and works before a valid score has
  even appeared. Run once at gameplay start over a few frames.
- **Per-frame read (continual).** `read_score` samples the locked cells and
  argmin-classifies each — no search. Per-frame luma normalization cancels the
  A2D gain/offset/brightness drift, so the fixed cells read correctly frame to
  frame. Cheap: a handful of rect-means + an L1 over the digit templates.

Templates are built from the labelled score corpus: each frame's known value is
right-justified across the cells, giving every cell a digit (or blank) label, and
every labelled cell fingerprint is kept as a 1-NN exemplar — no hand-cropping.

Integer-friendly for the firmware port: fixed cells + argmin L1 over uint8-scale
vectors, exactly like `gp_read_song`; registration is a masked SAD over the block,
run once instead of per frame.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .corpus import Sample, load_bgr, score_corpus_dir
from .fingerprint import _to_canonical
from .metadata import (
    N_SCORE_DIGITS,
    SCORE_BLOCK_ROI,
    SCORE_ROI,
    score_from_filename,
)
from .songselect import _LUMA_W, _luma_grid, _shift

BLANK = 10  # class label for an unused (empty) leading cell
CHROME_MASK_FILE = "score_block_mask.png"


@dataclass(frozen=True)
class ScoreConfig:
    n_digits: int = N_SCORE_DIGITS
    cols: int = 8   # per-cell grid columns (captures the digit's ink pattern)
    rows: int = 14  # per-cell grid rows
    # Registration search radius (px), used *once* to lock the block position for
    # the rig; not on the per-frame read path. Wide enough to cover per-rig
    # position variation.
    reg_search: int = 8


DEFAULT_SCORE_CONFIG = ScoreConfig()


@dataclass(frozen=True)
class DigitTemplate:
    digit: int  # 0-9, or BLANK
    vec: np.ndarray


@dataclass(frozen=True)
class ChromeReference:
    ref: np.ndarray   # median block luma (h, w) at the nominal block position
    mask: np.ndarray  # bool (h, w): static-chrome pixels used for registration


@dataclass(frozen=True)
class ScoreCatalog:
    mode: str
    templates: list[DigitTemplate]  # one per labelled cell exemplar (1-NN, not centroid)
    chrome: ChromeReference
    config: ScoreConfig


@dataclass(frozen=True)
class ScoreCalibration:
    mode: str
    cells: tuple[tuple[int, int, int, int], ...]  # locked per-cell ROIs (len n_digits)
    dx: int = 0  # block offset found at registration (for debug/report)
    dy: int = 0


@dataclass(frozen=True)
class ScoreResult:
    value: int
    digits: tuple[int, ...]  # per-cell class, left->right (BLANK for empty leading cells)
    dist: float              # worst (max) per-cell best-distance
    margin: float            # weakest (min) per-cell 2nd-best - best


def _cell_rois(roi: tuple[int, int, int, int], n: int) -> list[tuple[int, int, int, int]]:
    """Split a score ROI into `n` equal-width digit cells (left->right)."""
    x0, y0, x1, y1 = roi
    xs = np.linspace(x0, x1, n + 1).round().astype(int)
    return [(int(xs[i]), y0, int(xs[i + 1]), y1) for i in range(n)]


def _labels_for_value(value: int, n_digits: int) -> list[int]:
    """Right-justify `value` across `n_digits` cells -> per-cell class labels.

    Leading (unused) cells are BLANK; e.g. 1138 over 6 cells ->
    [BLANK, BLANK, 1, 1, 3, 8]. Scores never carry leading zeros.
    """
    s = str(value)
    pad = n_digits - len(s)
    return [BLANK] * pad + [int(c) for c in s]


def _block_luma(image: np.ndarray, dx: int = 0, dy: int = 0) -> np.ndarray:
    """Luma of the scoring block at the nominal position shifted by (dx, dy)."""
    img = _to_canonical(image)
    x0, y0, x1, y1 = SCORE_BLOCK_ROI
    patch = img[y0 + dy:y1 + dy, x0 + dx:x1 + dx].astype(np.float64)
    return patch @ _LUMA_W / 256.0


def load_chrome_mask(path=None) -> np.ndarray:
    """Load the hand-drawn registration mask as a bool array at block resolution.

    The mask PNG paints the static-chrome pixels (used for registration) in
    magenta; it may be authored at any integer upscale of the block. Returns a
    (block_h, block_w) bool array.
    """
    x0, y0, x1, y1 = SCORE_BLOCK_ROI
    bw, bh = x1 - x0, y1 - y0
    p = path if path is not None else score_corpus_dir() / CHROME_MASK_FILE
    a = load_bgr(p)  # (H, W, 3) BGR; magenta is symmetric so channel order is moot
    mag = (a[..., 0] > 200) & (a[..., 2] > 200) & (a[..., 1] < 150)
    H, W = mag.shape
    if H % bh or W % bw:
        raise ValueError(f"chrome mask {W}x{H} is not an integer multiple of block {bw}x{bh}")
    fy, fx = H // bh, W // bw
    return mag.reshape(bh, fy, bw, fx).mean(axis=(1, 3)) >= 0.5


def build_chrome_reference(
    images: list[np.ndarray], config: ScoreConfig = DEFAULT_SCORE_CONFIG, mask_path=None
) -> ChromeReference:
    """Median block luma (the static chrome) + the registration mask."""
    ref = np.median(np.stack([_block_luma(im) for im in images]), axis=0)
    return ChromeReference(ref=ref, mask=load_chrome_mask(mask_path))


def build_score_catalog(
    samples: list[Sample],
    mode: str = "training",
    config: ScoreConfig = DEFAULT_SCORE_CONFIG,
) -> ScoreCatalog:
    """Build per-mode digit templates (0-9 + BLANK) + the chrome reference.

    Each frame's value is right-justified over the cells to label every cell, and
    every labelled cell fingerprint is kept as its own template — matching is 1-NN
    over the exemplars (not a per-class centroid): the crisp digits blur together
    when averaged (8/3/0), so nearest-exemplar separates them cleanly. The chrome
    reference (for registration) is the median block over the same frames.
    """
    cells = _cell_rois(SCORE_ROI[mode], config.n_digits)
    templates: list[DigitTemplate] = []
    mode_images: list[np.ndarray] = []
    for s in samples:
        parsed = score_from_filename(s.path.name)
        if parsed is None or parsed[0] != mode:
            continue
        _mode, value = parsed
        mode_images.append(s.image)
        labels = _labels_for_value(value, config.n_digits)
        for cell, label in zip(cells, labels):
            templates.append(DigitTemplate(digit=label, vec=_luma_grid(s.image, cell, config)))
    chrome = build_chrome_reference(mode_images, config)
    return ScoreCatalog(mode=mode, templates=templates, chrome=chrome, config=config)


def _norm_masked(a: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Zero-mean/unit-std over the masked pixels (cancels A2D gain/offset)."""
    m = a[mask]
    return (a - m.mean()) / (m.std() + 1e-6)


def calibrate_score(
    images: list[np.ndarray],
    catalog: ScoreCatalog,
) -> ScoreCalibration:
    """Lock the scoring block's position for this rig (run once at gameplay start).

    Locates the block by matching its static chrome: over an offset search, the
    masked, per-frame-normalized SAD between each calibration frame's block and
    the chrome reference is summed; the `(dx, dy)` with the smallest total wins.
    The digit cells are then the nominal cells shifted by that offset. Using the
    chrome (not the digits) means registration is digit-independent and can't
    alias a cell onto a neighbouring digit.
    """
    cfg = catalog.config
    chrome = catalog.chrome
    mask = chrome.mask
    ref_n = _norm_masked(chrome.ref, mask)
    rng = cfg.reg_search

    best: tuple[float, int, int] | None = None
    for dy in range(-rng, rng + 1):
        for dx in range(-rng, rng + 1):
            total = 0.0
            for img in images:
                tn = _norm_masked(_block_luma(img, dx, dy), mask)
                total += float(np.abs((tn - ref_n)[mask]).sum())
            if best is None or total < best[0]:
                best = (total, dx, dy)
    assert best is not None
    _t, dx, dy = best
    cells = tuple(_cell_rois(_shift(SCORE_ROI[catalog.mode], dx, dy), cfg.n_digits))
    return ScoreCalibration(mode=catalog.mode, cells=cells, dx=dx, dy=dy)


def read_score(
    image: np.ndarray,
    catalog: ScoreCatalog,
    calibration: ScoreCalibration | None = None,
) -> ScoreResult:
    """Read the score from the locked cells — no offset search (search-free per frame).

    Each cell is fingerprinted (per-frame normalized, so A2D gain/offset drift is
    cancelled) and argmin-classified against the digit/blank templates. Non-blank
    classes, left to right, assemble the integer value. `calibration` gives the
    locked per-cell rects from `calibrate_score`; if omitted the nominal ROI cells
    are used (convenient for a single well-registered frame / tests).
    """
    cfg = catalog.config
    if calibration is not None:
        cells = list(calibration.cells)
    else:
        cells = _cell_rois(SCORE_ROI[catalog.mode], cfg.n_digits)
    templates = catalog.templates

    per_cell: list[tuple[int, float, float]] = []
    for cell in cells:
        fp = _luma_grid(image, cell, cfg)
        scored = sorted((float(np.abs(fp - t.vec).sum()), t.digit) for t in templates)
        best_d, best_c = scored[0]
        # margin = distance to the nearest exemplar of a *different* class.
        other = next((d for d, c in scored if c != best_c), best_d)
        per_cell.append((best_c, best_d, other - best_d))

    digits = tuple(c for c, _d, _m in per_cell)
    text = "".join(str(c) for c in digits if c != BLANK)
    value = int(text) if text else 0
    return ScoreResult(
        value=value,
        digits=digits,
        dist=max(d for _c, d, _m in per_cell),
        margin=min(m for _c, _d, m in per_cell),
    )
