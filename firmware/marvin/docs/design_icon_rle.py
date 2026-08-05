#!/usr/bin/env python3
"""Toggle RLE compression on the nav/button icon images in the MGS design.

Why: the nav icons show a corrupt block of roughly 5x5 px at their TOP-LEFT — the
first pixels a decoder emits — and redrawing the drawer also kills the perf-log USB
stream. One mechanism explains both: if a run length is mishandled the decode writes
past its destination, producing visible garbage at the start and corrupting whatever
follows the surface. The icons that misbehave are the ones rasterized with
rsvg-convert (large fully-transparent leading run); the untouched figma-exported
images with the same settings look fine.

Storing them raw removes RLE from the equation. 24x24 RGBA_8888 raw is 2304 B versus
~900 B encoded — a few tens of KB across the set, irrelevant against the ~600 KB the
image prune already reclaimed.

  design_icon_rle.py <design.zip> off|on [--apply]

`off` stores raw, `on` restores compression. Dry-run by default; MGS -> Generate after.
"""
import json, os, re, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                "../../../.claude/skills/mgs-legato-design/scripts"))
import mgs_zip

# The images rasterized locally rather than exported from figma.
PREFIXES = ("NAV_ICON_",)


def main(argv):
    if len(argv) < 3 or argv[2] not in ("off", "on"):
        print(__doc__)
        return 2

    zip_path = argv[1]
    want_rle = (argv[2] == "on")
    apply_it = "--apply" in argv

    repl, touched = {}, []
    for n in mgs_zip.members(zip_path):
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        nm = (json.loads(mgs_zip.read(zip_path, n))["properties"]
              .get("outputName", {}) or {}).get("value") or ""
        if not nm.startswith(PREFIXES):
            continue

        member = "assets/images/%s/rawconfig.json" % m.group(1)
        raw = json.loads(mgs_zip.read(zip_path, member))
        cur = (raw.get("useRLE") or {}).get("value")
        if cur == want_rle:
            continue
        raw["useRLE"]["value"] = want_rle
        repl[member] = json.dumps(raw, indent=1)
        touched.append((nm, cur, want_rle))

    if not touched:
        print("all matching images already have useRLE=%s." % want_rle)
        return 0

    print("=== setting useRLE=%s on %d images ===" % (want_rle, len(touched)))
    for nm, was, now in sorted(touched):
        print("   %-32s %s -> %s" % (nm, was, now))

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, repl)
    print("\napplied. Open MGS -> Generate, rebuild, then re-check the icon corners "
          "and whether the perf stream survives a drawer redraw.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
