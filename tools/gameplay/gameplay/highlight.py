"""Static-list highlight reader: which menu row is selected.

Given an already-classified static-list screen and its `MenuLayout`, divide the
menu band into `count` equal cells along the layout axis and decide which cell is
selected. GH3 marks the selected item by *changing* it (a highlight bar, or
brighter/colour-shifted text) — not always by making it the brightest — so the
robust signal is **per-cell deviation from that cell's unselected appearance**:
the selected cell is the one that has changed most from how it looks when not
selected.

That unselected appearance is learned per screen from the corpus (a
`SelectionCalibration`, analogous to the classifier's centroids; the firmware
port would bake these). Two details make it work:

- Baseline per cell uses only the samples where that cell is *unselected*, so it
  is non-degenerate even for two-item screens (where a median-over-all collapses
  to the midpoint and can't tell the cells apart).
- Cell colours are normalized per frame (subtract the mean cell colour, scale by
  the spread) before comparing, which cancels the global gain/offset value-slop of
  the analog-component -> HDMI path. This takes slop robustness from ~90% to ~99%.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .corpus import Sample
from .fingerprint import CANONICAL_H, CANONICAL_W, _to_canonical
from .metadata import HORIZONTAL, MENU_LAYOUTS, MenuLayout, selected_item_from_filename


@dataclass(frozen=True)
class SelectionResult:
    index: int  # selected cell index (= item index, items are in screen order)
    item: str
    scores: tuple[float, ...]  # per-cell deviation from unselected baseline
    margin: float  # best - second-best (confidence; higher = less ambiguous)


@dataclass(frozen=True)
class SelectionCalibration:
    """Per-screen, per-cell unselected baseline colours (normalized space)."""

    baselines: dict[str, np.ndarray]  # screen_id -> (count, 3) float


def _cell_bounds(layout: MenuLayout) -> list[tuple[int, int, int, int]]:
    x0, y0, x1, y1 = layout.band
    n = layout.count
    if layout.axis == HORIZONTAL:
        edges = np.linspace(x0, x1, n + 1).round().astype(int)
        return [(int(edges[i]), y0, int(edges[i + 1]), y1) for i in range(n)]
    edges = np.linspace(y0, y1, n + 1).round().astype(int)
    return [(x0, int(edges[i]), x1, int(edges[i + 1])) for i in range(n)]


def cell_colors(image: np.ndarray, layout: MenuLayout) -> np.ndarray:
    """Per-frame-normalized mean BGR of each menu cell — shape (count, 3).

    Normalization (subtract the mean cell colour, divide by the spread) makes the
    reader invariant to global gain/offset.
    """
    img = _to_canonical(image)
    rows = []
    for (x0, y0, x1, y1) in _cell_bounds(layout):
        x0c, x1c = max(0, x0), min(CANONICAL_W, x1)
        y0c, y1c = max(0, y0), min(CANONICAL_H, y1)
        rows.append(img[y0c:y1c, x0c:x1c].reshape(-1, 3).mean(axis=0))
    c = np.asarray(rows, dtype=np.float64)
    c -= c.mean(axis=0)
    std = c.std()
    return c / std if std > 1e-6 else c


def build_selection_calibration(samples: list[Sample]) -> SelectionCalibration:
    """Learn each modelled screen's per-cell unselected baseline from the corpus.

    For each cell, average its (normalized) colour over the samples in which that
    cell is *not* the selected one.
    """
    baselines: dict[str, np.ndarray] = {}
    for sid, layout in MENU_LAYOUTS.items():
        acc: list[list[np.ndarray]] = [[] for _ in range(layout.count)]
        for s in samples:
            if s.screen_id != sid:
                continue
            item = selected_item_from_filename(s.path.name)
            if item is None or item not in layout.items:
                continue
            sel = layout.index_of(item)
            cc = cell_colors(s.image, layout)
            for c in range(layout.count):
                if c != sel:
                    acc[c].append(cc[c])
        if all(acc):  # every cell has at least one unselected exemplar
            baselines[sid] = np.stack([np.mean(a, axis=0) for a in acc])
    return SelectionCalibration(baselines=baselines)


def read_selection(
    image: np.ndarray, layout: MenuLayout, calibration: SelectionCalibration
) -> SelectionResult:
    baseline = calibration.baselines[layout.screen_id]
    cc = cell_colors(image, layout)
    dev = np.linalg.norm(cc - baseline, axis=1)
    order = np.argsort(dev)[::-1]
    best = int(order[0])
    margin = float(dev[best] - (dev[int(order[1])] if len(order) > 1 else 0.0))
    return SelectionResult(
        index=best, item=layout.items[best], scores=tuple(float(d) for d in dev), margin=margin
    )
