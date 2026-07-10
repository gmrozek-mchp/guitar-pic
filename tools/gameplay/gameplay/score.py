"""In-song score reader: per-digit glyph OCR of the open-ended score value.

Unlike the menu/song readers (closed-set template matches), the score is an
open-ended number, so it is read digit by digit. The training font is white and
**proportional** (a `1` is narrower than an `8`, so digit x-positions shift with
the value — verified on a 6549-frame capture), but the digits are cleanly
gap-separated. So digits are isolated by their ink (the gaps between them), not a
fixed grid; each is resized to a canonical cell whose cells hold **ink coverage**
(0-255), and matched by integer L1 against a per-mode bank of **10 per-digit
templates** (one averaged coverage mask per glyph — it's a fixed 10-glyph font).
The digit count falls out of the segmentation (no fixed N, no blank class).

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
  offset, segments it, and matches each glyph's coverage mask (argmin integer L1)
  against the 10 templates — no offset search. The per-band relative ink threshold
  makes the coverage gain/offset robust, so no per-frame normalization is needed.
  Cheap: an ink mask + a small integer L1 over 10 tiny templates.

Templates are built from the labelled score corpus: each frame's digits (from its
filename value) map left→right onto the segmented glyphs, averaged per digit into
10 uint8 coverage masks — no hand-cropping. Integer-only; light for the firmware.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

import numpy as np

from .corpus import Sample, load_bgr, score_corpus_dir
from .fingerprint import CANONICAL_H, CANONICAL_W, _to_canonical
from .metadata import (
    SCORE_BLOCK_ROI,
    SCORE_DIGIT_BAND,
    SCORE_MULT_BRIGHT_MIN,
    SCORE_MULT_MIN_COUNT,
    SCORE_MULT_ROI,
    SCORE_MULT_SAT_MIN,
    STREAK_CELL_H,
    STREAK_CELL_T,
    STREAK_CELL_U,
    STREAK_GLYPH_COLS,
    STREAK_GLYPH_ROWS,
    STREAK_DEBOUNCE,
    STREAK_INK_FRAC,
    STREAK_MAX_STEP,
    STREAK_NOTE_CELL,
    STREAK_NOTE_MAX_SAD,
    STREAK_UNK_DIST,
    STREAK_UNK_MARGIN,
    score_from_filename,
    streak_from_filename,
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
    digit: int         # 0-9
    vec: np.ndarray    # uint8 ink-coverage mask, length glyph_rows*glyph_cols


@dataclass(frozen=True)
class ChromeReference:
    ref: np.ndarray   # median block luma (h, w) at the nominal block position
    mask: np.ndarray  # bool (h, w): static-chrome pixels used for registration


@dataclass(frozen=True)
class ScoreCatalog:
    mode: str
    templates: list[DigitTemplate]  # 10 per-digit averaged coverage masks (0-9)
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


def _glyph_cov(band: np.ndarray, run: tuple[int, int], cfg: ScoreConfig) -> np.ndarray:
    """Ink-coverage fingerprint of one segmented digit (bbox → canonical grid).

    Each cell holds the fraction of its pixels above the band's relative ink
    threshold, quantized to uint8 0-255. Coverage (not a 1-bit mask) keeps the
    thin/antialiased strokes that separate look-alike digits (8 vs 5/3); the
    relative threshold makes it gain/offset robust. Matching is then a plain
    integer L1 over 10 per-digit templates — no float normalization.
    """
    x0, x1 = run
    mask = _ink_mask(band, cfg)[:, x0:x1 + 1]
    ys = np.where(mask.any(1))[0]
    if len(ys):
        mask = mask[ys.min():ys.max() + 1, :]
    m = mask.astype(np.float64)
    ye = np.linspace(0, m.shape[0], cfg.glyph_rows + 1).round().astype(int)
    xe = np.linspace(0, m.shape[1], cfg.glyph_cols + 1).round().astype(int)
    g = np.empty((cfg.glyph_rows, cfg.glyph_cols), dtype=np.float64)
    for r in range(cfg.glyph_rows):
        for c in range(cfg.glyph_cols):
            blk = m[ye[r]:max(ye[r] + 1, ye[r + 1]), xe[c]:max(xe[c] + 1, xe[c + 1])]
            g[r, c] = blk.mean() if blk.size else 0.0
    return np.round(g.reshape(-1) * 255.0).astype(np.uint8)


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
    """Build the per-mode 0-9 coverage templates + the chrome reference.

    Each labelled frame is segmented; the glyphs map left→right onto the digits of
    the filename value (the run count must match). One template per digit — the
    mean ink-coverage over its exemplars (uint8) — since the fixed font's glyphs
    are consistent enough that a single averaged coverage mask separates all ten.
    """
    acc: dict[int, list[np.ndarray]] = defaultdict(list)
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
            acc[int(ch)].append(_glyph_cov(band, run, config))
    templates = [
        DigitTemplate(digit=d, vec=np.round(np.mean(np.stack(v), axis=0)).astype(np.uint8))
        for d, v in sorted(acc.items())
    ]
    chrome = build_chrome_reference(mode_images, config)
    return ScoreCatalog(mode=mode, templates=templates, chrome=chrome, config=config)


def read_score(
    image: np.ndarray,
    catalog: ScoreCatalog,
    calibration: ScoreCalibration | None = None,
) -> ScoreResult:
    """Read the score: crop the (registered) digit band, segment, classify each glyph.

    The digit count is however many glyphs the segmentation finds. Each glyph's
    coverage mask is matched (argmin integer L1) against the 10 per-digit
    templates; the digits assemble left→right into the integer. `calibration`
    supplies the locked block offset; if omitted the nominal band is used.
    """
    cfg = catalog.config
    dx = calibration.dx if calibration is not None else 0
    dy = calibration.dy if calibration is not None else 0
    band = _band_luma(image, catalog.mode, dx, dy)

    vecs = np.stack([t.vec.astype(np.int32) for t in catalog.templates])
    tdig = np.array([t.digit for t in catalog.templates])

    digits: list[int] = []
    dists: list[float] = []
    margins: list[float] = []
    for run in _segment_digits(band, cfg):
        fv = _glyph_cov(band, run, cfg).astype(np.int32)
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


def read_multiplier(image: np.ndarray) -> int:
    """Classify the score multiplier (1..4) by the medallion glyph's colour.

    2x = gold, 3x = green, 4x = purple; 1x shows no digit (dim portrait). Count
    bright, saturated pixels of each hue in the small `SCORE_MULT_ROI` patch and
    take the argmax — 1x if none clears `SCORE_MULT_MIN_COUNT`. Colour-only (no
    shape/segmentation), so a small patch suffices; works on a block crop or a
    full frame. Mode-independent.
    """
    img = _ensure_full_frame(image)
    x0, y0, x1, y1 = SCORE_MULT_ROI
    s = img[y0:y1, x0:x1].reshape(-1, 3).astype(np.int32)  # BGR
    b, g, r = s[:, 0], s[:, 1], s[:, 2]
    mx = s.max(1)
    bright = (mx > SCORE_MULT_BRIGHT_MIN) & ((mx - s.min(1)) > SCORE_MULT_SAT_MIN)
    purple = int((bright & (r > g + 25) & (b > g + 25)).sum())
    green = int((bright & (g > r + 20) & (g > b + 20)).sum())
    yellow = int((bright & (r > b + 40) & (g > b + 40)).sum())
    best = max(purple, green, yellow)
    if best < SCORE_MULT_MIN_COUNT:
        return 1
    if purple >= green and purple >= yellow:
        return 4
    if green >= yellow:
        return 3
    return 2


# ─── note-streak counter (odometer OCR + monotonic tracker) ─────────────────────
#
# Three fixed digit cells (hundreds/tens white-on-dark, units dark-on-light — the
# highlighted wheel), each read independently: extract ink by the cell's polarity,
# resize the ink bbox to a canonical coverage grid, and match (integer L1) against a
# per-polarity bank of 0-9 templates. A cell whose best distance/margin fails the
# confidence gate is flagged unreadable (a mid-roll tumbler). `read_streak` is
# stateless (one frame → per-cell raw reads); `StreakTracker` reconciles the noisy
# reads over time using the monotonic-increasing assumption (see its docstring).


@dataclass(frozen=True)
class StreakConfig:
    glyph_rows: int = STREAK_GLYPH_ROWS
    glyph_cols: int = STREAK_GLYPH_COLS
    ink_frac: float = STREAK_INK_FRAC
    note_max_sad: int = STREAK_NOTE_MAX_SAD
    unk_dist: int = STREAK_UNK_DIST
    unk_margin: int = STREAK_UNK_MARGIN
    debounce: tuple[int, int, int] = STREAK_DEBOUNCE
    max_step: int = STREAK_MAX_STEP


DEFAULT_STREAK_CONFIG = StreakConfig()

# Place order is (hundreds, tens, units); each entry is (cell ROI, is_light).
STREAK_CELLS: tuple[tuple[tuple[int, int, int, int], bool], ...] = (
    (STREAK_CELL_H, False),
    (STREAK_CELL_T, False),
    (STREAK_CELL_U, True),
)


@dataclass(frozen=True)
class StreakCatalog:
    note_tmpl: np.ndarray      # note-icon coverage mask at the locked position (presence fiducial)
    tmpl_wd: list[np.ndarray]  # 10 white-on-dark coverage masks, index == digit
    tmpl_dl: list[np.ndarray]  # 10 dark-on-light coverage masks, index == digit
    config: StreakConfig


@dataclass(frozen=True)
class StreakRaw:
    present: bool                       # odometer shown (streak >= ~25)
    digits: tuple[int, int, int]        # per-place best-match digit (h, t, u)
    known: tuple[bool, bool, bool]      # per-place: read was confident (settled wheel)


def _cell_luma(image: np.ndarray, roi: tuple[int, int, int, int]) -> np.ndarray:
    img = _ensure_full_frame(image)
    x0, y0, x1, y1 = roi
    return img[y0:y1, x0:x1].astype(np.float64) @ _LUMA_W / 256.0


def _cell_cov(cell: np.ndarray, is_light: bool, cfg: StreakConfig) -> np.ndarray:
    """Ink-coverage fingerprint of one odometer cell (polarity-aware, bbox → grid).

    White-on-dark cells take the bright pixels as ink; the light units wheel takes
    the dark pixels. The ink bbox is resized to the canonical grid, each cell
    holding its ink fraction (uint8 0-255) — the same coverage representation the
    score reader matches with integer L1.
    """
    lo, hi = cell.min(), cell.max()
    if is_light:
        ink = cell < lo + (1.0 - cfg.ink_frac) * (hi - lo)
    else:
        ink = cell > lo + cfg.ink_frac * (hi - lo)
    ys = np.where(ink.any(1))[0]
    xs = np.where(ink.any(0))[0]
    if not len(ys) or not len(xs):
        return np.zeros(cfg.glyph_rows * cfg.glyph_cols, dtype=np.uint8)
    m = ink[ys.min():ys.max() + 1, xs.min():xs.max() + 1].astype(np.float64)
    ye = np.linspace(0, m.shape[0], cfg.glyph_rows + 1).round().astype(int)
    xe = np.linspace(0, m.shape[1], cfg.glyph_cols + 1).round().astype(int)
    g = np.empty((cfg.glyph_rows, cfg.glyph_cols), dtype=np.float64)
    for r in range(cfg.glyph_rows):
        for c in range(cfg.glyph_cols):
            blk = m[ye[r]:max(ye[r] + 1, ye[r + 1]), xe[c]:max(xe[c] + 1, xe[c + 1])]
            g[r, c] = blk.mean() if blk.size else 0.0
    return np.round(g.reshape(-1) * 255.0).astype(np.uint8)


def _streak_classify(cov: np.ndarray, bank: list[np.ndarray]) -> tuple[int, int, int]:
    """(digit, best L1, runner-up margin) for one cell against a 10-template bank."""
    vecs = np.stack([b.astype(np.int32) for b in bank])  # row d == digit d
    d = np.abs(vecs - cov.astype(np.int32)).sum(1)
    order = np.argsort(d)
    best = int(order[0])
    return best, int(d[order[0]]), int(d[order[1]] - d[order[0]])


def build_streak_catalog(
    samples: list[Sample], config: StreakConfig = DEFAULT_STREAK_CONFIG
) -> StreakCatalog:
    """Build the two per-polarity 0-9 coverage banks from the labelled streak corpus.

    Each labelled cell (whose digit is known — not a mid-roll `x`) contributes its
    coverage mask to the white-on-dark bank (hundreds/tens) or the dark-on-light
    bank (units); one template per digit is the mean over its exemplars.
    """
    accW: dict[int, list[np.ndarray]] = defaultdict(list)
    accL: dict[int, list[np.ndarray]] = defaultdict(list)
    notes: list[np.ndarray] = []
    for s in samples:
        lab = streak_from_filename(s.path.name)
        if lab is None:
            continue
        notes.append(_cell_cov(_cell_luma(s.image, STREAK_NOTE_CELL), False, config))
        for (roi, light), digit in zip(STREAK_CELLS, lab):
            if digit is None:
                continue
            cov = _cell_cov(_cell_luma(s.image, roi), light, config)
            (accL if light else accW)[digit].append(cov)

    def bank(acc: dict[int, list[np.ndarray]]) -> list[np.ndarray]:
        return [
            np.round(np.mean(np.stack(acc[d]), axis=0)).astype(np.uint8)
            for d in range(10)
        ]  # KeyError if any digit is unseen — corpus must cover 0-9 per polarity

    note_tmpl = np.round(np.mean(np.stack(notes), axis=0)).astype(np.uint8)
    return StreakCatalog(note_tmpl=note_tmpl, tmpl_wd=bank(accW), tmpl_dl=bank(accL), config=config)


def read_streak(image: np.ndarray, catalog: StreakCatalog) -> StreakRaw:
    """Stateless per-frame odometer read: presence + per-place (digit, confident?).

    Presence means the odometer is *locked at its final position* — detected from the
    fixed note-icon glyph, not the digits: the counter slides in and bounces, so
    mid-slide the cells are misaligned. The note-icon coverage must match the locked
    template (L1 below `note_max_sad`); otherwise (absent or sliding) the odometer is
    not read. When locked, each cell is matched against its polarity's bank; a place
    is `known` only if the match clears the distance/margin gates (a mid-roll wheel is
    left unknown for the tracker to fill).
    """
    cfg = catalog.config
    img = _ensure_full_frame(image)
    note = _cell_cov(_cell_luma(img, STREAK_NOTE_CELL), False, cfg)
    if int(np.abs(note.astype(np.int32) - catalog.note_tmpl.astype(np.int32)).sum()) >= cfg.note_max_sad:
        return StreakRaw(False, (0, 0, 0), (False, False, False))
    banks = (catalog.tmpl_wd, catalog.tmpl_wd, catalog.tmpl_dl)
    digits: list[int] = []
    known: list[bool] = []
    for (roi, light), bank in zip(STREAK_CELLS, banks):
        cov = _cell_cov(_cell_luma(img, roi), light, cfg)
        digit, best, margin = _streak_classify(cov, bank)
        digits.append(digit)
        known.append(best < cfg.unk_dist and margin > cfg.unk_margin)
    return StreakRaw(True, (digits[0], digits[1], digits[2]), (known[0], known[1], known[2]))


class StreakTracker:
    """Reconcile noisy per-frame odometer reads into a streak value.

    Presence is binary: the odometer is either locked (shown, streak ≥ ~25) or not
    (absent / mid-slide). **When it is not present the streak has reset — go straight
    to 0** (that is the only reset path; a broken streak makes the counter disappear).

    While it is present, a confident wheel read is authoritative for its place, but a
    *changed* digit must be **debounced** — confirmed by `cfg.debounce[place]`
    consecutive confident reads before it commits. The slow wheels (hundreds, tens)
    need 2, so a single-frame misread (e.g. the tens 0↔8 aliasing flip) never sticks
    yet a sustained real change still commits; the units wheel is immediate (1). A
    committed change carries: unreadable lower places reset to 0 (a rollover). An
    unreadable place otherwise holds its last digit. First appearance seeds from the
    confident wheels (unknown places 0) once ≥2 agree. Debounce follows reads up *or*
    down, so it never locks.

    A committed value change larger than `cfg.max_step` is rejected as implausible
    (the frame is ignored) — a streak can't jump that much per poll, so it's a
    misread that cleared debounce (e.g. a hundreds wheel read mid-roll during a
    carry). The clamp is symmetric, so it still corrects downward and never locks;
    the seed bypasses it (a real reappearance jumps straight in).
    """

    def __init__(self, config: StreakConfig = DEFAULT_STREAK_CONFIG) -> None:
        self.cfg = config
        self.reset()

    def reset(self) -> None:
        self.val = 0
        self.seen = False
        self.dg = [0, 0, 0]      # per-place committed digits [hundreds, tens, units]
        self.pend = [-1, -1, -1]  # per-place pending (unconfirmed) digit, -1 = none
        self.pend_n = [0, 0, 0]   # consecutive confident reads of the pending digit

    def update(self, raw: StreakRaw) -> int:
        if not raw.present:
            self.reset()   # odometer gone (streak reset / not shown) → 0
            return 0

        if not self.seen:
            if sum(raw.known) < 2:
                return 0  # need ≥2 confident wheels to seed a value
            self.dg = [raw.digits[i] if raw.known[i] else 0 for i in range(3)]
            self.pend = [-1, -1, -1]
            self.pend_n = [0, 0, 0]
            self.seen = True
            self.val = self.dg[0] * 100 + self.dg[1] * 10 + self.dg[2]
            return self.val

        snap = (list(self.dg), list(self.pend), list(self.pend_n))
        carry = False
        for i in range(3):  # hundreds → tens → units
            if raw.known[i]:
                r = raw.digits[i]
                if r == self.dg[i]:
                    self.pend[i], self.pend_n[i] = -1, 0  # confirms committed
                else:
                    self.pend_n[i] = self.pend_n[i] + 1 if r == self.pend[i] else 1
                    self.pend[i] = r
                    if self.pend_n[i] >= self.cfg.debounce[i]:  # change confirmed
                        self.dg[i], self.pend[i], self.pend_n[i] = r, -1, 0
                        carry = True
            elif carry:  # unreadable wheel below a place that just rolled → 0
                self.dg[i], self.pend[i], self.pend_n[i] = 0, -1, 0

        new_val = self.dg[0] * 100 + self.dg[1] * 10 + self.dg[2]
        if abs(new_val - self.val) > self.cfg.max_step:
            self.dg, self.pend, self.pend_n = snap  # implausible jump → ignore frame
        else:
            self.val = new_val
        return self.val
