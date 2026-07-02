#!/usr/bin/env python3
"""Process GH3 cover art directly into marvin's game-data tree.

Reads original JPG covers from ./data/ (named <setlist>_<idx>_<slug>.jpg) and
writes the two firmware asset tiers straight into the marvin art folder, named
by the recognizer key the firmware expects (<setlist>-<NN>):

- large: 508x208 middle strip for the song-select detail screen, with a baked
  overlay matching the on-screen album-art card: the difficulty tier's dark tint
  (Tailwind <hue>-900 at 40%) washes the zinc-900 card behind the cover, the
  cover is dimmed to 60%, and a black gradient darkens the bottom edge so the
  title/artist text sits on a legible backdrop. Saved as PNG so the overlay is
  lossless. The tint is looked up per song from songs.csv: main tiers "1".."8"
  map to an 8-tier Tailwind palette (see TIER_PALETTE), bonus (or unknown) maps
  to a neutral gray.
  -> <out>/large/<setlist>-<NN>.png
- small: 144x144 dashboard now-playing thumbnail (pillarboxed, clean, no fade).
  Saved as PNG: on-device the covers are decoded offscreen into a static DDR
  cache, and Legato's JPEG decoder gates its block writes on the (stale, at boot)
  renderer clip rect — so a runtime JPEG decode lands as noise. The PNG decoder
  does a plain color-converting buffer copy with no clip dependency, so both tiers
  use PNG on the card.
  -> <out>/small/<setlist>-<NN>.png

This is a one-step process+install: <out> defaults to the marvin art tree, so
there is no separate install step. Existing cover files in each tier are cleared
first, so a re-run leaves no stale files (e.g. old .jpg larges).

Usage:
    cd tools/gh3-cover-art
    uv run process_gh3_cover_art.py
    uv run process_gh3_cover_art.py --source ./data --out /path/to/art
"""

import argparse
import csv
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: PIL/Pillow not found. Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)

LARGE_W = 508
LARGE_H = 208
SMALL_SIZE = 144

DEFAULT_OUT = "../../firmware/marvin/data/games/gh3-wii/art"
DEFAULT_CATALOG = "../../firmware/marvin/data/games/gh3-wii/songs.csv"

# Filename -> (setlist, index): "main_04_rock_and_roll.jpg" -> ("main", 4).
KEY_RE = re.compile(r"^(main|bonus)_(\d+)_")

# 8-tier difficulty palette, Tailwind ("tailscan") colors, tier 1 easiest -> 8
# hardest (cool/calm at the low end, hot at the high end). Each tier is
# (accent, tint): `accent` is the bright label color (Tailwind <hue>-400/-500) —
# set the matching MGS SCHEME_TEXT_TIER_n text color to this; `tint` is the dark
# wash (Tailwind <hue>-900) baked behind the large cover art. Edit a row to retune.
TIER_PALETTE = {
    #     accent (label)        tint (art wash)      Tailwind hue
    1: ((0x4A, 0xDE, 0x80), (0x14, 0x53, 0x2D)),   # green
    2: ((0x2D, 0xD4, 0xBF), (0x13, 0x4E, 0x4A)),   # teal
    3: ((0x60, 0xA5, 0xFA), (0x1E, 0x3A, 0x8A)),   # blue
    4: ((0xA7, 0x8B, 0xFA), (0x4C, 0x1D, 0x95)),   # violet
    5: ((0xFA, 0xCC, 0x15), (0x71, 0x3F, 0x12)),   # yellow
    6: ((0xFB, 0x92, 0x3C), (0x7C, 0x2D, 0x12)),   # orange
    7: ((0xF8, 0x71, 0x71), (0x7F, 0x1D, 0x1D)),   # red
    8: ((0xEF, 0x44, 0x44), (0x45, 0x0A, 0x0A)),   # deep red
}
BONUS_ACCENT = (0xD4, 0xD4, 0xD8)   # zinc-300
BONUS_TINT   = (0x3F, 0x3F, 0x46)   # zinc-700 (neutral, near-invisible wash)

# Overlay tunables (match the on-screen album-art card; safe to retune).
ZINC_900      = (0x18, 0x18, 0x1B)   # card background the tier tint sits over
TINT_ALPHA    = 0.40   # tier tint opacity over the card (Tailwind /40)
IMAGE_OPACITY = 0.60   # cover opacity over the tinted card (Tailwind opacity-60)
GRAD_BOTTOM   = 0.80   # black gradient alpha at the bottom edge (from-black/80)
GRAD_GAMMA    = 1.00   # gradient shape (1.0 = linear top->bottom; >1 keeps top clean)


def load_difficulty(catalog_path):
    """Read songs.csv into {(setlist, index): difficulty_string}."""
    table = {}
    p = Path(catalog_path)
    if not p.is_file():
        print(f"WARNING: catalog {catalog_path} not found; all fades will be gray.",
              file=sys.stderr)
        return table
    with p.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            try:
                key = (row["setlist"].strip(), int(row["index"]))
            except (KeyError, ValueError):
                continue
            table[key] = (row.get("difficulty") or "").strip()
    return table


def tier_tint(diff_str):
    """Map a difficulty string to its baked art tint: "1".."8" -> TIER_PALETTE
    tint, anything else (e.g. "bonus", empty) -> neutral gray."""
    try:
        tier = int(diff_str)
    except (TypeError, ValueError):
        tier = None
    entry = TIER_PALETTE.get(tier)
    return entry[1] if entry else BONUS_TINT


def _blend_rgb(a, b, t):
    """Linear blend of two RGB tuples: (1-t)*a + t*b."""
    return tuple(int(round(a[i] * (1.0 - t) + b[i] * t)) for i in range(3))


