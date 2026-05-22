"""Pillow-backed pixel rendering. Pure functions; testable without uvicorn.

Strips ride the wire as packed BGR888 at on-record `(w, h)` dimensions. The
renderer treats `kind` as opaque metadata — sensing, strike, score, or any
future `perf_strip_kind_t` value all use the same path.
"""

from __future__ import annotations

import io

from PIL import Image


PERF_STRIP_BPP = 3  # BGR888 — only constant in the wire contract.


class StripRenderError(ValueError):
    """Raised when payload bytes do not match declared dimensions."""


def render_strip_png(bgr: bytes, *, w: int, h: int) -> bytes:
    """Render a packed BGR888 buffer to a PNG byte string.

    `bgr` must be exactly `w * h * 3` bytes. Pillow expects RGB, so we swap
    on the way in. Output is a single-frame PNG with no alpha channel.
    """
    expected = w * h * PERF_STRIP_BPP
    if len(bgr) != expected:
        raise StripRenderError(
            f"strip payload {len(bgr)} B != w*h*3 = {w}*{h}*3 = {expected} B"
        )
    if w <= 0 or h <= 0:
        raise StripRenderError(f"non-positive strip dims w={w} h={h}")

    # Pillow's "raw" decoder swizzles BGR → RGB natively in C, no Python loop.
    img = Image.frombytes("RGB", (w, h), bytes(bgr), "raw", "BGR")
    out = io.BytesIO()
    img.save(out, format="PNG", optimize=False, compress_level=1)
    return out.getvalue()
