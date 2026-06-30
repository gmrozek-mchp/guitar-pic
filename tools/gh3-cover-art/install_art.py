#!/usr/bin/env python3
"""Install processed GH3 cover art to marvin SD-card data tree.

Copies cover images from data/508w/ and data/144x144/ to the marvin
game data directory with the naming scheme expected by the firmware:
  art/small/<setlist>-<NN>.jpg   (144×144 for now-playing)
  art/large/<setlist>-<NN>.jpg   (508px wide for song-select)

Usage:
    cd tools/gh3-cover-art
    uv run install_art.py
    uv run install_art.py --dest /path/to/custom/marvin/data/games/gh3-wii
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path


def install_covers(src_base, dest_base):
    """Copy processed covers to marvin data tree with firmware naming scheme."""
    src_base = Path(src_base)
    dest_base = Path(dest_base)

    src_small = src_base / "144x144"
    src_large = src_base / "508w"

    dest_small = dest_base / "art" / "small"
    dest_large = dest_base / "art" / "large"

    # Create destination directories
    dest_small.mkdir(parents=True, exist_ok=True)
    dest_large.mkdir(parents=True, exist_ok=True)

    # Regex to extract setlist and index from filenames like "main_00_slow_ride.jpg"
    pattern = r"^(main|bonus)_(\d+)_"

    small_files = sorted(src_small.glob("*.jpg"))
    large_files = sorted(src_large.glob("*.jpg"))

    if not small_files or not large_files:
        print(f"Error: No processed covers found in {src_base}", file=sys.stderr)
        return 1

    print(f"Installing {len(small_files)} small covers (144×144)...")
    for src_file in small_files:
        match = re.match(pattern, src_file.name)
        if not match:
            print(f"  WARNING: Skipped {src_file.name} (naming mismatch)")
            continue

        setlist, index = match.groups()
        dest_name = f"{setlist}-{int(index):02d}.jpg"
        dest_path = dest_small / dest_name

        shutil.copy2(src_file, dest_path)
        print(f"  {dest_name}")

    print(f"\nInstalling {len(large_files)} large covers (508px wide)...")
    for src_file in large_files:
        match = re.match(pattern, src_file.name)
        if not match:
            print(f"  WARNING: Skipped {src_file.name} (naming mismatch)")
            continue

        setlist, index = match.groups()
        dest_name = f"{setlist}-{int(index):02d}.jpg"
        dest_path = dest_large / dest_name

        shutil.copy2(src_file, dest_path)
        print(f"  {dest_name}")

    print(f"\n✓ Installed to:")
    print(f"  {dest_small}")
    print(f"  {dest_large}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default="./data",
                    help="Source directory with 144x144/ and 508w/ subdirs (default: ./data)")
    ap.add_argument("--dest", default="../../firmware/marvin/data/games/gh3-wii",
                    help="Destination marvin data root (default: ../../firmware/marvin/data/games/gh3-wii)")
    args = ap.parse_args()

    sys.exit(install_covers(args.source, args.dest))


if __name__ == "__main__":
    main()
