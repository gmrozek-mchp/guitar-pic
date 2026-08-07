#!/usr/bin/env python3
"""Report vertical metrics for every font in a Legato le_gen_fonts.c, and where a label
box puts the baseline.

Read-only, and it reads *generated* output rather than the design zip: the metrics MGS
emits are what the firmware actually draws with, and they are the only place the baseline
and x-height are written down. Use it to align anything vertically against text — a bullet
dot, a rule, an accent bar, an icon beside a caption.

    python3 font_metrics.py <le_gen_fonts.c | src-dir> [--box H] [--font NAME]

  --box H   also solve alignment for a label of height H (repeatable)
  --font    restrict to fonts whose name contains this substring

Structures parsed (both in le_gen_fonts.c):

  leRasterFont <name> = { {..header..}, <height>, <baseline>, <bpp>, <name>_data };
  const uint8_t <name>_data[] = <u32 glyph count><20-byte leFontGlyph>*

  leFontGlyph: codePoint u16, width i16, height i16, advance i16,
               bearingX i16, bearingY i16, flags u16, dataRowWidth u16, dataOffset u32
               (little-endian, packed, no padding)

`bearingY` is the rise above the baseline, so it yields the three vertical landmarks the
struct does not name: cap height from digits, x-height from round lowercase, and descender
depth from `h - bearingY` on descending letters.
"""

import argparse
import pathlib
import re
import struct
import sys

GLYPH = struct.Struct("<HhhhhhHHI")
CAP_CHARS = "0123456789"
X_CHARS = "acemnorsuvwxz"
DESC_CHARS = "pqgyj"


def find_source(arg):
    p = pathlib.Path(arg)
    if p.is_file():
        return p
    hits = sorted(p.rglob("le_gen_fonts.c"))
    if not hits:
        sys.exit(f"no le_gen_fonts.c under {p}")
    return hits[0]


def blob(src, name):
    m = re.search(r"const uint8_t %s\[\d+\] =\s*\{(.*?)\n\};" % re.escape(name), src, re.S)
    if not m:
        return None
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))


def glyphs(data):
    if not data or len(data) < 4:
        return {}
    n = struct.unpack_from("<I", data, 0)[0]
    out = {}
    for i in range(n):
        off = 4 + i * GLYPH.size
        if off + GLYPH.size > len(data):
            break
        cp, w, h, adv, bx, by, _f, _drw, _do = GLYPH.unpack_from(data, off)
        out[cp] = dict(w=w, h=h, adv=adv, bx=bx, by=by)
    return out


def rise(g, chars):
    vals = [g[ord(c)]["by"] for c in chars if ord(c) in g]
    return max(vals) if vals else 0


def fonts(src):
    for m in re.finditer(
        r"leRasterFont (\w+) =\s*\{.*?\n\s*(\d+),\n\s*(\d+),", src, re.S
    ):
        name, height, baseline = m.group(1), int(m.group(2)), int(m.group(3))
        g = glyphs(blob(src, name + "_data"))
        advs = {v["adv"] for v in g.values() if v["adv"] > 0}
        yield dict(
            name=name,
            height=height,
            baseline=baseline,
            mono=(len(advs) == 1),
            adv=(min(advs) if advs else 0),
            cap=rise(g, CAP_CHARS),
            xh=rise(g, X_CHARS),
            desc=max(
                (g[ord(c)]["h"] - g[ord(c)]["by"] for c in DESC_CHARS if ord(c) in g),
                default=0,
            ),
            n=len(g),
        )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("--box", type=int, action="append", default=[])
    ap.add_argument("--font", default="")
    a = ap.parse_args()

    path = find_source(a.target)
    src = path.read_text(errors="replace")
    print(f"# {path}\n")

    rows = [f for f in fonts(src) if a.font in f["name"]]
    if not rows:
        sys.exit("no fonts matched")

    hdr = f"{'font':30}{'glyphs':>7}{'h':>4}{'base':>6}{'adv':>5}{'cap':>5}{'x-ht':>6}{'desc':>6}  mono"
    print(hdr)
    print("-" * len(hdr))
    for f in sorted(rows, key=lambda r: r["name"]):
        print(
            f"{f['name']:30}{f['n']:>7}{f['height']:>4}{f['baseline']:>6}"
            f"{f['adv']:>5}{f['cap']:>5}{f['xh']:>6}{f['desc']:>6}"
            f"  {'yes' if f['mono'] else 'NO'}"
        )

    for H in a.box:
        print(f"\n## label box height {H}px")
        print("   Legato centres text at y + H/2 - fontHeight/2 (integer division), so")
        print("   baseline = y + (H//2 - h//2) + base. Offsets below are from the box's y.\n")
        w = f"{'font':30}{'clipped':>8}{'top':>6}{'baseline':>10}{'cap mid':>9}{'x-ht mid':>10}"
        print(w)
        print("-" * len(w))
        for f in sorted(rows, key=lambda r: r["name"]):
            top = H // 2 - f["height"] // 2
            bl = top + f["baseline"]
            clipped = "YES" if H < f["height"] else "-"
            print(
                f"{f['name']:30}{clipped:>8}{top:>+6}{bl:>+10}"
                f"{bl - f['cap'] / 2:>+9.1f}{bl - f['xh'] / 2:>+10.1f}"
            )
        print(
            "\n   Centre a marker of diameter D on the x-height middle: y = <x-ht mid> - D/2.\n"
            "   Cap mid is for a digits/caps-only line; the row box centre is for neither."
        )


if __name__ == "__main__":
    main()
