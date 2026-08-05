#!/usr/bin/env python3
"""Delete every child of a named widget in an MGS design.zip, keeping that widget.

The move behind this: a screen's figma-imported widget tree is being replaced by a
programmatic builder in C, so the design should supply only the empty root panel the
builder attaches to. See REFERENCE.md "strip a widget subtree to hand-code a screen"
— in particular, keep the deleted widgets' STRINGS and drive them from C via
leTableString, or non-ASCII captions (d-pad arrows, U+2212 minus) will silently lose
their glyphs on the next Generate.

Reports which strings / images / fonts the deletion orphans, but does NOT remove them:
keeping that sweep separate leaves the zip delta reviewable as "subtree removed".

  strip_subtree.py <design.zip> <widget-name> [--layer NAME] [--apply]

Idempotent (re-running on an already-empty widget is a no-op) and dry-run by default.
"""
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip


def pval(props, key, default=None):
    v = props.get(key)
    return v.get("value") if isinstance(v, dict) else default


def wname(node):
    return pval(node.get("properties", {}), "name")


def find(node, name):
    if wname(node) == name:
        return node
    for c in node.get("children") or []:
        hit = find(c, name)
        if hit is not None:
            return hit
    return None


def collect(node, out):
    out.append(node)
    for c in node.get("children") or []:
        collect(c, out)


def asset_refs(nodes, kind):
    """UUIDs of `kind` assets referenced by these nodes' own properties."""
    refs = set()
    blob = json.dumps([n.get("properties", {}) for n in nodes])
    for m in re.finditer(r'"type":\s*"%s",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"' % kind, blob):
        refs.add(m.group(1))
    refs.discard(mgs_zip.NULL_UUID)
    return refs


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2

    zip_path, target = argv[1], argv[2]
    apply_it = "--apply" in argv
    layer_name = None
    if "--layer" in argv:
        layer_name = argv[argv.index("--layer") + 1]

    member = mgs_zip.screen_members(zip_path)[0]
    doc = mgs_zip.load_json(zip_path, member)

    layers = doc["layers"]
    if layer_name is not None:
        layers = [l for l in layers if wname(l) == layer_name]
        if not layers:
            print("!! layer %s not found" % layer_name)
            return 1

    root = None
    for l in layers:
        root = find(l, target)
        if root is not None:
            break
    if root is None:
        print("!! widget %s not found" % target)
        return 1

    doomed = []
    for c in root.get("children") or []:
        collect(c, doomed)

    if not doomed:
        print("already stripped — %s has no children. Nothing to do." % target)
        return 0

    print("=== deleting %d widgets under %s ===" % (len(doomed), target))
    for n in doomed:
        print("   %s" % wname(n))

    survivors = []
    for l in doc["layers"]:
        collect(l, survivors)
    survivors = [n for n in survivors if n not in doomed]

    # Resolve string uuids to names so the report is readable.
    st = mgs_zip.load_json(zip_path, "stringtable.json")
    sname = {pval(s.get("properties", {}), "id"): pval(s.get("properties", {}), "name")
             for s in st.get("strings", [])}

    for kind, label in (("string", "strings"), ("image", "images"), ("font", "fonts")):
        orphans = asset_refs(doomed, kind) - asset_refs(survivors, kind)
        if orphans:
            print("\n=== %d %s orphaned (NOT removed — sweep separately) ===" %
                  (len(orphans), label))
            for u in sorted(orphans):
                print("   %s%s" % (u, "  " + sname[u] if sname.get(u) else ""))

    print("\nKeep any orphaned string you still want to render: drive it from C with "
          "\nleTableString + stringID_*, which preserves MGS glyph auto-inclusion for "
          "\nnon-ASCII captions. See REFERENCE.md.")

    root["children"] = []

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, {member: json.dumps(doc, indent=1)})
    print("\napplied. backup at %s.bak" % zip_path)
    print("Open MGS -> Generate to refresh le_gen_*. Until then the project still builds,"
          " but the old subtree still renders under the new layout.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
