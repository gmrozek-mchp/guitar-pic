#!/usr/bin/env bash
# Convert an image to the raw boot-splash framebuffer marvin expects.
#
# Output is headerless raw RGBA8888, 1280x800, in the XLCDC layer's native byte
# order: each pixel is the little-endian word 0xRRGGBBAA, i.e. memory bytes
# [A, B, G, R] (see default/src/ui/screens/splash/splash.{c,h}). splash.c reads it
# straight into the scanout buffer and rejects any size other than 4,096,000 bytes.
#
# Provision the result to QSPI with: ../../openocd/program-qspi.sh splash
#
# Uses uv + Pillow (no ImageMagick/ffmpeg needed). Any Pillow-readable input
# (jpg/png/...) works; it is resized to 1280x800 (aspect not preserved).
#
# Usage:
#   ./make-splash.sh [input] [output]
#     input   source image   (default: splash.jpg, next to this script)
#     output  raw blob        (default: splash.raw, next to this script)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
in="${1:-$here/splash.jpg}"
out="${2:-$here/splash.raw}"

[ -f "$in" ] || { echo "make-splash: input not found: $in" >&2; exit 1; }

export UV_CACHE_DIR="${UV_CACHE_DIR:-${TMPDIR:-/tmp}/uv-cache}"

uv run --with pillow python - "$in" "$out" <<'PY'
import sys
from PIL import Image

W, H = 1280, 800
EXPECT = W * H * 4
src, dst = sys.argv[1], sys.argv[2]

im = Image.open(src).convert("RGBA").resize((W, H))
r, g, b, a = im.split()
# XLCDC RGBA_8888 word is 0xRRGGBBAA; little-endian memory order is [A, B, G, R].
data = Image.merge("RGBA", (a, b, g, r)).tobytes()

if len(data) != EXPECT:
    sys.exit("make-splash: produced %d bytes, expected %d" % (len(data), EXPECT))

with open(dst, "wb") as f:
    f.write(data)
print("make-splash: wrote %s (%d bytes, %dx%d RGBA8888)" % (dst, len(data), W, H))
PY
