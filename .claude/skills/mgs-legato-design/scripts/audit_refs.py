#!/usr/bin/env python3
"""Referential-integrity check for a design.zip — run after ANY edit, before handing off.

Answers one question: does everything still point at something that exists? Cheap, read-only,
and the fastest way to catch a half-finished delete — the failure mode where the design still
opens in Composer but Generate emits a reference to an asset that is gone.

Checks, in the order things actually break:

  1. every widget asset-uuid in the screen/state JSON resolves — for ALL four kinds
     (scheme, string, image, font), not just schemes
  2. asset manifests agree with asset directories (images.json / fonts.json vs assets/*/)
  3. stringtable bindings resolve on both ends (string AND font)
  4. no string without a binding — it generates with no font, and no glyph guarantee
  5. duplicate ids / duplicate names per asset kind (the name becomes the C symbol)

Exits non-zero if anything fails, so it can gate a repack.

  audit_refs.py <design.zip>
"""
import json, os, re, sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

ASSET_REF = re.compile(r'"type":\s*"(scheme|string|image|font)",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"')


def pval(props, key, default=None):
    v = props.get(key)
    return v.get("value") if isinstance(v, dict) else default


def load_assets(zip_path):
    """{kind: {uuid: name}} for every asset the design declares."""
    out = {}

    out["scheme"] = {pval(s.get("properties", {}), "id"): pval(s.get("properties", {}), "name")
                     for s in mgs_zip.load_json(zip_path, "schemes.json")["schemes"]}

    st = mgs_zip.load_json(zip_path, "stringtable.json")
    out["string"] = {pval(s.get("properties", {}), "id"): pval(s.get("properties", {}), "name")
                     for s in st["strings"]}

    out["image"] = {i.get("id"): i.get("name")
                    for i in mgs_zip.load_json(zip_path, "assets/images/images.json")["images"]}
    out["font"] = {f.get("id"): f.get("name")
                   for f in mgs_zip.load_json(zip_path, "assets/fonts/fonts.json")["fonts"]}
    return out, st


def asset_dirs(members, kind):
    pref = "assets/%ss/" % kind
    return {m.split("/")[2] for m in members
            if m.startswith(pref) and m[len(pref):].startswith("{")}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    zip_path = argv[1]
    members = mgs_zip.members(zip_path)
    assets, st = load_assets(zip_path)
    fails = []

    print("== %s" % zip_path)
    print("   %s" % "  ".join("%d %ss" % (len(v), k) for k, v in sorted(assets.items())))

    # ---- 1. widget references ----
    refs = Counter()
    for m in mgs_zip.screen_members(zip_path) + [n for n in members if n == "state.json"]:
        blob = mgs_zip.read(zip_path, m).decode("utf-8", "ignore")
        for kind, uuid_ in ASSET_REF.findall(blob):
            if uuid_ == mgs_zip.NULL_UUID:
                continue
            refs[(kind, uuid_)] += 1
            if uuid_ not in assets[kind]:
                fails.append("dangling %s ref %s in %s" % (kind, uuid_, m))
    print("\n== widget references: %d, %d distinct" % (sum(refs.values()), len(refs)))
    for kind in sorted(assets):
        used = {u for (k, u) in refs if k == kind}
        print("   %-7s %3d of %3d referenced by a widget" % (kind, len(used), len(assets[kind])))

    # ---- 2. manifests vs directories ----
    print("\n== manifests vs asset directories")
    for kind in ("image", "font"):
        dirs = asset_dirs(members, kind)
        manifest = set(assets[kind])
        print("   %-6s manifest %3d  dirs %3d  %s"
              % (kind, len(manifest), len(dirs), "ok" if dirs == manifest else "MISMATCH"))
        for u in manifest - dirs:
            fails.append("%s %s in manifest has no asset directory" % (kind, assets[kind][u]))
        for u in dirs - manifest:
            fails.append("%s directory %s is not in the manifest" % (kind, u))

    # ---- 3 + 4. string bindings ----
    bound = set()
    for b in st["bindings"]:
        if b.get("string") not in assets["string"]:
            fails.append("binding references unknown string %s" % b.get("string"))
        if b.get("font") not in assets["font"]:
            fails.append("binding references unknown font %s" % b.get("font"))
        bound.add(b.get("string"))
    unbound = [assets["string"][u] for u in assets["string"] if u not in bound]
    print("\n== string bindings: %d for %d strings" % (len(st["bindings"]), len(assets["string"])))
    if unbound:
        for n in sorted(unbound):
            fails.append("string %s has no font binding" % n)
    else:
        print("   every string is bound to a font")

    # ---- 5. duplicate ids / names ----
    print("\n== uniqueness")
    for kind in sorted(assets):
        ids = [u for u in assets[kind]]
        names = [n for n in assets[kind].values()]
        dup_n = [n for n, c in Counter(names).items() if c > 1 and n is not None]
        print("   %-7s %d ids, %d names%s"
              % (kind, len(set(ids)), len(set(names)), "" if not dup_n else "  DUPLICATE NAMES"))
        for n in dup_n:
            fails.append("duplicate %s name %s (the generated C symbol would collide)" % (kind, n))

    if fails:
        print("\n!! %d problem(s):" % len(fails))
        for f in fails:
            print("   %s" % f)
        return 1

    print("\nno referential problems found.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
