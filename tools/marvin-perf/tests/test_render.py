"""Pixel-rendering tests. Skipped if Pillow / web layer not installed."""

from __future__ import annotations

import io

import pytest

PIL = pytest.importorskip("PIL")  # noqa: F841 — skip whole module if absent

from PIL import Image

from marvin_perf.web.render import StripRenderError, render_strip_png


def _decode(png_bytes: bytes) -> Image.Image:
    return Image.open(io.BytesIO(png_bytes)).convert("RGB")


def test_render_strip_png_roundtrip_dimensions() -> None:
    w, h = 240, 32
    bgr = b"".join(bytes([(i & 0xFF), 0, 0]) for i in range(w * h))
    png = render_strip_png(bgr, w=w, h=h)
    img = _decode(png)
    assert img.size == (w, h)


def test_render_strip_png_swaps_bgr_to_rgb() -> None:
    # Single pixel: wire byte order BGR=(B=10, G=20, R=30). PNG should
    # decode to RGB=(30, 20, 10).
    bgr = bytes([10, 20, 30])
    png = render_strip_png(bgr, w=1, h=1)
    img = _decode(png)
    r, g, b = img.getpixel((0, 0))
    assert (r, g, b) == (30, 20, 10)


def test_render_strip_png_preserves_pixel_position() -> None:
    # 2×1 strip: pixel0 = red on wire (B=0, G=0, R=255); pixel1 = blue
    # (B=255, G=0, R=0). After decode: pixel0=red, pixel1=blue.
    bgr = bytes([0, 0, 255, 255, 0, 0])
    png = render_strip_png(bgr, w=2, h=1)
    img = _decode(png)
    assert img.getpixel((0, 0)) == (255, 0, 0)  # red
    assert img.getpixel((1, 0)) == (0, 0, 255)  # blue


def test_render_strip_png_variable_dimensions() -> None:
    # The contract is per-record (w, h) — a 64×16 score region must work
    # via the same path as a 240×32 sensing strip.
    w, h = 64, 16
    bgr = bytes([42, 84, 126]) * (w * h)
    png = render_strip_png(bgr, w=w, h=h)
    img = _decode(png)
    assert img.size == (w, h)
    assert img.getpixel((0, 0)) == (126, 84, 42)
    assert img.getpixel((w - 1, h - 1)) == (126, 84, 42)


def test_render_strip_png_rejects_size_mismatch() -> None:
    with pytest.raises(StripRenderError):
        render_strip_png(b"\x00" * 100, w=240, h=32)


def test_render_strip_png_rejects_zero_dim() -> None:
    with pytest.raises(StripRenderError):
        render_strip_png(b"", w=0, h=0)
