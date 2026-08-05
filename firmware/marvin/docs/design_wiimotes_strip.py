#!/usr/bin/env python3
"""Strip the figma-imported wiimotes subtree out of marvin's MGS design.zip.

The wiimotes / manual-override screen is being rebuilt programmatically in C
(ui/screens/wiimotes/screen_wiimotes.c, the screen_bus.c model), so the design only
needs to supply the empty full-screen root panel the builder attaches to. This
deletes every child of PANEL_WIIMOTES on the SCREEN_WIIMOTES layer and leaves the
layer + root panel intact.

Idempotent, dry-run by default. Reports which strings / images / fonts the deletion
orphans so the follow-up sweep is an explicit decision rather than a surprise.

  python3 design_wiimotes_strip.py <design.zip> [--apply]
"""
import json, os, re, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                "../../../.claude/skills/mgs-legato-design/scripts"))
import mgs_zip

ROOT_PANEL = "PANEL_WIIMOTES"
LAYER      = "SCREEN_WIIMOTES"


def pval(props, key, default=None):
    v = props.get(key)
    return v.get("value") if isinstance(v, dict) else default


def wname(node):
    return pval(node.get("properties", {}), "name")


def find(node, name):
    """Depth-first search for the node named `name`."""
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
    """UUIDs of `kind` assets referenced by any of `nodes` (via their own props)."""
    refs = set()
    blob = json.dumps([n.get("properties", {}) for n in nodes])
    for m in re.finditer(r'"type":\s*"%s",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"' % kind, blob):
        refs.add(m.group(1))
    refs.discard(mgs_zip.NULL_UUID)
    return refs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    zip_path = sys.argv[1]
    apply_it = "--apply" in sys.argv

    member = mgs_zip.screen_members(zip_path)[0]
    doc = mgs_zip.load_json(zip_path, member)

    layer = next((l for l in doc["layers"] if wname(l) == LAYER), None)
    if layer is None:
        print("!! layer %s not found" % LAYER)
        return 1

    root = find(layer, ROOT_PANEL)
    if root is None:
        print("!! %s not found on %s" % (ROOT_PANEL, LAYER))
        return 1

    doomed = []
    for c in root.get("children") or []:
        collect(c, doomed)

    if not doomed:
        print("already stripped — %s has no children. Nothing to do." % ROOT_PANEL)
        return 0

    print("=== deleting %d widgets under %s ===" % (len(doomed), ROOT_PANEL))
    for n in doomed:
        print("   %s" % wname(n))

    # What the deletion orphans. Compare refs from the doomed subtree against refs
    # from everything that survives, so shared assets are not reported as orphaned.
    survivors = []
    for l in doc["layers"]:
        collect(l, survivors)
    survivors = [n for n in survivors if n not in doomed]

    for kind, label in (("string", "strings"), ("image", "images"), ("font", "fonts")):
        doomed_refs = asset_refs(doomed, kind)
        kept_refs = asset_refs(survivors, kind)
        orphans = doomed_refs - kept_refs
        if orphans:
            print("\n=== %d %s orphaned by this deletion (NOT removed here) ===" %
                  (len(orphans), label))
            for u in sorted(orphans):
                print("   %s" % u)

    print("\nOrphaned strings/images are left in place on purpose: the rebuilt screen "
          "\ndrives 17 of those 18 strings from C (leTableString), so only "
          "\nWIIMOTES_ENABLED — the pill the mockup drops — actually goes unused. "
          "\nSweeping it and the 3 images is a separate audit pass "
          "\n(skill: audit_strings_fonts.py), kept out of this commit.")

    root["children"] = []

    # The two strum labels carry figma's leading-space artifact (' UP' / ' DN'), sized
    # for an icon+text row. The mockup draws a chevron there; the arrow glyphs already
    # exist in the design (the d-pad strings), so fold them into the value.
    # Down is spelled out (Greg) rather than kept as figma's abbreviated 'DN'.
    strum = {"GUITAR_STRUM_UP": "▲ UP", "GUITAR_STRUM_DOWN": "▼ DOWN"}
    st = mgs_zip.load_json(zip_path, "stringtable.json")
    retargeted = []
    for s in st["strings"]:
        nm = pval(s.get("properties", {}), "name")
        if nm in strum:
            for v in s.get("values", []):
                if v.get("value") != strum[nm]:
                    retargeted.append((nm, v["value"], strum[nm]))
                    v["value"] = strum[nm]

    if retargeted:
        print("\n=== retargeting %d strum labels ===" % len(retargeted))
        for nm, old, new in retargeted:
            print("   %-18s %r -> %r" % (nm, old, new))
        print("   (MGS will auto-add the arrows to those two strings' bound fonts on"
              " Generate)")

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, {member: json.dumps(doc, indent=1),
                              "stringtable.json": json.dumps(st, indent=1)})
    print("\napplied.")
    print("Greg: open MGS -> Generate to refresh le_gen_* before building.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
