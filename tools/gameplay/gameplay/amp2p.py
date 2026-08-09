"""2-player amp scoreboard: per-side chrome registration + score digit reader.

Two-player GH3 shows two amp scoreboards near the top of the frame instead of the
single bottom-left block the score/multiplier/streak readers use. This module
locates each amp independently and reads its score, so both sides run the same
code at per-side offsets — the register-once design of the single-player score
block (`score.py`), but per side and at the 2-player scale.

**Registration.** The amp is static on screen (fixed rig), so one reference frame
builds the chrome reference and registration only absorbs minor per-rig offset.
The fiducial is the static dark panel texture, marked by a hand-painted magenta
mask (`amp2p_<side>_mask.png`); the digit strip and medallion interior are excluded
because they change with the score / multiplier / portrait. Matching is masked,
per-frame-normalized SAD over an integer offset search (mirrors `calibrate_score`).

**Score digits.** The strip is a fixed grid of equal cells, right-aligned against a
fixed edge, so there is no ink segmentation step: the cell positions come straight
from `AMP2P_GRID` and each cell is classified independently. Which cells hold a
digit is read off the display itself — unused leading cells are *unpowered*, so a
per-cell luma-contrast gate gives the digit count without relying on the glyph
matcher. Cells are matched by integer coverage L1 against a bank of ten templates
(`covcore`, shared with `score.py`), because the font is not segment-decodable:
`1` is a centred bar and `4`/`7` carry diagonals.

The grid table covers 1-5 digits. A 6-digit score re-lays-out the strip and that
geometry is unmeasured, so `read_amp2p_score` reports `layout_unknown` rather than
returning a number the grid cannot justify.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .corpus import Sample, load_bgr, score_corpus_dir
from .covcore import cov_grid, ink_mask, luma_i
from .fingerprint import CANONICAL_H, CANONICAL_W
from .metadata import (
    AMP2P_BAND_H,
    AMP2P_BAND_Y0,
    AMP2P_BLANK_CONTRAST,
    AMP2P_BLOCK,
    AMP2P_GLYPH_COLS,
    AMP2P_GLYPH_ROWS,
    AMP2P_GRID,
    AMP2P_INK_DEN,
    AMP2P_INK_NUM,
    AMP2P_RIGHT_EDGE,
    AMP2P_UNK_DIST,
    AMP2P_UNK_MARGIN,
    AMP2P_VARIANTS,
    amp2p_score_from_filename,
)

SIDES = ("left", "right")
AMP2P_MAX_CELLS = max(AMP2P_GRID)

_LUMA_W = np.array([29, 150, 77], dtype=np.float64)  # BGR weights (matches score.py)


def _mask_path(side: str):
    return score_corpus_dir() / f"amp2p_{side}_mask.png"


def _ref_path(side: str):
    return score_corpus_dir() / f"amp2p_{side}_ref.png"


def load_amp2p_mask(side: str) -> np.ndarray:
    """Load the hand-painted registration mask as a (block_h, block_w) bool array.

    Magenta pixels mark the static chrome used for registration. The PNG may be at
    any integer upscale of the block; it is box-averaged down to block resolution.
    """
    x0, y0, x1, y1 = AMP2P_BLOCK[side]
    bw, bh = x1 - x0, y1 - y0
    a = load_bgr(_mask_path(side))  # magenta is channel-symmetric, so BGR/RGB moot
    mag = (a[..., 0] > 200) & (a[..., 2] > 200) & (a[..., 1] < 150)
    H, W = mag.shape
    if H % bh or W % bw:
        raise ValueError(f"amp2p {side} mask {W}x{H} is not an integer multiple of block {bw}x{bh}")
    fy, fx = H // bh, W // bw
    return mag.reshape(bh, fy, bw, fx).mean(axis=(1, 3)) >= 0.5


def block_size(side: str) -> tuple[int, int]:
    """The side's block shape as (h, w) — the marvin-perf region-strip size."""
    x0, y0, x1, y1 = AMP2P_BLOCK[side]
    return y1 - y0, x1 - x0


