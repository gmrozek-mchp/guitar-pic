"""Synthetic analog-slop perturbations for robustness testing.

The corpus snapshots are clean digital captures over the perf-log path, so they
lack the distortions the real Wii feed has: it is analog component video converted
to HDMI before capture. To test whether the classifier survives that, we inject
the two slop families it introduces:

- **value slop** — gain (brightness scaling), offset, sensor/analog noise;
- **positional slop** — sub-cell translation, slight scale (overscan in/out).

Transforms operate on uint8 HxWx3 BGR arrays and are deterministic given an rng.
`envelope()` yields a fixed, named battery used by `evaluate.py`.
"""

from __future__ import annotations

from collections.abc import Iterator

import numpy as np
from PIL import Image


def adjust_gain(img: np.ndarray, gain: float) -> np.ndarray:
    return np.clip(img.astype(np.float32) * gain, 0, 255).astype(np.uint8)


def adjust_offset(img: np.ndarray, offset: float) -> np.ndarray:
    return np.clip(img.astype(np.float32) + offset, 0, 255).astype(np.uint8)


def add_noise(img: np.ndarray, sigma: float, rng: np.random.Generator) -> np.ndarray:
    noise = rng.normal(0.0, sigma, size=img.shape)
    return np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)


def translate(img: np.ndarray, dx: int, dy: int) -> np.ndarray:
    """Shift by (dx, dy) px, filling exposed edges by replication."""
    h, w = img.shape[:2]
    padded = np.pad(img, ((abs(dy), abs(dy)), (abs(dx), abs(dx)), (0, 0)), mode="edge")
    y0, x0 = abs(dy) - dy, abs(dx) - dx
    return padded[y0 : y0 + h, x0 : x0 + w]


def rescale(img: np.ndarray, factor: float) -> np.ndarray:
    """Zoom about the centre by `factor`, then centre-crop/pad back to the
    original size (factor>1 = overscan crop, factor<1 = borders appear)."""
    h, w = img.shape[:2]
    nw, nh = max(1, round(w * factor)), max(1, round(h * factor))
    im = Image.fromarray(img, mode="RGB").resize((nw, nh), Image.BILINEAR)
    scaled = np.asarray(im, dtype=np.uint8)

    out = np.empty_like(img)
    # x axis
    if nw >= w:
        sx = (nw - w) // 2
        xs_src, xs_dst, cw = sx, 0, w
    else:
        dx = (w - nw) // 2
        xs_src, xs_dst, cw = 0, dx, nw
    if nh >= h:
        sy = (nh - h) // 2
        ys_src, ys_dst, ch = sy, 0, h
    else:
        dy = (h - nh) // 2
        ys_src, ys_dst, ch = 0, dy, nh

    if nw < w or nh < h:  # borders exposed — fill by edge replication
        edge = np.pad(scaled, ((0, 0), (0, 0), (0, 0)), mode="edge")
        out[:] = np.pad(
            edge,
            ((ys_dst, h - ys_dst - ch), (xs_dst, w - xs_dst - cw), (0, 0)),
            mode="edge",
        )
    else:
        out[ys_dst : ys_dst + ch, xs_dst : xs_dst + cw] = scaled[
            ys_src : ys_src + ch, xs_src : xs_src + cw
        ]
    return out


def envelope(img: np.ndarray, rng: np.random.Generator) -> Iterator[tuple[str, str, np.ndarray]]:
    """Yield (category, name, perturbed_image) over a fixed slop battery."""
    yield "gain", "gain-0.85", adjust_gain(img, 0.85)
    yield "gain", "gain-1.15", adjust_gain(img, 1.15)
    yield "offset", "offset-neg20", adjust_offset(img, -20)
    yield "offset", "offset-pos20", adjust_offset(img, 20)
    yield "noise", "noise-sigma6", add_noise(img, 6.0, rng)
    yield "noise", "noise-sigma12", add_noise(img, 12.0, rng)
    yield "translate", "shift-x4", translate(img, 4, 0)
    yield "translate", "shift-y4", translate(img, 0, 4)
    yield "translate", "shift-xy6", translate(img, -6, 6)
    yield "scale", "overscan-1.03", rescale(img, 1.03)
    yield "scale", "underscan-0.97", rescale(img, 0.97)
