#!/usr/bin/env python3
"""Process board photos into marvin's system-info asset tree.

Reads one photo per node from ./data/ (named <node>.<ext>, any size/aspect) and
writes the exact slot the firmware expects:

    <out>/<node>.png     288x620 RGB, fitted to the column and framed

The frame (1px border, rounded corners) is baked in here rather than drawn by the UI:
the photo is scanned out on its own hardware layer above the RGB565 canvas — which is
how it keeps 8 bits per channel — so no widget can draw over it. Being opaque over the
whole rect is also what lets the firmware tell the display controller to skip reading
the canvas underneath it.

The firmware validates the PNG header against NODE_ART_W x NODE_ART_H and skips any
file that doesn't match exactly (ui/node_art.h), so resizing here is not cosmetic
— an off-size photo silently shows a blank frame on the detail screen.

PNG, not JPEG: Legato's JPEG decoder gates its block writes on the renderer clip
rect, which is stale during marvin's offscreen boot decode, so a runtime JPEG lands
as noise. The album-art tiers are PNG for the same reason.

RGB here, not RGBA: the firmware decodes into an RGBA8888 slot but forces the alpha
byte opaque, because a PNG-without-alpha decode leaves it zero. An alpha channel in
this file would not survive that, which is why the corners are baked as opaque page
colour rather than left transparent.

The photo column is tall and narrow (288x620, ~1:2.15), which almost no camera photo
is. Centre-cropping a landscape shot to that would keep a thin vertical sliver, so
the default is `--fit contain`: scale the whole photo to fit the width and letterbox
the rest in the card's own zinc-900, which keeps the board recognizable. Use
`--fit cover` for a photo already framed portrait.

Node names must match ui/node_art.c's PHOTO table:
    marvin fauxmote guitar fretboard beatbox lemmy lightshow guitar-pic

Usage:
    cd tools/node-photos
    uv run process_node_photos.py                     # -> the marvin data tree
    uv run process_node_photos.py --fit cover
    uv run process_node_photos.py --out /Volumes/SDCARD/system/nodes
"""

import argparse
import sys
from pathlib import Path
from typing import Optional

from PIL import Image, ImageDraw, ImageOps

# Must match NODE_ART_W / NODE_ART_H in firmware/marvin/default/src/ui/node_art.h
SLOT_W = 288
SLOT_H = 620

# Must match the PHOTO table in firmware/marvin/default/src/ui/node_art.c. "guitar-pic"
# is the whole-rig shot for the project card, which is not a node.
NODES = ["marvin", "fauxmote", "guitar", "fretboard", "beatbox", "lemmy", "lightshow",
         "guitar-pic"]

# The card fill the letterbox blends into (Tailwind zinc-900, SCHEME_FILL_ZINC_900).
ZINC_900 = (24, 24, 27)

# The frame is baked in because the photo is scanned out on its own hardware layer
# (OVR1) above the RGB565 canvas, so no widget can draw over it. These reproduce what
# the canvas used to draw around the photo: CARD_R / a LE_WIDGET_BORDER_LINE panel on
# SCHEME_BACKGROUND, whose shadowDark is #404040, with the corners outside the radius
# eaten back to the page behind (SCHEME_BACKGROUND's base, black).
#
# CARD_R must track CARD_R in screens/system/screen_system.c — the mockup's rounded-2xl,
# which is 16 and not the 12 a Tailwind scale would suggest, because its theme overrides
# --radius-xl but leaves --radius-2xl at the default.
CARD_R = 16
BORDER = (0x40, 0x40, 0x40)
PAGE_BG = (0, 0, 0)

# Supersampling factor for the corner anti-aliasing. Downsampled with BOX (area
# average), which is what makes the result a true coverage value per pixel.
SS = 4

SRC_EXTS = (".png", ".jpg", ".jpeg", ".webp", ".tif", ".tiff", ".bmp")

# Default output: the marvin data tree that gets copied to the card, mirroring how
# process_gh3_cover_art.py installs straight into the art folder.
DEFAULT_OUT = Path(__file__).resolve().parents[2] / "firmware/marvin/data/system/nodes"


def find_source(src_dir: Path, node: str) -> Optional[Path]:
    for ext in SRC_EXTS:
        p = src_dir / f"{node}{ext}"
        if p.exists():
            return p
    # tolerate a suffixed name like "guitar_v2.jpg" so drops don't need renaming
    hits = sorted(
        p for p in src_dir.iterdir()
        if p.is_file() and p.suffix.lower() in SRC_EXTS and p.stem.split("_")[0] == node
    )
    return hits[0] if hits else None


def rounded_coverage(w: int, h: int, radius: int) -> Image.Image:
    """Per-pixel coverage (L mode) of a rounded rectangle filling w x h."""
    big = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        (0, 0, w * SS - 1, h * SS - 1), radius=radius * SS, fill=255
    )
    return big.resize((w, h), Image.BOX)


def add_frame(photo: Image.Image) -> Image.Image:
    """Bake the 1px rounded border and the page-coloured corners into the photo.

    Built back-to-front so both edges anti-alias against what is actually behind them:
    the page colour, then the border rounded over it, then the photo rounded inside the
    border. The result is opaque over the whole rect, which is what lets the firmware
    mark it for BASE discard.
    """
    out = Image.new("RGB", (SLOT_W, SLOT_H), PAGE_BG)
    out.paste(Image.new("RGB", (SLOT_W, SLOT_H), BORDER),
              (0, 0), rounded_coverage(SLOT_W, SLOT_H, CARD_R))

    inner = (SLOT_W - 2, SLOT_H - 2)
    out.paste(photo.crop((1, 1, SLOT_W - 1, SLOT_H - 1)),
              (1, 1), rounded_coverage(inner[0], inner[1], max(CARD_R - 1, 0)))
    return out


def render(src: Path, fit: str) -> Image.Image:
    img = ImageOps.exif_transpose(Image.open(src)).convert("RGB")

    if fit == "cover":
        photo = ImageOps.fit(img, (SLOT_W, SLOT_H), method=Image.LANCZOS, centering=(0.5, 0.5))
    else:
        # contain: scale to fit inside the slot, then centre on the card's own fill
        scaled = img.copy()
        scaled.thumbnail((SLOT_W, SLOT_H), Image.LANCZOS)
        photo = Image.new("RGB", (SLOT_W, SLOT_H), ZINC_900)
        photo.paste(scaled, ((SLOT_W - scaled.width) // 2, (SLOT_H - scaled.height) // 2))

    return add_frame(photo)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--source", type=Path, default=Path(__file__).parent / "data")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--fit", choices=("contain", "cover"), default="contain")
    a = ap.parse_args()

    if not a.source.is_dir():
        print(f"no source dir {a.source}", file=sys.stderr)
        return 1
    a.out.mkdir(parents=True, exist_ok=True)

    made, missing = 0, []
    for node in NODES:
        src = find_source(a.source, node)
        if src is None:
            missing.append(node)
            continue
        dst = a.out / f"{node}.png"
        img = render(src, a.fit)
        assert img.size == (SLOT_W, SLOT_H), img.size
        img.save(dst, "PNG", optimize=True)
        print(f"{src.name:28} -> {dst.name:16} {SLOT_W}x{SLOT_H}  {dst.stat().st_size // 1024} KiB")
        made += 1

    print(f"\n{made} of {len(NODES)} photo(s) written to {a.out}")
    if missing:
        # Not an error: the firmware leaves a missing slot blank rather than failing.
        print(f"no source for: {', '.join(missing)}  (those cards show a blank frame)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
