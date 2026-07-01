#!/usr/bin/env python3
"""Process GH3 cover art directly into marvin's game-data tree.

Reads original JPG covers from ./data/ (named <setlist>_<idx>_<slug>.jpg) and
writes the two firmware asset tiers straight into the marvin art folder, named
by the recognizer key the firmware expects (<setlist>-<NN>):

- large: 508x208 middle strip for the song-select detail screen, with a baked
  vertical gradient overlay: the difficulty color washes the whole strip and
  darkens toward the bottom (staying the same hue — a dark color, not pure
  black — as a legible backdrop for the title/artist text). Saved as PNG so the
  gradient is lossless. The color is looked up per song from songs.csv: main
  tiers "1".."8" map to an 8-tier palette (green->red with blue/purple accents,
  see TIER_COLORS), bonus (or unknown) maps to gray.
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

# Per-tier difficulty palette (tier 1 easiest -> 8 hardest): cool/calm at the low
# end, hot at the high end, anchored green(1) -> red(8) with blue/purple accents.
# The SAME colors drive the art fade here AND the TIER-label text schemes in
# screen_song_select.c, so keep them in sync. Edit a row to retune one tier.
TIER_COLORS = {
    1: (0x4A, 0xDE, 0x53),   # green
    2: (0x14, 0xB8, 0xA6),   # teal
    3: (0x3B, 0x82, 0xF6),   # blue
    4: (0xA8, 0x55, 0xF7),   # purple
    5: (0xFA, 0xCC, 0x15),   # yellow
    6: (0xFB, 0x92, 0x3C),   # orange
    7: (0xFF, 0x56, 0x30),   # red-orange
    8: (0xEF, 0x44, 0x44),   # red
}
BONUS_GRAY = (96, 96, 96)

# Fade tunables (visual taste; safe to adjust). A single full-height ramp by height
# fraction t: the difficulty `color` washes the top and darkens to a near-black tint
# of the same hue at the bottom edge. FADE_GAMMA shapes it (>1 keeps the top clean
# and concentrates the darkening into the lower band). FADE_START/FADE_FULL bound the
# ramp; the defaults (0..1) run it across the whole height.
FADE_TOP_ALPHA   = 0.35   # wash opacity at the top (0 = no tint, art fully visible)
FADE_START       = 0.00   # height fraction where darkening begins (raise to keep more clean top)
FADE_FULL        = 1.00   # height fraction reaching darkest (1.0 = at the bottom edge)
FADE_BOTTOM_DARK = 0.05   # darkest color = difficulty color * this (same hue, ~black)
FADE_GAMMA       = 2.00   # ramp shape: >1 keeps the top lighter, darkens lower down


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


def difficulty_color(diff_str):
    """Map a difficulty string to its tier color: "1".."8" -> TIER_COLORS,
    anything else (e.g. "bonus", empty) -> gray."""
    try:
        tier = int(diff_str)
    except (TypeError, ValueError):
        tier = None
    return TIER_COLORS.get(tier, BONUS_GRAY)


def build_fade(w, h, color):
    """Vertical gradient overlay baked over the cover: the difficulty `color`
    washes the top of the strip, darkens through the middle, and goes to a solid
    dark tint of the same hue over the lower band so the title/artist text sits on
    a legible, on-theme backdrop.

    Returns (overlay_rgb, alpha) as full-size images for
    Image.composite(overlay_rgb, image, alpha). Per row, by height fraction t:
      - t <= FADE_START          : top wash (alpha FADE_TOP_ALPHA, full `color`)
      - FADE_START < t <= FADE_FULL: eased ramp (FADE_GAMMA) toward darkest
                                      (alpha -> 1.0, color -> color*FADE_BOTTOM_DARK)
    With FADE_FULL = 1.0 the ramp runs to the bottom edge, so there is no flat dark
    band — the bottom half is a continuous gradient."""
    bottom = tuple(int(c * FADE_BOTTOM_DARK) for c in color)
    color_col = Image.new("RGB", (1, h))
    alpha_col = Image.new("L", (1, h))
    cp = color_col.load()
    ap = alpha_col.load()
    span = max(1e-6, FADE_FULL - FADE_START)
    for y in range(h):
        t = y / (h - 1) if h > 1 else 0.0
        s = (t - FADE_START) / span          # 0 at FADE_START, 1 at FADE_FULL
        s = 0.0 if s < 0.0 else (1.0 if s > 1.0 else s)
        s = s ** FADE_GAMMA
        cp[0, y] = (int(color[0] * (1.0 - s) + bottom[0] * s),
                    int(color[1] * (1.0 - s) + bottom[1] * s),
                    int(color[2] * (1.0 - s) + bottom[2] * s))
        a = FADE_TOP_ALPHA + (1.0 - FADE_TOP_ALPHA) * s
        ap[0, y] = max(0, min(255, int(a * 255)))
    return color_col.resize((w, h)), alpha_col.resize((w, h))


def make_large(img, color):
    """Scale to 508 wide, center-crop the middle 508x208 band, bake the
    difficulty-color wash that darkens toward the bottom. Returns an RGB image."""
    ratio = LARGE_W / img.width
    scaled = img.resize((LARGE_W, max(LARGE_H, int(round(img.height * ratio)))),
                        Image.Resampling.LANCZOS)
    top = (scaled.height - LARGE_H) // 2
    strip = scaled.crop((0, top, LARGE_W, top + LARGE_H))

    overlay, alpha = build_fade(LARGE_W, LARGE_H, color)
    return Image.composite(overlay, strip, alpha)


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
            color = difficulty_color(difficulty.get(key))

            img = Image.open(jpg_path)
            if img.mode in ("RGBA", "LA", "P"):
                rgb = Image.new("RGB", img.size, (255, 255, 255))
                rgb.paste(img, mask=img.split()[-1] if img.mode == "RGBA" else None)
                img = rgb
            elif img.mode != "RGB":
                img = img.convert("RGB")

            # large: PNG strip with baked difficulty fade
            make_large(img, color).save(out_large / (stem + ".png"), "PNG")
            # small: clean 144x144 PNG (PNG decoder is clip-independent offscreen;
            # the JPEG decoder is not — see the module docstring)
            make_small(img).save(out_small / (stem + ".png"), "PNG")

            print(f"  {jpg_path.name}: tier {difficulty.get(key) or '-'} "
                  f"color {color} -> large/{stem}.png, small/{stem}.png")
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
    print(f"  {out_large}  (song-select, PNG, difficulty fade)")
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