def _ensure_full_frame(image: np.ndarray, side: str) -> np.ndarray:
    """Return a canonical 720x480 frame for `side`.

    The marvin-perf region capture provides just the side's block; embed it into a
    black full frame at its real position so the absolute ROIs work unchanged. Both
    sides share a block size, hence `side` rather than a shape lookup.
    """
    a = np.asarray(image)
    if a.shape[:2] == (CANONICAL_H, CANONICAL_W):
        return a
    x0, y0, x1, y1 = AMP2P_BLOCK[side]
    if a.shape[:2] == (y1 - y0, x1 - x0):
        full = np.zeros((CANONICAL_H, CANONICAL_W, a.shape[2]), dtype=a.dtype)
        full[y0:y1, x0:x1] = a
        return full
    raise ValueError(
        f"amp2p {side}: expected a 720x480 frame or a {y1 - y0}x{x1 - x0} block, got {a.shape[:2]}"
    )


def _block_luma(image: np.ndarray, side: str, dx: int = 0, dy: int = 0) -> np.ndarray:
    """Float luma of the side's block at its nominal position shifted by (dx, dy)."""
    img = _ensure_full_frame(image, side)
    x0, y0, x1, y1 = AMP2P_BLOCK[side]
    return img[y0 + dy:y1 + dy, x0 + dx:x1 + dx].astype(np.float64) @ _LUMA_W


