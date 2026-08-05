#!/usr/bin/env python3
"""Replace an existing image asset's source blob, keeping its uuid and bindings.

Use when the artwork changed but the asset identity should not: every widget that
references it keeps working, because widgets bind the uuid, not the file. Contrast
add_image.py, which creates a new asset (and so needs rebinding).

Updates the three things that describe the blob — `sourceData`, the width/height in
`imageconfig.json`, and `colorCount` in `rawconfig.json` — and leaves everything else
(memory location, output format, RLE, mask) untouched.

  set_image_source.py <design.zip> NAME=<file.png> [NAME=<file.png> ...] [--apply]

Warns when the new artwork has different pixel dimensions, since that changes layout
wherever the image is drawn. Dry-run by default.
"""
import hashlib, json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip
from add_image import png_info


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2

    zip_path = argv[1]
    apply_it = "--apply" in argv
    pairs = []
    for a in argv[2:]:
        if a.startswith("--"):
            continue
        nm, _, path = a.partition("=")
        if not nm or not path:
            print("!! bad pair %r (want NAME=file.png)" % a)
            return 2
        pairs.append((nm, path))

    # current assets by outputName
    assets = {}
    for n in mgs_zip.members(zip_path):
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        doc = json.loads(mgs_zip.read(zip_path, n))
        nm = (doc["properties"].get("outputName", {}) or {}).get("value")
        assets[nm] = (m.group(1), n, doc)

    repl, changed = {}, 0
    for nm, path in pairs:
        if nm not in assets:
            print("!! no image named %s" % nm)
            return 1
        uuid_, member, doc = assets[nm]
        blob = open(path, "rb").read()
        w, h, ncolors = png_info(blob)

        sd = "assets/images/%s/sourceData" % uuid_
        cur = mgs_zip.read(zip_path, sd)
        if cur == blob:
            print("   same   %-30s (unchanged)" % nm)
            continue

        p = doc["properties"]
        ow = (p.get("outputWidth", {}) or {}).get("value")
        oh = (p.get("outputHeight", {}) or {}).get("value")
        note = ""
        if (ow, oh) != (w, h):
            note = "  ** size %sx%s -> %dx%d — check layout **" % (ow, oh, w, h)
        print("   set    %-30s %d -> %d B, %dx%d%s"
              % (nm, len(cur), len(blob), w, h, note))

        for k, v in (("sourceWidth", w), ("outputWidth", w),
                     ("sourceHeight", h), ("outputHeight", h)):
            if k in p and isinstance(p[k], dict):
                p[k]["value"] = v
        repl[member] = json.dumps(doc, indent=1)
        repl[sd] = blob

        rawm = "assets/images/%s/rawconfig.json" % uuid_
        if rawm in mgs_zip.members(zip_path) and ncolors is not None:
            raw = json.loads(mgs_zip.read(zip_path, rawm))
            if isinstance(raw.get("colorCount"), dict):
                raw["colorCount"]["value"] = ncolors
                repl[rawm] = json.dumps(raw, indent=1)
        changed += 1

    if not changed:
        print("\nnothing to do.")
        return 0
    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, repl)
    print("\napplied to %d image(s). Open MGS -> Generate." % changed)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
