#!/usr/bin/env python3
"""Sweep the assets orphaned by the wiimotes subtree strip.

Follow-up to design_wiimotes_strip.py, which deliberately left its orphans in place
so that commit's zip delta read as "subtree removed". This removes them:

  - string WIIMOTES_ENABLED ('ENABLED') — the status pill the mockup drops. Its font
    (DejaVuSansMonoBold_12) keeps 17 other bindings, so no font is orphaned.
  - 3 images the deleted widgets referenced: the old static tilt PNG and two figma
    icon blobs. None is referenced by any surviving widget or by hand source.

SCOPE: only the wiimotes orphans. The broader design carries ~26 more unreferenced
images (~350 KB of source blobs, incl. a byte-identical twin of the tilt PNG); that is
a separate, deliberate cleanup step — do NOT fold it in here.

Idempotent, dry-run by default.

  python3 design_wiimotes_orphan_sweep.py <design.zip> [--apply]
"""
import json, os, re, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                "../../../.claude/skills/mgs-legato-design/scripts"))
import mgs_zip

DEAD_STRING = "WIIMOTES_ENABLED"


# Referenced only by the widgets design_wiimotes_strip.py deleted.
DEAD_IMAGES = {
    "{0784247d-bbcf-4075-93dc-7dad9879e6b5}": "figmaImg_TiltControl",
    "{40cf9615-12b9-462c-b74d-181edc6e1e06}": "figmaImg_Icon_1_1",
    "{6adfb3d4-d9b7-4d84-8fbf-89d4ff2a8bff}": "figmaImg_Icon_0_1",
}

def pval(props, key, default=None):
    v = props.get(key)
    return v.get("value") if isinstance(v, dict) else default


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    zip_path = argv[1]
    apply_it = "--apply" in argv
    members = mgs_zip.members(zip_path)
    changed = False

    # ---- safety: no surviving WIDGET may reference the images we are about to drop ----
    # Scan the design JSON only, not assets/ — the images.json manifest and the assets'
    # own configs name them by definition, and are pruned below.
    live_refs = set()
    for n in members:
        if n.startswith("assets/") or not n.endswith(".json"):
            continue
        live_refs.update(re.findall(r'"type":\s*"image",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"',
                                    mgs_zip.read(zip_path, n).decode("utf-8", "ignore")))
    still_used = live_refs & set(DEAD_IMAGES)
    if still_used:
        print("!! refusing: these images are still referenced by a widget:")
        for u in sorted(still_used):
            print("   %s  %s" % (u, DEAD_IMAGES[u]))
        return 1

    # ---- strings: delete the dead one ----
    st = mgs_zip.load_json(zip_path, "stringtable.json")

    dead_ids = {pval(s.get("properties", {}), "id")
                for s in st["strings"]
                if pval(s.get("properties", {}), "name") == DEAD_STRING}

    if dead_ids:
        before_s, before_b = len(st["strings"]), len(st["bindings"])
        st["strings"] = [s for s in st["strings"]
                         if pval(s.get("properties", {}), "id") not in dead_ids]
        st["bindings"] = [b for b in st["bindings"] if b.get("string") not in dead_ids]
        print("=== deleted string %s ===" % DEAD_STRING)
        print("   strings  %d -> %d" % (before_s, len(st["strings"])))
        print("   bindings %d -> %d" % (before_b, len(st["bindings"])))
        changed = True
    else:
        print("string %s already gone" % DEAD_STRING)

    # ---- images: drop each asset directory whole AND prune the manifest ----
    # An image is listed in assets/images/images.json as well as living in its own
    # assets/images/{uuid}/ directory. Dropping only the directory leaves a manifest
    # entry pointing at nothing.
    drop = []
    for u, name in DEAD_IMAGES.items():
        pref = "assets/images/%s/" % u
        got = [n for n in members if n.startswith(pref)]
        if got:
            bytes_ = sum(len(mgs_zip.read(zip_path, n)) for n in got)
            print("\n=== dropping image %s (%d members, %d B) ===" % (name, len(got), bytes_))
            drop.append(pref)
            changed = True
        else:
            print("\nimage %s directory already gone" % name)

    manifest = mgs_zip.load_json(zip_path, "assets/images/images.json")
    before = len(manifest["images"])
    manifest["images"] = [i for i in manifest["images"] if i.get("id") not in DEAD_IMAGES]
    if len(manifest["images"]) != before:
        print("\n=== images.json manifest: %d -> %d entries ===" %
              (before, len(manifest["images"])))
        changed = True
    else:
        print("\nimages.json manifest already pruned")

    if not changed:
        print("\nnothing to do — already swept.")
        return 0

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path,
                   {"stringtable.json": json.dumps(st, indent=1),
                    "assets/images/images.json": json.dumps(manifest, indent=1)},
                   drop=drop)
    print("\napplied.")
    print("Greg: open MGS -> Generate to refresh le_gen_*.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
