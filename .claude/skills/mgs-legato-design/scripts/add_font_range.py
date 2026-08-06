#!/usr/bin/env python3
"""Declare codepoints on a font so runtime-drawn characters can't be lost.

The runtime-glyph trap (see SKILL.md): MGS auto-includes the glyphs needed by the
design strings BOUND to a font, and nothing else. Text built at runtime — a C literal,
a CSV field, a formatted number, a star rating assembled byte-by-byte — is invisible to
it. Such a character survives only while some *unrelated* design string happens to
contain it, and disappears silently (no build error, blank glyph) the day that string
is retargeted or pruned.

`ranges.json` is the fix: it holds the font's DECLARED codepoint ranges, which MGS
honours on top of what the strings need. Declaring the codepoint makes the glyph a
property of the font rather than a side effect of someone else's caption.

  add_font_range.py <design.zip> <FontName> <codepoint|start-end> [name] [--apply]

Codepoints accept 0x2605 / U+2605 / 9733 forms. Idempotent (a range already covered is
a no-op), dry-run by default. Cost is one glyph's bitmap in le_gen_fonts.c.
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

FONTS = "assets/fonts/fonts.json"


def parse_cp(s):
    s = s.strip()
    if s.upper().startswith("U+"):
        return int(s[2:], 16)
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def main(argv):
    args = [a for a in argv[1:] if a != "--apply"]
    apply_it = "--apply" in argv
    if len(args) < 3:
        print(__doc__)
        return 2

    zip_path, font_name, spec = args[0], args[1], args[2]
    label = args[3] if len(args) > 3 else "user"

    start, _, end = spec.partition("-")
    lo = parse_cp(start)
    hi = parse_cp(end) if end else lo
    if hi < lo:
        print("!! range end is below its start")
        return 1

    fonts = {f.get("name"): f.get("id")
             for f in mgs_zip.load_json(zip_path, FONTS)["fonts"]}
    if font_name not in fonts:
        print("!! no font named %s. Available:" % font_name)
        for n in sorted(fonts):
            print("   %s" % n)
        return 1

    member = "assets/fonts/%s/ranges.json" % fonts[font_name]
    doc = mgs_zip.load_json(zip_path, member)
    ranges = doc.setdefault("user", [])

    print("=== %s (%s) ===" % (font_name, fonts[font_name]))
    for r in ranges:
        print("   have  %-14s U+%04X..U+%04X" % (r.get("name", "?"), r["start"], r["end"]))

    if any(r["start"] <= lo and hi <= r["end"] for r in ranges):
        print("\nU+%04X..U+%04X already covered — nothing to do." % (lo, hi))
        return 0

    ranges.append({"name": label, "start": lo, "end": hi})
    print("   add   %-14s U+%04X..U+%04X" % (label, lo, hi))

    # The glyph set MGS last generated, for context on what this will add.
    glyphs = mgs_zip.load_json(zip_path, "assets/fonts/%s/glyphs.json" % fonts[font_name])
    present = {int(v.get("codePoint", 0)) for v in glyphs.values()}
    missing = [c for c in range(lo, hi + 1) if c not in present]
    print("\n   %d of %d codepoints in this range are not in the current glyph set%s"
          % (len(missing), hi - lo + 1,
             (": " + " ".join("U+%04X" % c for c in missing[:8])) if missing else ""))
    print("   (they are added on the next Generate — a declared range is not a")
    print("    generated glyph until MGS runs)")

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, {member: json.dumps(doc, indent=1)})
    print("\napplied. Open MGS -> Generate; verify with audit_glyph_coverage.py.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
