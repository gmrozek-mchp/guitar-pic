#!/usr/bin/env python3
"""Check that every character a Legato UI can display exists in the font that draws it.

MGS auto-includes the glyphs needed by **design-defined strings** when it generates, so
those are always safe. It cannot see text produced at **runtime** — `setString` with a
dynamic string built from a C literal, a CSV/QSPI data file, a sprintf of a device name.
Any non-ASCII character on that path must be added to the font's character set **by hand
in MGS** (Font asset -> add range / add characters) or it silently renders as missing.

This script reports both sides:
  * design strings   -> per font, characters bound but not present (should be empty after
                        a Generate; if not, the font needs a manual range)
  * runtime text     -> non-ASCII characters found in hand-source string literals and in
                        data files, checked against the fonts each source file uses

Usage:
    audit_glyph_coverage.py <design.zip> [hand-src-dir] [data-file-or-dir ...]

Hand-source scanning strips C comments first (a `…` in a comment is not rendered text)
and excludes the generated tree (located from the zip's own directory). Data files are
decoded as UTF-8.
For data files the font set can't be inferred, so they are checked against **every**
surviving font and reported per font.
"""
import collections
import json
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

ASCII = set(range(32, 127))


def load_coverage(zip_path):
    """font name -> set of codepoints actually present in its generated glyph set."""
    z = zipfile.ZipFile(zip_path)
    fonts = json.loads(z.read("assets/fonts/fonts.json"))["fonts"]
    cov, bound = {}, {}
    for f in fonts:
        g = json.loads(z.read("assets/fonts/%s/glyphs.json" % f["id"]))
        cov[f["name"]] = {v["codePoint"] for v in g.values()}
    st = json.loads(z.read("stringtable.json"))
    vals = {s["properties"]["id"]["value"]:
            next((v.get("value", "") for v in s["values"]), "") for s in st["strings"]}
    names = {f["id"]: f["name"] for f in fonts}
    for b in st["bindings"]:
        bound.setdefault(names.get(b["font"], b["font"]), set()).update(vals.get(b["string"], ""))
    return cov, bound


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def literals(text):
    return re.findall(r'"((?:[^"\\\n]|\\.)*)"', text)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    zip_path = argv[1]
    rest = argv[2:]
    # First positional after the zip is the source root if it is a directory holding
    # C/C++ source; everything else is runtime data. (Don't test for "src" in the
    # name — a project's source root can be called anything.)
    hand_dir = None
    if rest and os.path.isdir(rest[0]):
        if any(f.endswith((".c", ".h", ".cpp")) for _r, _d, fs in os.walk(rest[0]) for f in fs):
            hand_dir = rest[0]
    data_paths = [p for p in rest if p != hand_dir]

    cov, bound = load_coverage(zip_path)
    problems = 0

    print("== FONT COVERAGE (%d fonts)" % len(cov))
    for name in sorted(cov):
        cps = cov[name]
        extra = sorted(c for c in cps if c > 255)
        latin1 = bool(cps & set(range(160, 256)))
        print("  %-28s %3d glyphs  %s  extra=%s"
              % (name, len(cps), "ASCII+Latin-1" if latin1 else "ASCII only   ",
                 " ".join("U+%04X(%s)" % (c, chr(c)) for c in extra) or "-"))

    print("\n== DESIGN STRINGS vs their font (MGS should keep this empty)")
    gap = False
    for name, chars in sorted(bound.items()):
        miss = sorted({ord(c) for c in chars} - cov.get(name, set()))
        if miss:
            gap = True
            problems += 1
            print("  ! %-26s missing %s" % (name, " ".join("U+%04X(%s)" % (c, chr(c)) for c in miss)))
    if not gap:
        print("  OK — every design string is fully covered by its bound font")

    if hand_dir:
        print("\n== RUNTIME TEXT in hand source (must be added to fonts MANUALLY)")
        found = False
        for p in sorted(mgs_zip.hand_source_files(hand_dir, zip_path,
                                                  exts=(".c", ".h", ".cpp"))):
            t = strip_c_comments(open(p, encoding="utf-8", errors="replace").read())
            fonts_here = sorted(n for n in cov if re.search(r"\b%s\b" % re.escape(n), t))
            if not fonts_here:
                continue
            chars = collections.Counter(
                ch for lit in literals(t) for ch in lit if ord(ch) > 126)
            if not chars:
                continue
            found = True
            print("  %s\n     fonts used here: %s" % (p, ", ".join(fonts_here)))
            for ch, n in chars.most_common():
                miss = [x for x in fonts_here if ord(ch) not in cov[x]]
                if miss:
                    problems += 1
                    print("     U+%04X %-2s x%-3d %s"
                          % (ord(ch), ch, n,
                             ("!! MISSING in: " + ", ".join(miss)) if miss else "ok"))
        if not found:
            print("  none — every rendered literal in hand source is pure ASCII")

    for dpath in data_paths:
        files = []
        if os.path.isdir(dpath):
            for dp, _dn, fn in os.walk(dpath):
                files += [os.path.join(dp, f) for f in fn]
        else:
            files = [dpath]
        print("\n== RUNTIME TEXT in data: %s" % dpath)
        for p in sorted(files):
            try:
                t = open(p, "rb").read().decode("utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            chars = collections.Counter(ch for ch in t if ord(ch) > 126)
            if not chars:
                continue
            print("  %s — %d distinct non-ASCII" % (p, len(chars)))
            for ch, n in chars.most_common():
                miss = sorted(f for f in cov if ord(ch) not in cov[f])
                print("     U+%04X %-2s x%-4d %s"
                      % (ord(ch), ch, n,
                         "absent from: " + ", ".join(miss) if miss else "in every font"))

    print("\n%s" % ("PROBLEMS: %d" % problems if problems else "no coverage problems found"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
