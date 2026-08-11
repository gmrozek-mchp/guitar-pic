"""Render marvin's cv_marvin_v1 sample points over a 720x480 corpus frame.

Parses the sensor tables out of cv_marvin_v1.c (any revision) so the picture
always matches the source. Usage:

    uv run python render_cv_points.py <cv_marvin_v1.c> <out_dir> [--old <old.c>]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[2]
SCREENS = REPO / "firmware" / "marvin" / "docs" / "gh3_screens"

FRETS = ["GREEN", "RED", "YELLOW", "BLUE", "ORANGE"]
FRET_RGB = {
    "GREEN": (0, 255, 0),
    "RED": (255, 40, 40),
    "YELLOW": (255, 235, 0),
    "BLUE": (60, 130, 255),
    "ORANGE": (255, 150, 0),
}

# Which corpus frame each config's highway lives on.
FRAMES = {
    "1p": "in_song__training.png",
    "2p-left": "in_song_2p__0200.png",
}


def parse_configs(src: str) -> dict[str, dict]:
    """{name: {"sensor": {fret: (hx, hy, ex, ey)}, "sensing": (x, y, w, h)}}."""
    out: dict[str, dict] = {}
    for block in re.findall(r"const cv_marvin_v1_config_t\s+\w+\s*=\s*\{(.*?)\n\};", src, re.S):
        name = re.search(r'\.name\s*=\s*"([^"]+)"', block).group(1)
        sensor = {}
        for fret in FRETS:
            m = re.search(
                r"\[FRET_%s\]\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}" % fret, block
            )
            sensor[fret] = tuple(int(g) for g in m.groups())
        rect = re.search(
            r"\.sensing_x\s*=\s*(\d+)u?,\s*\.sensing_y\s*=\s*(\d+)u?,"
            r"\s*\.sensing_w\s*=\s*(\d+)u?,\s*\.sensing_h\s*=\s*(\d+)u?",
            block,
        )
        out[name] = {
            "sensor": sensor,
            "sensing": tuple(int(g) for g in rect.groups()),
        }
    return out


def ring(d: ImageDraw.ImageDraw, x: float, y: float, r: float, color, width=1):
    d.ellipse([x - r, y - r, x + r, y + r], outline=color, width=width)


def cross(d: ImageDraw.ImageDraw, x: float, y: float, r: float, color, width=1):
    d.line([x - r, y - r, x + r, y + r], fill=color, width=width)
    d.line([x - r, y + r, x + r, y - r], fill=color, width=width)


def draw(img: Image.Image, cfg: dict, old: dict | None, scale: int):
    """Paint sample points onto an already-scaled image."""
    d = ImageDraw.Draw(img)
    sx, sy, sw, sh = cfg["sensing"]
    d.rectangle(
        [sx * scale, sy * scale, (sx + sw) * scale - 1, (sy + sh) * scale - 1],
        outline=(140, 140, 140),
        width=1,
    )
    for fret in FRETS:
        hx, hy, ex, ey = cfg["sensor"][fret]
        color = FRET_RGB[fret]
        # hold sensor: white ring + crosshair tick
        ring(d, hx * scale, hy * scale, 4 * scale, (255, 255, 255), width=max(1, scale // 2))
        # edge sensor: fret-colored ring, filled center
        ring(d, ex * scale, ey * scale, 4 * scale, color, width=max(1, scale // 2))
        d.ellipse(
            [ex * scale - scale, ey * scale - scale, ex * scale + scale, ey * scale + scale],
            fill=color,
        )
        # connector from hold to edge so the side is unambiguous
        d.line([hx * scale, hy * scale, ex * scale, ey * scale], fill=color, width=1)
        if old is not None:
            ox, oy = old["sensor"][fret][2:]
            if (ox, oy) != (ex, ey):
                cross(d, ox * scale, oy * scale, 3 * scale, color, width=max(1, scale // 3))


def render(name: str, new_cfg: dict, old_cfg: dict | None, out_dir: Path):
    frame = Image.open(SCREENS / FRAMES[name]).convert("RGB")

    full = frame.copy()
    draw(full, new_cfg, old_cfg, scale=1)
    full_path = out_dir / f"cv_points__{name}__full.png"
    full.save(full_path)

    # zoomed crop of the sensing band, with margin so rings aren't clipped
    sx, sy, sw, sh = new_cfg["sensing"]
    pad = 14
    box = (max(0, sx - pad), max(0, sy - pad), min(720, sx + sw + pad), min(480, sy + sh + pad))
    scale = 5
    crop = frame.crop(box).resize(
        ((box[2] - box[0]) * scale, (box[3] - box[1]) * scale), Image.NEAREST
    )
    # shift configs into crop space before drawing
    def shift(cfg):
        return {
            "sensor": {
                f: (v[0] - box[0], v[1] - box[1], v[2] - box[0], v[3] - box[1])
                for f, v in cfg["sensor"].items()
            },
            "sensing": (sx - box[0], sy - box[1], sw, sh),
        }

    draw(crop, shift(new_cfg), shift(old_cfg) if old_cfg else None, scale=scale)

    # caption strip: legend + per-fret hold/edge coords
    try:
        font = ImageFont.load_default(size=15)
        small = ImageFont.load_default(size=13)
    except TypeError:  # Pillow < 10
        font = small = ImageFont.load_default()
    bar_h = 90
    out = Image.new("RGB", (crop.width, crop.height + bar_h), (18, 18, 18))
    out.paste(crop, (0, 0))
    d = ImageDraw.Draw(out)
    y0 = crop.height + 6
    d.text(
        (8, y0),
        f"{name}  sensing {sx},{sy} {sw}x{sh}   "
        "white ring = hold (hx,hy) | colored ring = edge (ex,ey) | x = previous edge",
        fill=(210, 210, 210),
        font=font,
    )
    for idx, fret in enumerate(FRETS):
        hx, _, ex, _ = new_cfg["sensor"][fret]
        ox = old_cfg["sensor"][fret][2] if old_cfg else ex
        side = "left" if ex < hx else "right"
        edge = f"{ex} ({side})" if ox == ex else f"{ox} -> {ex} ({side})"
        d.text(
            (8 + (idx % 3) * 330, y0 + 24 + (idx // 3) * 20),
            f"{fret[:3]}  hold {hx}   edge {edge}",
            fill=FRET_RGB[fret],
            font=small,
        )
    zoom_path = out_dir / f"cv_points__{name}__zoom.png"
    out.save(zoom_path)
    return full_path, zoom_path


def main() -> int:
    src = Path(sys.argv[1]).read_text()
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    old_src = None
    if "--old" in sys.argv:
        old_src = Path(sys.argv[sys.argv.index("--old") + 1]).read_text()

    new_cfgs = parse_configs(src)
    old_cfgs = parse_configs(old_src) if old_src else {}
    for name in FRAMES:
        for p in render(name, new_cfgs[name], old_cfgs.get(name), out_dir):
            print(p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
