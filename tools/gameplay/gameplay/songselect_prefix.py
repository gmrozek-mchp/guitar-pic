"""song_select reader (prototype): identify the song from title **length + prefix**.

Alternative to `songselect.read_song`, which matches the whole title+artist slot
bitmap against a per-song template. That whole-slot match dilutes the
discriminative signal across the entire title, so titles that render to a similar
overall ink pattern sit close together.

Character analysis of the 64-song catalog shows the identifying signal is
concentrated at the front: **(rendered title length, first ~2 characters)
uniquely separates all 64 songs**. This reader leans on exactly that — it reads
only the title line, and from it takes two cheap features:

- **length** — the horizontal ink extent of the title (left-most to right-most
  inked column in the title band), normalized by ROI width; and
- **prefix** — a low-res normalized luma grid over just the *leading* window of
  the title band (the first few characters' ink pattern).

Match is still closed-set nearest-template (we have a reference frame of every
song), but the template is (length, prefix-grid) rather than the whole slot, and
the distance is `prefix_L1 + length_weight * |dlength|`. This is "minimal OCR" in
the region sense: read the discriminating leading region, not the whole string —
it does not yet emit character labels.

The title band sits in the top rows of the slot ROI (the artist/year line is
below it); `TITLE_BAND` is the row range within `SONG_SLOT_ROI`/`SONG_FIRST_ROI`.
Everything is integer/soft-float-friendly for the eventual firmware port, mirroring
`songselect`.

Leaves `songselect.read_song` in place — this is a parallel prototype.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

import numpy as np

from .corpus import Sample
from .fingerprint import _to_canonical
from .metadata import (
    SONG_FIRST_ROI,
    SONG_SLOT_ROI,
    song_from_filename,
)

# Luma weights (B, G, R) /256 — same convention as songselect.
_LUMA_W = np.array([29, 150, 77], dtype=np.float64)

FIRST = "first"
SLOT = "slot"

# Title line within the slot ROI, as row fractions of the ROI height. From the
# corpus ink profile the title occupies the top ~30% (artist/year is below).
TITLE_BAND = (0.0, 0.30)


@dataclass(frozen=True)
class PrefixConfig:
    # Prefix grid: a few cells across the leading window, a couple of rows down
    # the (single) title line.
    prefix_cols: int = 12
    prefix_rows: int = 3
    # Leading window width, as a fraction of the ROI width, that the prefix grid
    # covers. ~0.28 of a 330px ROI ≈ 92px ≈ the first 6-7 glyphs.
    prefix_frac: float = 0.28
    # Relative weight of the length term vs the prefix-grid L1 in the match score.
    # length is in [0,1]; the prefix L1 is ~tens, so this scales |dlength| up to a
    # comparable range. 0 => prefix-only; large => length-dominated.
    length_weight: float = 120.0
    # Ink threshold for length extent: fraction of the peak column energy in the
    # title band that a column must exceed to count as inked.
    ink_frac: float = 0.18
    # Read-time offset search (px), as in songselect — re-aligns the slot before
    # scoring so the fine grid and the length extent tolerate analog positional slop.
    dx_search: tuple[int, ...] = (-6, -3, 0, 3, 6)
    dy_search: tuple[int, ...] = (-3, 0, 3)


DEFAULT_PREFIX_CONFIG = PrefixConfig()


@dataclass(frozen=True)
class PrefixFeature:
    length: float          # normalized title ink extent [0,1]
    prefix: np.ndarray     # normalized leading-window luma grid


@dataclass(frozen=True)
class PrefixTemplate:
    setlist: str
    index: int
    song_id: str
    roi_kind: str  # SLOT or FIRST
    feat: PrefixFeature


@dataclass(frozen=True)
class PrefixCatalog:
    templates: list[PrefixTemplate]
    config: PrefixConfig


@dataclass(frozen=True)
class PrefixResult:
    setlist: str
    index: int
    song_id: str
    dist: float
    margin: float  # 2nd-best dist - best dist (higher = more confident)


def _title_band(roi: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    """Sub-ROI covering just the title line (top band of the slot ROI)."""
    x0, y0, x1, y1 = roi
    h = y1 - y0
    ty0 = y0 + int(round(h * TITLE_BAND[0]))
    ty1 = y0 + int(round(h * TITLE_BAND[1]))
    return (x0, ty0, x1, ty1)


def _luma(image: np.ndarray, roi: tuple[int, int, int, int]) -> np.ndarray:
    img = _to_canonical(image)
    x0, y0, x1, y1 = roi
    patch = img[y0:y1, x0:x1].astype(np.float64)
    return patch @ _LUMA_W / 256.0  # (h, w)


def _column_energy(luma: np.ndarray) -> np.ndarray:
    """Per-column ink energy: mean abs deviation from each row's median, so it
    responds to text (bright/dark strokes) and is offset/gain-tolerant."""
    return np.abs(luma - np.median(luma, axis=1, keepdims=True)).mean(axis=0)


def _title_length(luma: np.ndarray, cfg: PrefixConfig) -> float:
    """Normalized horizontal ink extent of the title band, in [0,1]."""
    e = _column_energy(luma)
    if e.size == 0:
        return 0.0
    thr = cfg.ink_frac * e.max()
    inked = np.nonzero(e >= thr)[0]
    if inked.size == 0:
        return 0.0
    extent = int(inked[-1] - inked[0] + 1)
    return extent / luma.shape[1]


def _prefix_grid(luma: np.ndarray, cfg: PrefixConfig) -> np.ndarray:
    """Per-frame-normalized low-res luma grid over the leading window of the title
    band -> length prefix_cols*prefix_rows."""
    w = luma.shape[1]
    lead = luma[:, : max(1, int(round(w * cfg.prefix_frac)))]
    ys = np.linspace(0, lead.shape[0], cfg.prefix_rows + 1).round().astype(int)
    xs = np.linspace(0, lead.shape[1], cfg.prefix_cols + 1).round().astype(int)
    cells = np.empty((cfg.prefix_rows, cfg.prefix_cols), dtype=np.float64)
    for r in range(cfg.prefix_rows):
        for c in range(cfg.prefix_cols):
            cells[r, c] = lead[ys[r]:ys[r + 1], xs[c]:xs[c + 1]].mean()
    v = cells.reshape(-1)
    v -= v.mean()
    s = v.std()
    return v / s if s > 1e-6 else v


def _feature(image: np.ndarray, roi: tuple[int, int, int, int], cfg: PrefixConfig) -> PrefixFeature:
    luma = _luma(image, _title_band(roi))
    return PrefixFeature(length=_title_length(luma, cfg), prefix=_prefix_grid(luma, cfg))


def build_prefix_catalog(
    samples: list[Sample], config: PrefixConfig = DEFAULT_PREFIX_CONFIG
) -> PrefixCatalog:
    templates: list[PrefixTemplate] = []
    for s in samples:
        if s.screen_id != "song_select":
            continue
        parsed = song_from_filename(s.path.name)
        if parsed is None:
            continue
        setlist, index, song_id = parsed
        roi_kind = FIRST if index == 0 else SLOT
        roi = SONG_FIRST_ROI if roi_kind == FIRST else SONG_SLOT_ROI
        templates.append(
            PrefixTemplate(setlist, index, song_id, roi_kind, _feature(s.image, roi, config))
        )
    return PrefixCatalog(templates=templates, config=config)


def _shift(roi: tuple[int, int, int, int], dx: int, dy: int) -> tuple[int, int, int, int]:
    x0, y0, x1, y1 = roi
    return (x0 + dx, y0 + dy, x1 + dx, y1 + dy)


def read_song_prefix(
    image: np.ndarray, catalog: PrefixCatalog, setlist: str | None = None
) -> PrefixResult:
    """Identify the selected song by nearest (length, prefix-grid) template.

    Matches across the whole catalog by default (like `songselect.read_song`); the
    setlist falls out of the winning song. A small offset search per candidate ROI
    re-aligns the slot before scoring.
    """
    cfg = catalog.config
    cand = [t for t in catalog.templates if setlist is None or t.setlist == setlist]

    # One feature per (roi_kind, offset), reused across all templates.
    feats: dict[tuple[str, int, int], PrefixFeature] = {}
    for dx in cfg.dx_search:
        for dy in cfg.dy_search:
            feats[(SLOT, dx, dy)] = _feature(image, _shift(SONG_SLOT_ROI, dx, dy), cfg)
            feats[(FIRST, dx, dy)] = _feature(image, _shift(SONG_FIRST_ROI, dx, dy), cfg)

    def best_dist(t: PrefixTemplate) -> float:
        best = float("inf")
        for dx in cfg.dx_search:
            for dy in cfg.dy_search:
                f = feats[(t.roi_kind, dx, dy)]
                d = float(np.abs(f.prefix - t.feat.prefix).sum())
                d += cfg.length_weight * abs(f.length - t.feat.length)
                if d < best:
                    best = d
        return best

    scored = sorted(((best_dist(t), t) for t in cand), key=lambda kv: kv[0])
    best_d, best = scored[0]
    second_d = scored[1][0] if len(scored) > 1 else best_d
    return PrefixResult(
        setlist=best.setlist, index=best.index, song_id=best.song_id,
        dist=best_d, margin=second_d - best_d,
    )
