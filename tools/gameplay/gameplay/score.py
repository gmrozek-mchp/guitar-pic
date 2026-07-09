"""In-song score reader: per-digit glyph OCR of the open-ended score value.

Unlike the menu/song readers (closed-set template matches), the score is an
open-ended number, so it is read digit by digit. The training font is white and
**proportional** (a `1` is narrower than an `8`, so digit x-positions shift with
the value — verified on a 6549-frame capture), but the digits are cleanly
gap-separated. So digits are isolated by their ink (the gaps between them), not a
fixed grid; each is normalized to a canonical cell and matched against a per-mode
bank of 0-9 glyph exemplars (1-NN). The digit count falls out of the segmentation
(no fixed N, no blank class).

Two stages, because the field position is fixed on a given rig but the analog
capture varies frame to frame:

- **Registration (once).** The whole scoring block (`metadata.SCORE_BLOCK_ROI`)
  is located by matching its *chrome* — the static box frame, inner panel texture,
  and medallion ring, which are digit-independent and identical in training and
  career. A masked (`metadata` + hand-drawn `score_block_mask.png`) normalized-SAD
  offset search finds the block's `(dx, dy)` for this rig (`calibrate_score` →
  `ScoreCalibration`); the digit band is then a fixed offset inside it. Run once
  at gameplay start over a few frames.
- **Per-frame read (continual).** `read_score` crops the digit band at the locked
  offset, segments it, and 1-NN-classifies each glyph — no offset search. A
  per-band relative ink threshold and per-glyph normalization cancel A2D
  gain/offset drift. Cheap: an ink mask + a small L1 per digit.

Templates are built from the labelled score corpus: each frame's digits (from its
filename value) map left→right onto the segmented glyphs, each kept as a 1-NN
exemplar — no hand-cropping. Integer-friendly for the firmware port.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .corpus import Sample, load_bgr, score_corpus_dir
from .fingerprint import CANONICAL_H, CANONICAL_W, _to_canonical
from .metadata import (
    SCORE_BLOCK_ROI,
    SCORE_DIGIT_BAND,
    score_from_filename,
)
from .songselect import _LUMA_W

CHROME_MASK_FILE = "score_block_mask.png"


def _ensure_full_frame(image: np.ndarray) -> np.ndarray:
    """Return a canonical 720x480 frame.

    The marvin-perf region capture (and this corpus) provides just the 96x105
    scoring block; embed it into a black full frame at its real position so the
    reader's absolute ROIs / chrome registration work unchanged. A full frame is
    returned as-is; anything else falls back to `_to_canonical` (resize).
    """
    a = np.asarray(image)
    if a.shape[:2] == (CANONICAL_H, CANONICAL_W):
        return a
    x0, y0, x1, y1 = SCORE_BLOCK_ROI
    if a.shape[:2] == (y1 - y0, x1 - x0):
        full = np.zeros((CANONICAL_H, CANONICAL_W, a.shape[2]), dtype=a.dtype)
        full[y0:y1, x0:x1] = a
        return full
    return _to_canonical(image)


@dataclass(frozen=True)
class ScoreConfig:
    glyph_rows: int = 14   # canonical glyph grid (rows), each segmented digit resized to this
    glyph_cols: int = 10   # canonical glyph grid (cols)
    # Segmentation: relative ink threshold (fraction of the band's min..max luma
    # range — gain/offset robust since the digits are the brightest ink), and the
    # run/gap filters that split the ink profile into digit blobs.
    ink_frac: float = 0.6
    min_gap: int = 1       # empty columns (>this) that separate two digits
    min_width: int = 1     # drop runs narrower than this (specks)
    min_ink: int = 4       # drop runs with fewer than this many ink pixels
    # Registration search radius (px), used *once* to lock the block for the rig.
    reg_search: int = 8


DEFAULT_SCORE_CONFIG = ScoreConfig()


@dataclass(frozen=True)
class DigitTemplate:
    digit: int  # 0-9
    vec: np.ndarray


@dataclass(frozen=True)
class ChromeReference:
    ref: np.ndarray   # median block luma (h, w) at the nominal block position
    mask: np.ndarray  # bool (h, w): static-chrome pixels used for registration


@dataclass(frozen=True)
class ScoreCatalog:
    mode: str
    templates: list[DigitTemplate]  # one per labelled glyph exemplar (1-NN, not centroid)
    chrome: ChromeReference
    config: ScoreConfig


@dataclass(frozen=True)
class ScoreCalibration:
    mode: str
    dx: int = 0  # block offset found at registration
    dy: int = 0


@dataclass(frozen=True)
class ScoreResult:
    value: int
    digits: tuple[int, ...]  # the digits read, left->right
    dist: float              # worst (max) per-digit best-distance
    margin: float            # weakest (min) per-digit runner-up gap


# ─── digit band luma + segmentation ─────────────────────────────────────────────


def _band_luma(image: np.ndarray, mode: str, dx: int = 0, dy: int = 0) -> np.ndarray:
    """Luma of the digit search band, shifted by the registration offset."""
    img = _ensure_full_frame(image)
    x0, y0, x1, y1 = SCORE_DIGIT_BAND[mode]
    patch = img[y0 + dy:y1 + dy, x0 + dx:x1 + dx].astype(np.float64)
    return patch @ _LUMA_W / 256.0


def _ink_mask(band: np.ndarray, cfg: ScoreConfig) -> np.ndarray:
    thr = band.min() + cfg.ink_frac * (band.max() - band.min())
    return band > thr


def _segment_digits(band: np.ndarray, cfg: ScoreConfig) -> list[tuple[int, int]]:
    """Split the digit band into per-digit column runs (left->right).

    A column with any ink extends the current run; more than `min_gap` empty
    columns closes it. Specks (too narrow / too little ink) are dropped.
    """
    col = _ink_mask(band, cfg).sum(0)
    runs: list[tuple[int, int]] = []
    start: int | None = None
    gap = 0
    for x, c in enumerate(col):
        if c > 0:
            if start is None:
                start = x
            gap = 0
        elif start is not None:
            gap += 1
            if gap > cfg.min_gap:
                runs.append((start, x - gap))
                start = None
    if start is not None:
        runs.append((start, len(col) - 1))
    return [
        (x0, x1) for x0, x1 in runs
        if (x1 - x0 + 1) >= cfg.min_width and int(col[x0:x1 + 1].sum()) >= cfg.min_ink
    ]


def _glyph_vec(band: np.ndarray, run: tuple[int, int], cfg: ScoreConfig) -> np.ndarray:
    """Normalized fingerprint of one segmented digit (bbox → canonical grid)."""
    x0, x1 = run
    sub = band[:, x0:x1 + 1]
    # Crop to the glyph's ink rows so height variation doesn't dominate.
    rmask = _ink_mask(band, cfg)[:, x0:x1 + 1].any(1)
    ys = np.where(rmask)[0]
    if len(ys):
        sub = sub[ys.min():ys.max() + 1, :]
    ye = np.linspace(0, sub.shape[0], cfg.glyph_rows + 1).round().astype(int)
    xe = np.linspace(0, sub.shape[1], cfg.glyph_cols + 1).round().astype(int)
    g = np.empty((cfg.glyph_rows, cfg.glyph_cols), dtype=np.float64)
    for r in range(cfg.glyph_rows):
        for c in range(cfg.glyph_cols):
            blk = sub[ye[r]:max(ye[r] + 1, ye[r + 1]), xe[c]:max(xe[c] + 1, xe[c + 1])]
            g[r, c] = blk.mean() if blk.size else 0.0
    v = g.reshape(-1)
    v -= v.mean()
    s = v.std()
    return v / s if s > 1e-6 else v


# ─── chrome registration (locks the block; unchanged) ───────────────────────────


def _block_luma(image: np.ndarray, dx: int = 0, dy: int = 0) -> np.ndarray:
    """Luma of the scoring block at the nominal position shifted by (dx, dy)."""
    img = _ensure_full_frame(image)
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


def _norm_masked(a: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Zero-mean/unit-std over the masked pixels (cancels A2D gain/offset)."""
    m = a[mask]
    return (a - m.mean()) / (m.std() + 1e-6)


