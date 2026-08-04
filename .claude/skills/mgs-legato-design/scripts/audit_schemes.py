#!/usr/bin/env python3
"""Audit the schemes in an MGS-Legato design.zip: role, color, usage, duplicates.

    audit_schemes.py <design.zip> [hand-src-dir]

For each scheme prints role (TEXT / FILL / NEUTRAL / COMPONENT), the key hex, and
usage. Reports duplicate-color groups (merge candidates), schemes referenced by
neither a widget nor hand code (delete candidates), and the colorMode spread.

`hand-src-dir` (optional) is your hand-written source root (e.g. `<proj>/default/src`).
Scheme names referenced there are flagged as code-used so you don't rename/delete
them without a source patch. The generated `config/default/` tree is auto-excluded
(it declares every symbol → false positives).

Read-only. Pairs with mgs_zip.py for the actual edit + repack.
"""
import glob, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

NEUTRAL_BASES = {"#C8D0D4", "#FFFFFF", "#E6E3E6"}   # Legato default / off-white bases


def hx(c):
    return "#%02X%02X%02X" % (round(c["red"] * 255), round(c["green"] * 255), round(c["blue"] * 255))


def role(p):
    b, t = hx(p["base"]), hx(p["text"])
    if b in NEUTRAL_BASES and t != "#000000":     return "TEXT", t
    if b not in NEUTRAL_BASES and t == "#000000": return "FILL", b
    if b in NEUTRAL_BASES and t == "#000000":     return "NEUTRAL", b
    return "COMPONENT", b + "/" + t


def color_sig(p):
    keys = [k for k, v in p.items() if isinstance(v, dict) and v.get("type") == "color"]
    return tuple((k, hx(p[k])) for k in sorted(keys))


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    zp = argv[1]
    src_dir = argv[2] if len(argv) > 2 else None

    schemes = mgs_zip.load_json(zp, "schemes.json")["schemes"]
    id2name = {s["properties"]["id"]["value"]: s["properties"]["name"]["value"] for s in schemes}
    used_uuid = mgs_zip.scheme_uuids_used(zp)
    used_names = {id2name[u] for u in used_uuid if u in id2name}

    code_used = set()
    if src_dir:
        files = [f for f in glob.glob(os.path.join(src_dir, "**", "*.[ch]"), recursive=True)
                 if "/config/default/" not in f.replace(os.sep, "/")]
        blob = "\n".join(open(f, errors="ignore").read() for f in files)
        allnames = sorted(id2name.values(), key=len, reverse=True)
        if allnames:
            code_used = set(re.findall(r"\b(" + "|".join(re.escape(n) for n in allnames) + r")\b", blob))

    # colorMode spread
    modes = {}
    for s in schemes:
        v = s["properties"]["colorMode"]["value"]
        modes[v] = modes.get(v, 0) + 1

    # per-scheme table
    print("=== schemes (%d) ===" % len(schemes))
    print("%-34s %-9s %-16s %-6s %-5s" % ("name", "role", "hex", "widget", "code"))
    for s in sorted(schemes, key=lambda s: s["properties"]["name"]["value"]):
        p = s["properties"]; nm = p["name"]["value"]; r, c = role(p)
        print("%-34s %-9s %-16s %-6s %-5s" % (
            nm, r, c,
            "yes" if nm in used_names else "-",
            "yes" if nm in code_used else "-"))

    # duplicates
    from collections import defaultdict
    groups = defaultdict(list)
    for s in schemes:
        groups[color_sig(s["properties"])].append(s["properties"]["name"]["value"])
    dups = {k: v for k, v in groups.items() if len(v) > 1}

    # unused (neither widget nor code)
    unused = [id2name[u_id] for s in schemes
              for u_id in [s["properties"]["id"]["value"]]
              if id2name[u_id] not in used_names and id2name[u_id] not in code_used]

    print("\n=== summary ===")
    print("colorMode spread (value:count):", modes, " (6 = RGBA_8888)")
    print("used by widgets: %d | referenced in hand code: %d" % (len(used_names), len(code_used)))
    print("duplicate-color groups: %d (collapsible: %d)" %
          (len(dups), sum(len(v) - 1 for v in dups.values())))
    for names in list(dups.values())[:20]:
        print("   dup:", sorted(names))
    print("unused (delete candidates): %d" % len(unused))
    print("   ", sorted(unused)[:40])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
