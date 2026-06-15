"""song_select reader: which song is in the highlight slot, and which setlist.

`song_select` is fixed-slot — the selected song sits in a fixed highlight slot
(title + artist/year) while the list scrolls under it. We identify the song by
**matching that slot's low-resolution bitmap against the known per-song
templates** (closed-set nearest-match, the same idea as the screen classifier;
deliberately *not* char-level OCR — we have a reference image of every song).

Two wrinkles, both from the corpus:
- Each setlist's first song (Slow Ride on main, Avalancha on bonus) sits one row
  lower (the list can't scroll up past it), so its template comes from
  `SONG_FIRST_ROI`; every other song uses `SONG_SLOT_ROI`. At read time we
  fingerprint both ROIs and compare each template against the ROI it was built
  from.
- main vs bonus setlists overlap by position, so we first read which setlist tab
  is lit and match only within that catalog.

The slot fingerprint is a low-res grayscale grid over the ROI (fine enough to
separate ~40 short titles by their ink pattern), normalized per frame to cancel
the analog gain/offset slop. Integer-friendly for the firmware port.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

import numpy as np

from .corpus import Sample
from .fingerprint import _to_canonical
from .metadata import (
    SETLIST_BG_ROI,
    SONG_FIRST_ROI,
    SONG_SLOT_ROI,
    song_from_filename,
)

# Luma weights (B, G, R) /256.
_LUMA_W = np.array([29, 150, 77], dtype=np.float64)

FIRST = "first"
SLOT = "slot"


@dataclass(frozen=True)
class SongConfig:
    cols: int = 32  # horizontal cells across the title (captures the ink pattern)
    rows: int = 6   # title + artist split into a few rows
    # Small read-time offset search (px) that re-aligns the slot before matching —
    # this is what makes the fine grid tolerant of the analog path's positional
    # slop (without it, a few-px shift drops translate robustness to ~70%).
    dx_search: tuple[int, ...] = (-6, -3, 0, 3, 6)
    dy_search: tuple[int, ...] = (-3, 0, 3)


DEFAULT_SONG_CONFIG = SongConfig()


@dataclass(frozen=True)
class SongTemplate:
    setlist: str
    index: int
    song_id: str
    roi_kind: str  # SLOT or FIRST
    vec: np.ndarray


@dataclass(frozen=True)
class SongCatalog:
    templates: list[SongTemplate]
    config: SongConfig
    setlist_warmth_threshold: float  # bg (R-B) above this => main (yellow), else bonus


@dataclass(frozen=True)
class SongResult:
    setlist: str
    index: int
    song_id: str
    dist: float
    margin: float  # 2nd-best dist - best dist (higher = more confident)


def _luma_grid(image: np.ndarray, roi: tuple[int, int, int, int], cfg: SongConfig) -> np.ndarray:
    """Per-frame-normalized low-res luma grid over an ROI -> length cols*rows."""
    img = _to_canonical(image)
    x0, y0, x1, y1 = roi
    patch = img[y0:y1, x0:x1].astype(np.float64)
    luma = patch @ _LUMA_W / 256.0  # (h, w)
    ys = np.linspace(0, luma.shape[0], cfg.rows + 1).round().astype(int)
    xs = np.linspace(0, luma.shape[1], cfg.cols + 1).round().astype(int)
    cells = np.empty((cfg.rows, cfg.cols), dtype=np.float64)
    for r in range(cfg.rows):
        for c in range(cfg.cols):
            cells[r, c] = luma[ys[r]:ys[r + 1], xs[c]:xs[c + 1]].mean()
    v = cells.reshape(-1)
    v -= v.mean()
    s = v.std()
    return v / s if s > 1e-6 else v


def _bg_warmth(image: np.ndarray) -> float:
    """Warmth (R - B) of the page background — median over the BG ROI is robust to
    the song text painted over it. High = yellow (main), low = white (bonus)."""
    img = _to_canonical(image)
    x0, y0, x1, y1 = SETLIST_BG_ROI
    patch = img[y0:y1, x0:x1].reshape(-1, 3).astype(np.float64)
    med = np.median(patch, axis=0)  # (B, G, R)
    return float(med[2] - med[0])


def build_song_catalog(samples: list[Sample], config: SongConfig = DEFAULT_SONG_CONFIG) -> SongCatalog:
    templates: list[SongTemplate] = []
    warmth: dict[str, list[float]] = defaultdict(list)
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
            SongTemplate(setlist, index, song_id, roi_kind, _luma_grid(s.image, roi, config))
        )
        warmth[setlist].append(_bg_warmth(s.image))
    # Threshold midway between the two setlists' mean background warmth.
    main_w = np.mean(warmth["main"]) if warmth["main"] else 0.0
    bonus_w = np.mean(warmth["bonus"]) if warmth["bonus"] else 0.0
    threshold = float((main_w + bonus_w) / 2.0)
    return SongCatalog(templates=templates, config=config, setlist_warmth_threshold=threshold)


def read_setlist(image: np.ndarray, catalog: SongCatalog) -> str:
    """Which setlist is active ("main"/"bonus"), from the page background colour."""
    return "main" if _bg_warmth(image) >= catalog.setlist_warmth_threshold else "bonus"


def _shift(roi: tuple[int, int, int, int], dx: int, dy: int) -> tuple[int, int, int, int]:
    x0, y0, x1, y1 = roi
    return (x0 + dx, y0 + dy, x1 + dx, y1 + dy)


def read_song(image: np.ndarray, catalog: SongCatalog, setlist: str | None = None) -> SongResult:
    """Identify the selected song by nearest-template match.

    Matches across the whole catalog (both setlists) by default — main/bonus
    bitmaps are distinct, so the setlist falls out of the winning song and we
    don't depend on the (less reliable) tab read. Pass `setlist` to restrict.

    A small offset search per candidate ROI re-aligns the slot before scoring, so
    the fine grid tolerates the analog path's positional slop.
    """
    cfg = catalog.config
    cand = [t for t in catalog.templates if setlist is None or t.setlist == setlist]

    # One fingerprint per (roi_kind, offset), reused across all templates.
    fps: dict[tuple[str, int, int], np.ndarray] = {}
    for dx in cfg.dx_search:
        for dy in cfg.dy_search:
            fps[(SLOT, dx, dy)] = _luma_grid(image, _shift(SONG_SLOT_ROI, dx, dy), cfg)
            fps[(FIRST, dx, dy)] = _luma_grid(image, _shift(SONG_FIRST_ROI, dx, dy), cfg)

    def best_dist(t: SongTemplate) -> float:
        return min(
            float(np.abs(fps[(t.roi_kind, dx, dy)] - t.vec).sum())
            for dx in cfg.dx_search
            for dy in cfg.dy_search
        )

    scored = sorted(((best_dist(t), t) for t in cand), key=lambda kv: kv[0])
    best_d, best = scored[0]
    second_d = scored[1][0] if len(scored) > 1 else best_d
    return SongResult(
        setlist=best.setlist, index=best.index, song_id=best.song_id,
        dist=best_d, margin=second_d - best_d,
    )