def _norm_masked(a: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Zero-mean/unit-std over the masked pixels (cancels A2D gain/offset)."""
    m = a[mask]
    return (a - m.mean()) / (m.std() + 1e-6)


@dataclass(frozen=True)
class AmpReference:
    side: str
    ref: np.ndarray   # (block_h, block_w) reference luma
    mask: np.ndarray  # (block_h, block_w) bool: static-chrome registration pixels


@dataclass(frozen=True)
class AmpCalibration:
    side: str
    dx: int = 0
    dy: int = 0


def build_reference(side: str, images: list[np.ndarray] | None = None) -> AmpReference:
    """Build a side's chrome reference (mask + reference luma).

    With no `images`, the reference luma is the committed clean crop
    (`amp2p_<side>_ref.png`, block-sized). Given full frames, it is their median
    block luma at the nominal position (the masked panel is static across frames,
    so the median cleans up the unmasked digit/portrait content).
    """
    mask = load_amp2p_mask(side)
    if images:
        ref = np.median(np.stack([_block_luma(im, side) for im in images]), axis=0)
    else:
        crop = load_bgr(_ref_path(side)).astype(np.float64) @ _LUMA_W
        ref = crop
    return AmpReference(side=side, ref=ref, mask=mask)


def calibrate(
    side: str,
    images: list[np.ndarray],
    reference: AmpReference,
    search: int = 8,
) -> AmpCalibration:
    """Lock the side's block offset for this rig by matching the static chrome.

    Over an integer offset search, sum the masked per-frame-normalized SAD between
    each frame's block and the reference across all frames; the `(dx, dy)` with the
    smallest total wins. Run once at gameplay start.
    """
    ref_n = _norm_masked(reference.ref, reference.mask)
    best: tuple[float, int, int] | None = None
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            total = 0.0
            for img in images:
                tn = _norm_masked(_block_luma(img, side, dx, dy), reference.mask)
                total += float(np.abs((tn - ref_n)[reference.mask]).sum())
            if best is None or total < best[0]:
                best = (total, dx, dy)
    assert best is not None
    return AmpCalibration(side=side, dx=best[1], dy=best[2])


# ─── score digits (fixed grid + per-cell coverage match) ────────────────────────


@dataclass(frozen=True)
class AmpDigitTemplate:
    digit: int         # 0-9
    vec: np.ndarray    # uint8 ink-coverage mask, length AMP2P_GLYPH_ROWS*COLS


@dataclass(frozen=True)
class AmpDigitBank:
    templates: list[AmpDigitTemplate]

    @property
    def digits(self) -> tuple[int, ...]:
        return tuple(t.digit for t in self.templates)


@dataclass(frozen=True)
class Amp2pScoreRead:
    side: str
    value: int | None         # None when the read cannot be trusted
    digits: tuple[int, ...]   # per powered cell, left->right
    n_cells: int              # powered cells found (0 = display blank/off)
    dist: float               # worst (max) per-cell best-distance
    margin: float             # weakest (min) per-cell runner-up gap
    layout_unknown: bool      # grid table cannot describe what is on screen
    reason: str = ""          # why `value` is None (empty when it is set)


def cell_bounds(side: str, n: int) -> list[tuple[int, int]]:
    """Block-local x spans [x0, x1) of `n` right-aligned digit cells, left->right."""
    if n not in AMP2P_GRID:
        raise KeyError(f"no amp2p grid for {n} digits")
    w, pitch = AMP2P_GRID[n]
    right = AMP2P_RIGHT_EDGE[side]
    # The rightmost cell ends at `right`; each earlier cell steps back one pitch.
    return [(right - w - (n - 1 - i) * pitch, right - (n - 1 - i) * pitch) for i in range(n)]


def grid_span(side: str) -> tuple[int, int]:
    """Block-local x span [x0, x1) the digit strip can occupy at its widest."""
    return cell_bounds(side, AMP2P_MAX_CELLS)[0][0], AMP2P_RIGHT_EDGE[side]


def _band_luma(image: np.ndarray, side: str, dx: int = 0, dy: int = 0) -> np.ndarray:
    """Integer luma of the digit band across the whole block, at the locked offset.

    Full block width so a block-local x from `cell_bounds` indexes the result
    directly; the ink threshold is derived from the grid span alone (`_band_ink`).
    """
    img = _ensure_full_frame(image, side)
    bx0, by0, bx1, _by1 = AMP2P_BLOCK[side]
    y0 = by0 + AMP2P_BAND_Y0 + dy
    return luma_i(img[y0:y0 + AMP2P_BAND_H, bx0 + dx:bx1 + dx])


def _band_ink(band: np.ndarray, side: str) -> np.ndarray:
    """Ink mask of the band, thresholded on the digit-grid span only.

    The band spans the whole block, but the amp's bright gold chrome sits outside
    the strip and would otherwise set the range and wash the digits out.
    """
    gx0, gx1 = grid_span(side)
    return ink_mask(band, AMP2P_INK_NUM, AMP2P_INK_DEN, range_from=band[:, gx0:gx1])


def cell_is_powered(band: np.ndarray, span: tuple[int, int]) -> bool:
    """Whether a cell is lit at all.

    Unused leading cells are unpowered, so their luma range is just the panel's
    dark gradient; a cell holding a digit spans several times that. Independent of
    the glyph matcher, which is what makes it the digit-count signal.
    """
    x0, x1 = span
    cell = band[:, x0:x1]
    return int(cell.max()) - int(cell.min()) >= AMP2P_BLANK_CONTRAST


def cell_cov(band: np.ndarray, span: tuple[int, int]) -> np.ndarray:
    """Ink-coverage fingerprint of one cell (rows tightened to the glyph bbox).

    The ink threshold is relative to *this cell*, not the band: the LEDs pulse, so
    a bright digit's strokes bloom about a pixel wider than a dim one's. Under a
    band-wide threshold the brightest digit sets the range and a dim `9` thins into
    a `5`; per-cell normalization removes the brightness phase (the same reason the
    streak reader thresholds per tumbler).

    Columns are *not* tightened: the cell is monospaced, so horizontal position
    inside it separates glyphs (`1` is a centred bar).
    """
    x0, x1 = span
    mask = ink_mask(band[:, x0:x1], AMP2P_INK_NUM, AMP2P_INK_DEN)
    ys = np.where(mask.any(1))[0]
    if len(ys):
        mask = mask[ys.min():ys.max() + 1, :]
    return cov_grid(mask, AMP2P_GLYPH_ROWS, AMP2P_GLYPH_COLS)


def _powered_cells(band: np.ndarray, side: str) -> tuple[int, bool]:
    """(digit count, right_aligned) from the powered-cell pattern over the max grid."""
    spans = cell_bounds(side, AMP2P_MAX_CELLS)
    lit = [cell_is_powered(band, s) for s in spans]
    n = sum(lit)
    # Powered cells must form a contiguous run ending at the right edge.
    return n, lit[AMP2P_MAX_CELLS - n:] == [True] * n


def _grid_aligned(band: np.ndarray, side: str, n: int) -> bool:
    """Whether the band's ink actually sits on the `n`-cell grid.

    The layout guard. Ink is expected inside every cell and absent from the 1 px
    gaps between them; a strip the game re-laid-out for a wider value fails both,
    which is how an unmeasured layout announces itself instead of being misread.
    """
    mask = _band_ink(band, side)
    spans = cell_bounds(side, n)
    if not all(mask[:, x0:x1].any() for x0, x1 in spans):
        return False
    gaps = [(a[1], b[0]) for a, b in zip(spans, spans[1:]) if b[0] > a[1]]
    return not any(mask[:, x0:x1].any() for x0, x1 in gaps)


def _variant_means(exemplars: list[np.ndarray], k: int) -> list[np.ndarray]:
    """Split exemplars into at most `k` groups and return each group's mean.

    Deterministic k-means seeded by the two most distant exemplars, so a corpus
    always yields the same bank. `k` groups model the LED's brightness phases.
    Exemplars are sorted first, so the result depends on the corpus as a *set* and
    not on the order the loader happened to walk it.
    """
    X = np.stack(sorted(exemplars, key=lambda v: v.tobytes())).astype(np.float64)
    if len(X) <= k or k <= 1:
        return [X.mean(0)] if k <= 1 else list(X)
    d = np.abs(X[:, None, :] - X[None]).sum(-1)
    i, j = np.unravel_index(int(np.argmax(d)), d.shape)
    cent = X[[i, j]]
    for _ in range(k - 2):  # farthest-point init for any extra centroids
        far = np.abs(X[:, None, :] - cent[None]).sum(-1).min(1)
        cent = np.vstack([cent, X[int(np.argmax(far))]])
    for _ in range(50):
        lab = np.abs(X[:, None, :] - cent[None]).sum(-1).argmin(1)
        new = np.stack([X[lab == g].mean(0) if (lab == g).any() else cent[g] for g in range(k)])
        if np.allclose(new, cent):
            break
        cent = new
    lab = np.abs(X[:, None, :] - cent[None]).sum(-1).argmin(1)
    return [X[lab == g].mean(0) for g in range(k) if (lab == g).any()]


def build_amp2p_bank(samples: list[Sample], variants: int = AMP2P_VARIANTS) -> AmpDigitBank:
    """Build the 0-9 coverage templates from the labelled 2-player corpus.

    Each frame's powered cells map left->right onto the digits of its filename value
    (the counts must agree, else the frame is skipped). Each digit contributes up to
    `variants` templates rather than one average — see AMP2P_VARIANTS. Both sides
    feed one bank: same glyph art at the same scale, only the block origin differs.
    """
    acc: dict[int, list[np.ndarray]] = {}
    for s in samples:
        parsed = amp2p_score_from_filename(s.path.name)
        if parsed is None:
            continue
        side, value = parsed
        band = _band_luma(s.image, side)
        digits = str(value)
        if len(digits) not in AMP2P_GRID:
            continue
        n, aligned = _powered_cells(band, side)
        if n != len(digits) or not aligned:
            continue  # the display disagrees with the label — skip this frame
        for span, ch in zip(cell_bounds(side, n), digits):
            acc.setdefault(int(ch), []).append(cell_cov(band, span))
    templates = [
        AmpDigitTemplate(digit=d, vec=np.round(v).astype(np.uint8))
        for d, ex in sorted(acc.items())
        for v in _variant_means(ex, variants)
    ]
    return AmpDigitBank(templates=templates)


def match_cell(bank: AmpDigitBank, vec: np.ndarray) -> tuple[int, float, float]:
    """(digit, best L1, runner-up gap) for one cell's coverage vector."""
    vecs = np.stack([t.vec.astype(np.int32) for t in bank.templates])
    tdig = np.array([t.digit for t in bank.templates])
    d = np.abs(vecs - vec.astype(np.int32)).sum(1)
    order = np.argsort(d)
    best = int(tdig[order[0]])
    other = next((float(d[i]) for i in order if int(tdig[i]) != best), float(d[order[0]]))
    return best, float(d[order[0]]), other - float(d[order[0]])


def read_amp2p_score(
    image: np.ndarray,
    bank: AmpDigitBank,
    calibration: AmpCalibration | None = None,
    side: str | None = None,
) -> Amp2pScoreRead:
    """Read one amp's score: powered-cell count, then classify each cell.

    The digit count comes from the display (which cells are lit), the cell
    positions from `AMP2P_GRID`, and the digits from an argmin integer L1 against
    the bank. `value` is None — with `reason` set — whenever the grid or the match
    cannot justify a number: a non-right-aligned lit pattern, a digit count the
    grid table does not cover, ink that does not sit on the grid, or a cell that
    fails the distance/margin gates. `calibration` supplies the locked block
    offset; if omitted the nominal position is used.
    """
    if side is None:
        side = calibration.side if calibration is not None else None
    if side not in AMP2P_BLOCK:
        raise ValueError(f"amp2p: side must be one of {SIDES}, got {side!r}")
    dx = calibration.dx if calibration is not None else 0
    dy = calibration.dy if calibration is not None else 0

    def fail(n: int, reason: str, layout_unknown: bool = False) -> Amp2pScoreRead:
        return Amp2pScoreRead(
            side=side, value=None, digits=(), n_cells=n, dist=0.0, margin=0.0,
            layout_unknown=layout_unknown, reason=reason,
        )

    band = _band_luma(image, side, dx, dy)
    n, aligned = _powered_cells(band, side)
    if n == 0:
        return fail(0, "no powered cells")
    if not aligned:
        return fail(n, "lit cells are not right-aligned", layout_unknown=True)
    if n == AMP2P_MAX_CELLS and not _grid_aligned(band, side, n):
        # Every cell lit *and* the ink off-grid is what a re-laid-out wider strip
        # looks like through the widest grid we have measured.
        return fail(n, f"ink does not sit on the {n}-cell grid", layout_unknown=True)

    digits: list[int] = []
    dists: list[float] = []
    margins: list[float] = []
    for span in cell_bounds(side, n):
        d, dist, margin = match_cell(bank, cell_cov(band, span))
        digits.append(d)
        dists.append(dist)
        margins.append(margin)

    worst, weakest = max(dists), min(margins)
    if worst > AMP2P_UNK_DIST or weakest < AMP2P_UNK_MARGIN:
        return Amp2pScoreRead(
            side=side, value=None, digits=tuple(digits), n_cells=n,
            dist=worst, margin=weakest, layout_unknown=False,
            reason=f"weak match (dist={worst:.0f} margin={weakest:.0f})",
        )
    return Amp2pScoreRead(
        side=side, value=int("".join(str(x) for x in digits)), digits=tuple(digits),
        n_cells=n, dist=worst, margin=weakest, layout_unknown=False,
    )


# ─── corpus growth ─────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class GrowCandidate:
    path: str
    value: int          # the current bank's reading (the proposed label)
    dist: float
    margin: float


def grow_candidates(
    frames_dir,
    bank: AmpDigitBank,
    side: str,
    calibration: AmpCalibration | None = None,
    limit: int = 8,
    have_values: set[int] | None = None,
) -> tuple[list[GrowCandidate], int, int]:
    """Pick the frames a new capture should contribute to the labelled corpus.

    Returns `(candidates, n_settled, n_frames)`. Candidates are the *weakest* reads
    the current bank makes — lowest runner-up margin first, then highest distance —
    because those are the glyph renderings the bank does not yet cover, and adding
    them is what widens its margins. Two filters keep bad exemplars out:

    - **Settled only.** A frame is eligible only if its neighbours read the same
      value, so no mid-transition cell can enter a template (the policy the streak
      corpus audit set).
    - **Confident only.** A gated read has no trustworthy label, so it cannot be
      proposed; a capture that is *all* gated means the bank does not fit that
      footage and needs the bootstrap path, not growth.

    The proposed label is the bank's own reading, so the caller must eyeball the
    montage before committing — this narrows the labelling job, it does not replace it.
    """
    from pathlib import Path

    files = [f for f in sorted(Path(frames_dir).glob("*.png"))
             if load_bgr(f).shape[:2] in (block_size(side), (CANONICAL_H, CANONICAL_W))]
    reads = [read_amp2p_score(load_bgr(f), bank, calibration, side) for f in files]
    settled = [
        (f, r) for i, (f, r) in enumerate(zip(files, reads))
        if 0 < i < len(files) - 1
        and r.value is not None
        and reads[i - 1].value == r.value == reads[i + 1].value
    ]
    ranked = sorted(settled, key=lambda fr: (fr[1].margin, -fr[1].dist))
    seen: set[int] = set(have_values or ())
    out: list[GrowCandidate] = []
    for f, r in ranked:
        if r.value in seen:  # one exemplar per value; skip what the corpus already has
            continue
        seen.add(r.value)
        out.append(GrowCandidate(path=str(f), value=r.value, dist=r.dist, margin=r.margin))
        if len(out) >= limit:
            break
    return out, len(settled), len(files)


def corpus_name(side: str, value: int, source: str) -> str:
    """The labelled-corpus filename for one amp frame."""
    return f"score2p__{side}__{value:05d}__{source}.png"
