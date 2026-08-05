#!/usr/bin/env python3
"""Rename image assets in a design.zip: OLD=NEW pairs.

An image's name lives in TWO places that must move together:
  - assets/images/{uuid}/imageconfig.json  -> properties.outputName.value
  - assets/images/images.json              -> the entry's "name"
Widgets reference the uuid, not the name, so a rename never disturbs a binding.

What it CAN break is the C build: the generated symbol in le_gen_images.c /
le_gen_assets.h is the name, so any hand source doing `&OLD` stops compiling. This
refuses to rename a code-referenced image unless --force is given, and prints the
source patch you would need.

  rename_images.py <design.zip> <hand-src-dir> OLD=NEW [OLD=NEW ...] [--apply] [--force]

Idempotent (a pair whose OLD is already gone and NEW already present is skipped).
"""
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

MANIFEST = "assets/images/images.json"


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def hand_source(src_dir):
    """{path: comment-stripped text} for .c/.h outside the generated tree."""
    generated = os.path.join(src_dir, "config", "default")
    out = {}
    for root, _d, files in os.walk(src_dir):
        if root == generated or root.startswith(generated + os.sep):
            continue
        for f in files:
            if f.endswith((".c", ".h")):
                p = os.path.join(root, f)
                out[p] = strip_comments(open(p, encoding="utf-8", errors="ignore").read())
    return out


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2

    zip_path, src_dir = argv[1], argv[2]
    apply_it = "--apply" in argv
    force = "--force" in argv
    pairs = []
    for a in argv[3:]:
        if a.startswith("--"):
            continue
        old, _, new = a.partition("=")
        if not old or not new:
            print("!! bad pair %r (want OLD=NEW)" % a)
            return 2
        pairs.append((old, new))

    # uuid -> (member, props) for every image, keyed by current outputName
    by_name = {}
    for n in mgs_zip.members(zip_path):
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        doc = json.loads(mgs_zip.read(zip_path, n))
        nm = (doc["properties"].get("outputName", {}) or {}).get("value")
        by_name[nm] = (n, m.group(1), doc)

    manifest = mgs_zip.load_json(zip_path, MANIFEST)
    code = hand_source(src_dir)

    todo, skipped, blocked = [], [], []
    for old, new in pairs:
        if old not in by_name:
            (skipped if new in by_name else blocked).append(
                (old, new, "already renamed" if new in by_name else "no such image"))
            continue
        if new in by_name:
            blocked.append((old, new, "target name already in use"))
            continue
        refs = [p for p, txt in code.items() if re.search(r"\b%s\b" % re.escape(old), txt)]
        if refs and not force:
            blocked.append((old, new, "referenced in hand source: %s"
                            % ", ".join(os.path.relpath(r, src_dir) for r in refs)))
            continue
        todo.append((old, new, by_name[old], refs))

    for old, new, why in skipped:
        print("   skip  %-26s %s" % (old, why))
    for old, new, why in blocked:
        print("   !!    %-26s -> %-26s %s" % (old, new, why))
    if blocked and not force:
        print("\nrefusing: fix the above (or pass --force and patch the source).")
        return 1
    if not todo:
        print("nothing to rename.")
        return 0

    print("=== renaming %d images ===" % len(todo))
    for old, new, (member, uuid_, doc), refs in todo:
        print("   %-26s -> %-26s %s" % (old, new, uuid_))
        if refs:
            print("        source patch needed: &%s -> &%s in %s"
                  % (old, new, ", ".join(os.path.relpath(r, src_dir) for r in refs)))

    replacements = {}
    for old, new, (member, uuid_, doc), _refs in todo:
        doc["properties"]["outputName"]["value"] = new
        replacements[member] = json.dumps(doc, indent=1)
        for entry in manifest["images"]:
            if entry.get("id") == uuid_:
                entry["name"] = new
    replacements[MANIFEST] = json.dumps(manifest, indent=1)

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, replacements)

    # verify: outputName and manifest name agree for every image
    bad = []
    man = {i["id"]: i["name"] for i in mgs_zip.load_json(zip_path, MANIFEST)["images"]}
    for n in mgs_zip.members(zip_path):
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        nm = (json.loads(mgs_zip.read(zip_path, n))["properties"].get("outputName", {})
              or {}).get("value")
        if man.get(m.group(1)) != nm:
            bad.append((m.group(1), nm, man.get(m.group(1))))
    print("\napplied. outputName == manifest name for all %d images: %s"
          % (len(man), "OK" if not bad else "MISMATCH %s" % bad))
    print("Open MGS -> Generate to rename the generated leImage symbols.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