def calibrate_score(images: list[np.ndarray], catalog: ScoreCatalog) -> ScoreCalibration:
    """Lock the scoring block's position for this rig (run once at gameplay start).

    Matches the static chrome: over an offset search, the masked,
    per-frame-normalized SAD between each calibration frame's block and the chrome
    reference is summed; the `(dx, dy)` with the smallest total wins. The digit
    band is then that offset applied to the nominal band. Registering on the chrome
    (not the digits) is digit-independent and robust before any valid score shows.
    """
    cfg = catalog.config
    mask = catalog.chrome.mask
    ref_n = _norm_masked(catalog.chrome.ref, mask)
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
    return ScoreCalibration(mode=catalog.mode, dx=dx, dy=dy)


# ─── catalog build + read ────────────────────────────────────────────────────────


def build_score_catalog(
    samples: list[Sample],
    mode: str = "training",
    config: ScoreConfig = DEFAULT_SCORE_CONFIG,
) -> ScoreCatalog:
    """Build the per-mode 0-9 glyph exemplars + the chrome reference.

    Each labelled frame is segmented; the segmented glyphs map left→right onto the
    digits of the filename value (the run count must match), and each glyph vector
    is kept as a 1-NN exemplar. Matching is nearest-exemplar (not a centroid): the
    crisp digits blur together when averaged, so exemplars separate them cleanly.
    """
    templates: list[DigitTemplate] = []
    mode_images: list[np.ndarray] = []
    for s in samples:
        parsed = score_from_filename(s.path.name)
        if parsed is None or parsed[0] != mode:
            continue
        _mode, value = parsed
        mode_images.append(s.image)
        band = _band_luma(s.image, mode)
        runs = _segment_digits(band, config)
        digits = str(value)
        if len(runs) != len(digits):
            continue  # segmentation disagrees with the label — skip this frame
        for run, ch in zip(runs, digits):
            templates.append(DigitTemplate(digit=int(ch), vec=_glyph_vec(band, run, config)))
    chrome = build_chrome_reference(mode_images, config)
    return ScoreCatalog(mode=mode, templates=templates, chrome=chrome, config=config)


