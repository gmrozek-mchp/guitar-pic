#!/usr/bin/env python3
"""Verify marvin's QR rasterizer by decoding what it actually produces.

Compiles the firmware's own ui/gfx/qr_raster.c (plus vendored qrcodegen) for the host,
renders every URL in the System Info NODE table, and decodes the result. A rasterizer
bug — wrong stride, transposed modules, inverted colours, a quiet zone that is not
actually quiet — shows up here rather than on the panel at a demo.

The URL list is scraped from screen_system.c rather than duplicated here: a tool
carrying its own copy of a firmware table is how a slot goes silently unverified.

    uv run verify.py            # verify
    uv run verify.py --png out  # also write decoded tiles as PNGs to look at
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import segno
import zxingcpp
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
SRC = REPO / "firmware/marvin/default/src"
SCREEN = SRC / "ui/screens/system/screen_system.c"
QRCODEGEN = SRC / "third_party/qrcodegen"

# Must track ui/gfx/qr_raster.h. Asserted against the rendered tile below, so a change
# there that is not mirrored here fails loudly instead of silently weakening the test.
MODULES = 33
QUIET = 4
SCALE = 4
TILE = (MODULES + 2 * QUIET) * SCALE


def node_urls() -> dict[str, str]:
    """{node name: qr_url} for every NODE entry that has one, in table order."""
    text = SCREEN.read_text()
    entries = re.findall(r'\.name\s*=\s*"([^"]+)"', text)
    urls = re.findall(r'\.qr_url\s*=\s*(NULL|"[^"]*")', text)
    if not urls:
        sys.exit(f"no .qr_url entries found in {SCREEN}")
    if len(urls) != len(entries):
        sys.exit(
            f"{SCREEN}: {len(entries)} nodes but {len(urls)} .qr_url initializers — "
            "every entry must state one, NULL included, or the mapping is a guess"
        )
    return {n: u.strip('"') for n, u in zip(entries, urls) if u != "NULL"}


def build(workdir: Path) -> Path:
    exe = workdir / "qr_dump"
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if cc is None:
        sys.exit("no host C compiler found")
    cmd = [
        cc, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
        f"-I{SRC}", f"-I{QRCODEGEN}",
        str(Path(__file__).parent / "qr_dump.c"),
        str(SRC / "ui/gfx/qr_raster.c"),
        str(QRCODEGEN / "qrcodegen.c"),
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def check_tile(img: Image.Image, url: str) -> list[str]:
    """Geometry and colour checks the decoder alone would not catch."""
    problems = []

    if img.size != (TILE, TILE):
        problems.append(f"tile is {img.size}, expected {(TILE, TILE)}")
        return problems

    colours = {c for _, c in img.convert("RGB").getcolors(maxcolors=1 << 16)}
    if not colours <= {(0, 0, 0), (255, 255, 255)}:
        extra = sorted(colours - {(0, 0, 0), (255, 255, 255)})[:4]
        problems.append(f"tile has non black/white pixels, e.g. {extra}")

    px = img.convert("L").load()
    edge = QUIET * SCALE
    for x in range(TILE):
        for y in list(range(edge)) + list(range(TILE - edge, TILE)):
            if px[x, y] != 255 or px[y, x] != 255:
                problems.append("quiet zone is not blank")
                break
        else:
            continue
        break

    # Module count and payload cross-checked against an independent encoder. The
    # module *pattern* legitimately differs (mask choice and boosted ECC level), so
    # only the size is compared.
    ref = segno.make(url, error="m", version=None, boost_error=True)
    ref_modules = ref.symbol_size(border=0)[0]
    if ref_modules > MODULES:
        problems.append(
            f"needs {ref_modules} modules at ECC M but the tile is pinned to {MODULES} "
            f"— URL is {len(url)} bytes, too long for version {(MODULES - 17) // 4}"
        )

    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", type=Path, help="also write each tile as a PNG here")
    args = ap.parse_args()

    urls = node_urls()
    print(f"{len(urls)} QR URLs in the NODE table ({len(set(urls.values()))} unique)\n")

    failures = 0
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        exe = build(work)

        for name, url in urls.items():
            ppm = work / f"{name}.ppm"
            run = subprocess.run([str(exe), url, str(ppm)], capture_output=True, text=True)
            if run.returncode != 0:
                print(f"FAIL {name:<12} render: {run.stderr.strip()}")
                failures += 1
                continue

            img = Image.open(ppm)
            problems = check_tile(img, url)

            results = zxingcpp.read_barcodes(img)
            if not results:
                problems.append("no QR decoded from the tile")
            elif results[0].text != url:
                problems.append(f"decoded {results[0].text!r}, expected {url!r}")
            elif results[0].format != zxingcpp.BarcodeFormat.QRCode:
                problems.append(f"decoded as {results[0].format}, not a QR code")

            if args.png:
                args.png.mkdir(parents=True, exist_ok=True)
                img.save(args.png / f"{name}.png")

            if problems:
                failures += 1
                print(f"FAIL {name:<12} {url}")
                for p in problems:
                    print(f"       - {p}")
            else:
                print(f"ok   {name:<12} {len(url):>2}B  {img.size[0]}px  {url}")

    print()
    if failures:
        print(f"{failures} of {len(urls)} failed")
        return 1
    print(f"all {len(urls)} QR tiles decode back to their URL")
    return 0


if __name__ == "__main__":
    sys.exit(main())
