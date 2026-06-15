"""Nearest-centroid screen classifier with an UNKNOWN reject rule.

Each screen class is represented by one centroid fingerprint (the mean over its
corpus samples) — a small fixed-size template, which is the per-state metadata the
firmware `gameplay_engine` will eventually carry. Classification is L1 distance to
every centroid, argmin, then a two-part accept test:

    accept best iff  d_best <= t_abs  AND  (d_second - d_best) >= t_margin

Otherwise the result is UNKNOWN. The navigator's recovery path depends on a
trustworthy UNKNOWN, so the margin gate (am I *clearly* closer to one class than the
next?) matters as much as the absolute gate. All math is integer L1 over uint8
vectors, matching the firmware target.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .fingerprint import FingerprintConfig, fingerprint
from .screens import UNKNOWN

# Coarse fallback thresholds for the default normalized 12x8 config; callers
# should prefer values recommended by `evaluate.recommend_thresholds` for the
# actual config in use (distance scale depends on grid size + normalization).
DEFAULT_T_ABS = 7000
DEFAULT_T_MARGIN = 600


@dataclass(frozen=True)
class Templates:
    ids: tuple[str, ...]
    centroids: np.ndarray  # (K, L) uint8
    config: FingerprintConfig


@dataclass(frozen=True)
class Classification:
    screen_id: str  # a canonical id, or UNKNOWN
    best_id: str  # nearest centroid regardless of reject (for diagnostics)
    best_dist: int
    second_dist: int

    @property
    def margin(self) -> int:
        return self.second_dist - self.best_dist

    @property
    def accepted(self) -> bool:
        return self.screen_id != UNKNOWN


def build_templates(
    labelled_fps: dict[str, list[np.ndarray]], config: FingerprintConfig
) -> Templates:
    """Build per-class centroids from labelled fingerprints (id -> [fp, ...])."""
    ids = tuple(sorted(labelled_fps))
    centroids = np.empty((len(ids), config.length), dtype=np.uint8)
    for i, sid in enumerate(ids):
        stack = np.stack(labelled_fps[sid]).astype(np.float64)
        centroids[i] = stack.mean(axis=0).round().clip(0, 255).astype(np.uint8)
    return Templates(ids=ids, centroids=centroids, config=config)


def _l1_to_centroids(fp: np.ndarray, centroids: np.ndarray) -> np.ndarray:
    diff = centroids.astype(np.int32) - fp.astype(np.int32)
    return np.abs(diff).sum(axis=1)


def classify_fp(
    fp: np.ndarray,
    templates: Templates,
    t_abs: int = DEFAULT_T_ABS,
    t_margin: int = DEFAULT_T_MARGIN,
) -> Classification:
    """Classify a precomputed fingerprint against templates."""
    dists = _l1_to_centroids(fp, templates.centroids)
    order = np.argsort(dists, kind="stable")
    best_i = int(order[0])
    best_d = int(dists[best_i])
    second_d = int(dists[order[1]]) if len(order) > 1 else (best_d + t_margin)
    best_id = templates.ids[best_i]

    accept = best_d <= t_abs and (second_d - best_d) >= t_margin
    return Classification(
        screen_id=best_id if accept else UNKNOWN,
        best_id=best_id,
        best_dist=best_d,
        second_dist=second_d,
    )


def classify_image(
    image: np.ndarray,
    templates: Templates,
    t_abs: int = DEFAULT_T_ABS,
    t_margin: int = DEFAULT_T_MARGIN,
) -> Classification:
    """Classify a raw BGR frame (fingerprints it with the templates' config)."""
    fp = fingerprint(image, templates.config)
    return classify_fp(fp, templates, t_abs=t_abs, t_margin=t_margin)