def _bottom_gradient_alpha(w, h):
    """L-mode alpha mask for the bottom black gradient: 0 at the top, GRAD_BOTTOM
    at the bottom edge (shaped by GRAD_GAMMA). Matches the CSS overlay
    `bg-gradient-to-t from-black/80 to-transparent`."""
    col = Image.new("L", (1, h))
    cp = col.load()
    for y in range(h):
        t = y / (h - 1) if h > 1 else 0.0
        a = GRAD_BOTTOM * (t ** GRAD_GAMMA)
        cp[0, y] = max(0, min(255, int(round(a * 255))))
    return col.resize((w, h))


def make_large(img, tint):
    """Scale to 508 wide, center-crop the middle 508x208 band, then bake the
    album-art overlay: the tier `tint` washes the zinc-900 card behind the cover,
    the cover is dimmed to IMAGE_OPACITY, and a black gradient darkens the bottom
    edge. Returns an RGB image."""
    ratio = LARGE_W / img.width
    scaled = img.resize((LARGE_W, max(LARGE_H, int(round(img.height * ratio)))),
                        Image.Resampling.LANCZOS)
    top = (scaled.height - LARGE_H) // 2
    strip = scaled.crop((0, top, LARGE_W, top + LARGE_H)).convert("RGB")

    card = _blend_rgb(ZINC_900, tint, TINT_ALPHA)            # tier tint over the card
    backdrop = Image.new("RGB", (LARGE_W, LARGE_H), card)
    dimmed = Image.blend(backdrop, strip, IMAGE_OPACITY)     # cover at opacity-60
    black = Image.new("RGB", (LARGE_W, LARGE_H), (0, 0, 0))
    alpha = _bottom_gradient_alpha(LARGE_W, LARGE_H)
    return Image.composite(black, dimmed, alpha)             # bottom black gradient


def make_small(img):
    """Scale to fit a 144x144 box, pillarbox/letterbox on black. Returns RGB."""
    scale = min(SMALL_SIZE / img.width, SMALL_SIZE / img.height)
    new_w = int(img.width * scale)
    new_h = int(img.height * scale)
    scaled = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (SMALL_SIZE, SMALL_SIZE), (0, 0, 0))
    canvas.paste(scaled, ((SMALL_SIZE - new_w) // 2, (SMALL_SIZE - new_h) // 2))
    return canvas


def clear_covers(d):
    """Remove existing cover files (any image extension) from a tier dir, so a
    re-run leaves no stale files behind (e.g. old .jpg larges)."""
    for f in d.iterdir():
        if f.is_file() and f.suffix.lower() in (".jpg", ".jpeg", ".png"):
            f.unlink()


def process_cover_art(src_dir, out_base, catalog_path):
    """Process all JPGs in src_dir into the marvin art tree."""
    src_path = Path(src_dir)

    out_large = Path(out_base) / "large"
    out_small = Path(out_base) / "small"
    out_large.mkdir(parents=True, exist_ok=True)
    out_small.mkdir(parents=True, exist_ok=True)
    clear_covers(out_large)
    clear_covers(out_small)

    difficulty = load_difficulty(catalog_path)

    jpg_files = sorted(f for f in src_path.iterdir()
                       if f.is_file() and f.suffix.lower() in (".jpg", ".jpeg"))
    if not jpg_files:
        print(f"No JPG files found in {src_dir}", file=sys.stderr)
        return 1

    print(f"Processing {len(jpg_files)} cover images -> {out_base}\n")

    processed = 0
    failed = []

    for jpg_path in jpg_files:
        try:
            m = KEY_RE.match(jpg_path.name)
            if not m:
                print(f"  WARNING: {jpg_path.name} has no <setlist>_<idx>_ prefix; skipped")
                continue
            setlist, index = m.group(1), int(m.group(2))
            key = (setlist, index)
            stem = f"{setlist}-{index:02d}"            # firmware naming
            tint = tier_tint(difficulty.get(key))

            img = Image.open(jpg_path)
            if img.mode in ("RGBA", "LA", "P"):
                rgb = Image.new("RGB", img.size, (255, 255, 255))
                rgb.paste(img, mask=img.split()[-1] if img.mode == "RGBA" else None)
                img = rgb
            elif img.mode != "RGB":
                img = img.convert("RGB")

            # large: PNG strip with baked album-art overlay (tier tint + gradient)
            make_large(img, tint).save(out_large / (stem + ".png"), "PNG")
            # small: clean 144x144 PNG (PNG decoder is clip-independent offscreen;
            # the JPEG decoder is not — see the module docstring)
            make_small(img).save(out_small / (stem + ".png"), "PNG")

            print(f"  {jpg_path.name}: tier {difficulty.get(key) or '-'} "
                  f"tint {tint} -> large/{stem}.png, small/{stem}.png")
            processed += 1

        except Exception as e:
            print(f"  ERROR {jpg_path.name}: {e}")
            failed.append((jpg_path.name, str(e)))

    print(f"\n✓ Processed {processed}/{len(jpg_files)} images")
    if failed:
        print(f"✗ Failed: {len(failed)}")
        for name, error in failed:
            print(f"  - {name}: {error}")
        return 1

    print(f"\nInstalled to:")
    print(f"  {out_large}  (song-select, PNG, album-art overlay)")
    print(f"  {out_small}  (dashboard, PNG)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default="./data",
                    help="Source directory with original JPG files (default: ./data)")
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="marvin art root; writes large/ and small/ subdirs "
                         "(default: %(default)s)")
    ap.add_argument("--catalog", default=DEFAULT_CATALOG,
                    help="songs.csv path for per-song difficulty (fade color)")
    args = ap.parse_args()

    sys.exit(process_cover_art(args.source, args.out, args.catalog))


if __name__ == "__main__":
    main()