def read_score(
    image: np.ndarray,
    catalog: ScoreCatalog,
    calibration: ScoreCalibration | None = None,
) -> ScoreResult:
    """Read the score: crop the (registered) digit band, segment, classify each glyph.

    The digit count is however many glyphs the segmentation finds. Each glyph is
    1-NN-matched (argmin L1) against the exemplars; the digits assemble left→right
    into the integer. `calibration` supplies the locked block offset; if omitted
    the nominal band is used (a well-registered frame / tests).
    """
    cfg = catalog.config
    dx = calibration.dx if calibration is not None else 0
    dy = calibration.dy if calibration is not None else 0
    band = _band_luma(image, catalog.mode, dx, dy)

    vecs = np.stack([t.vec for t in catalog.templates])
    tdig = np.array([t.digit for t in catalog.templates])

    digits: list[int] = []
    dists: list[float] = []
    margins: list[float] = []
    for run in _segment_digits(band, cfg):
        fv = _glyph_vec(band, run, cfg)
        d = np.abs(vecs - fv).sum(1)
        order = np.argsort(d)
        best = int(tdig[order[0]])
        digits.append(best)
        dists.append(float(d[order[0]]))
        # runner-up gap = nearest exemplar of a different digit − nearest
        other = next((float(d[i]) for i in order if int(tdig[i]) != best), float(d[order[0]]))
        margins.append(other - float(d[order[0]]))

    text = "".join(str(x) for x in digits)
    value = int(text) if text else -1
    return ScoreResult(
        value=value,
        digits=tuple(digits),
        dist=max(dists) if dists else 0.0,
        margin=min(margins) if margins else 0.0,
    )
