#!/usr/bin/env python3
"""Process GH3 cover art into two offline sizes for marvin.

Reads JPG cover images from ./data/ and creates two asset sizes:
- 508px wide for song-select screen (maintains aspect ratio)
- 144x144 for now-playing section (maintains aspect ratio with pillarboxing)

Output organized into subdirectories by size.

Usage:
    cd tools/gh3-cover-art
    uv run process_gh3_cover_art.py
    uv run process_gh3_cover_art.py --source ./data --out ./data
"""

import argparse
import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: PIL/Pillow not found. Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)


def scale_to_width(img, target_width):
    """Scale image to target width, maintaining aspect ratio."""
    ratio = target_width / img.width
    new_height = int(img.height * ratio)
    return img.resize((target_width, new_height), Image.Resampling.LANCZOS)


def scale_to_square_letterboxed(img, target_size, bg_color=(0, 0, 0)):
    """Scale image to fit in target_size x target_size box, maintaining aspect ratio.

    Pillarbox/letterbox with black background if needed.
    """
    # Calculate scale to fit within target box
    scale = min(target_size / img.width, target_size / img.height)
    new_width = int(img.width * scale)
    new_height = int(img.height * scale)

    # Resize image
    scaled = img.resize((new_width, new_height), Image.Resampling.LANCZOS)

    # Create square canvas and paste centered
    canvas = Image.new("RGB", (target_size, target_size), bg_color)
    offset_x = (target_size - new_width) // 2
    offset_y = (target_size - new_height) // 2
    canvas.paste(scaled, (offset_x, offset_y))

    return canvas


def process_cover_art(src_dir, out_base):
    """Process all JPGs in src_dir into sized versions."""
    src_path = Path(src_dir)

    # Create output directories
    out_508w = Path(out_base) / "508w"
    out_144 = Path(out_base) / "144x144"
    out_508w.mkdir(parents=True, exist_ok=True)
    out_144.mkdir(parents=True, exist_ok=True)

    # Find all JPG files (skip subdirs in case they exist)
    jpg_files = sorted([f for f in src_path.iterdir()
                       if f.is_file() and f.suffix.lower() in ('.jpg', '.jpeg')])

    if not jpg_files:
        print(f"No JPG files found in {src_dir}", file=sys.stderr)
        return

    print(f"Processing {len(jpg_files)} cover images...\n")

    processed = 0
    failed = []

    for jpg_path in jpg_files:
        try:
            print(f"Processing {jpg_path.name}...", end=" ")

            img = Image.open(jpg_path)

            # Convert to RGB if needed (strip alpha, handle RGBA)
            if img.mode in ('RGBA', 'LA', 'P'):
                rgb_img = Image.new('RGB', img.size, (255, 255, 255))
                rgb_img.paste(img, mask=img.split()[-1] if img.mode == 'RGBA' else None)
                img = rgb_img
            elif img.mode != 'RGB':
                img = img.convert('RGB')

            # Generate 508px wide version
            img_508w = scale_to_width(img, 508)
            out_508w_path = out_508w / jpg_path.name
            img_508w.save(out_508w_path, 'JPEG', quality=92)
            print(f"\n  → 508w: {out_508w_path.name} ({img_508w.width}×{img_508w.height})", end=" ")

            # Generate 144x144 version
            img_144 = scale_to_square_letterboxed(img, 144)
            out_144_path = out_144 / jpg_path.name
            img_144.save(out_144_path, 'JPEG', quality=92)
            print(f"\n  → 144×144: {out_144_path.name}")

            processed += 1

        except Exception as e:
            print(f"\n  ERROR: {e}")
            failed.append((jpg_path.name, str(e)))

    print(f"\n✓ Processed {processed}/{len(jpg_files)} images")
    if failed:
        print(f"✗ Failed: {len(failed)}")
        for name, error in failed:
            print(f"  - {name}: {error}")
        return 1

    print(f"\nOutput directories:")
    print(f"  508w (song-select):  {out_508w}")
    print(f"  144×144 (now-playing): {out_144}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default="./data",
                    help="Source directory with original JPG files (default: ./data)")
    ap.add_argument("--out", default="./data",
                    help="Output base directory (will create 508w/ and 144x144/ subdirs)")
    args = ap.parse_args()

    sys.exit(process_cover_art(args.source, args.out))


if __name__ == "__main__":
    main()
