#!/usr/bin/env python3
"""Process board photos into marvin's system-info asset tree.

Reads one photo per node from ./data/ (named <node>.<ext>, any size/aspect) and
writes the exact slot the firmware expects:

    <out>/<node>.png     288x620 RGB, centre-cropped to the column's aspect

The firmware validates the PNG header against NODE_ART_W x NODE_ART_H and skips any
file that doesn't match exactly (game/node_art.h), so resizing here is not cosmetic
— an off-size photo silently shows a blank frame on the detail screen.

PNG, not JPEG: Legato's JPEG decoder gates its block writes on the renderer clip
rect, which is stale during marvin's offscreen boot decode, so a runtime JPEG lands
as noise. The album-art tiers are PNG for the same reason.

The photo column is tall and narrow (288x620, ~1:2.15), which almost no camera photo
is. Centre-cropping a landscape shot to that would keep a thin vertical sliver, so
the default is `--fit contain`: scale the whole photo to fit the width and letterbox
the rest in the card's own zinc-900, which keeps the board recognizable. Use
`--fit cover` for a photo already framed portrait.

Node names must match game/node_art.c's PHOTO table:
    marvin fauxmote guitar fretboard beatbox lemmy lightshow

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

from PIL import Image, ImageOps

# Must match NODE_ART_W / NODE_ART_H in firmware/marvin/default/src/game/node_art.h
SLOT_W = 288
SLOT_H = 620

# Must match the PHOTO table in firmware/marvin/default/src/game/node_art.c
NODES = ["marvin", "fauxmote", "guitar", "fretboard", "beatbox", "lemmy", "lightshow"]

# The card fill the letterbox blends into (Tailwind zinc-900, SCHEME_FILL_ZINC_900).
ZINC_900 = (24, 24, 27)

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


def render(src: Path, fit: str) -> Image.Image:
    img = ImageOps.exif_transpose(Image.open(src)).convert("RGB")

    if fit == "cover":
        return ImageOps.fit(img, (SLOT_W, SLOT_H), method=Image.LANCZOS, centering=(0.5, 0.5))

    # contain: scale to fit inside the slot, then centre on the card's own fill
    scaled = img.copy()
    scaled.thumbnail((SLOT_W, SLOT_H), Image.LANCZOS)
    out = Image.new("RGB", (SLOT_W, SLOT_H), ZINC_900)
    out.paste(scaled, ((SLOT_W - scaled.width) // 2, (SLOT_H - scaled.height) // 2))
    return out


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
