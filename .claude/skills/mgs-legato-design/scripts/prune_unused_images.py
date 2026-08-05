#!/usr/bin/env python3
"""Report / delete image assets that no widget and no hand-written C references.

An image is KEPT if either:
  - a widget references its uuid  ({"type":"image","value":"{uuid}"}) in the design
    JSON (screens + state.json), or
  - its generated symbol (outputName) appears in hand source outside the generated
    config/default/ tree.

Both halves matter. "Unused by widget" is NOT unused — logos, button icons and status
LEDs are routinely drawn from C with setImage(w, (leImage *)&NAME), and the generated
tree declares every symbol so it must be excluded from the grep or nothing looks dead.

The hand-source check strips comments first, then matches the bare symbol on word
boundaries. Comments are stripped because prose mentions of a name (a screen called
"Marvin" and an image called "Marvin") otherwise produce false KEEPs; the match is
left deliberately loose (no leading '&' required) so a macro alias still counts.

Deleting an image drops assets/images/{uuid}/ AND its entry in
assets/images/images.json — see REFERENCE.md; the manifest and the directories must
agree afterwards, which this asserts.

  prune_unused_images.py <design.zip> <hand-src-dir> [--apply]

Dry-run by default. After --apply, open MGS -> Generate to shrink le_gen_images.c.
"""
import hashlib, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

MANIFEST = "assets/images/images.json"


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def hand_source_blob(src_dir):
    """Every .c/.h outside the generated config/default/ tree, comments stripped."""
    generated = os.path.join(src_dir, "config", "default")
    out = []
    for root, _dirs, files in os.walk(src_dir):
        if root == generated or root.startswith(generated + os.sep):
            continue
        for f in files:
            if f.endswith((".c", ".h")):
                p = os.path.join(root, f)
                out.append(strip_comments(open(p, encoding="utf-8", errors="ignore").read()))
    return "\n".join(out)


def inventory(zip_path):
    """uuid -> (outputName, sourceData bytes, sha1) for every image asset."""
    names = mgs_zip.members(zip_path)
    imgs = {}
    for n in names:
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        props = json.loads(mgs_zip.read(zip_path, n)).get("properties", {})
        out = (props.get("outputName", {}) or {}).get("value")
        sd = "assets/images/%s/sourceData" % m.group(1)
        blob = mgs_zip.read(zip_path, sd) if sd in names else b""
        imgs[m.group(1)] = (out, len(blob), hashlib.sha1(blob).hexdigest()[:12])
    return imgs


def widget_refs(zip_path):
    refs = set()
    for n in mgs_zip.members(zip_path):
        if n.startswith("assets/") or not n.endswith(".json"):
            continue
        refs.update(re.findall(r'"type":\s*"image",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"',
                               mgs_zip.read(zip_path, n).decode("utf-8", "ignore")))
    refs.discard(mgs_zip.NULL_UUID)
    return refs


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2

    zip_path, src_dir = argv[1], argv[2]
    apply_it = "--apply" in argv

    imgs = inventory(zip_path)
    wref = widget_refs(zip_path)
    code = hand_source_blob(src_dir)

    keep, dead = [], []
    for u, (out, sz, sha) in imgs.items():
        by_widget = u in wref
        by_code = bool(out) and re.search(r"\b%s\b" % re.escape(out), code) is not None
        (keep if (by_widget or by_code) else dead).append((out, sz, u, by_widget, by_code, sha))

    print("images %d | kept %d | unused %d" % (len(imgs), len(keep), len(dead)))

    print("\n== KEEP ==")
    for out, sz, _u, w, c, _s in sorted(keep, key=lambda r: -r[1]):
        why = ("widget" if w else "") + ("+code" if w and c else ("code" if c else ""))
        print("   %-42s %9d B  %s" % (out, sz, why))

    print("\n== UNUSED ==")
    total = 0
    for out, sz, u, _w, _c, _s in sorted(dead, key=lambda r: -r[1]):
        print("   %-42s %9d B  %s" % (out, sz, u))
        total += sz
    print("   ---- %d images, %d B of sourceData" % (len(dead), total))

    # Duplicate blobs among everything still present, worth knowing either way.
    by_sha = {}
    for u, (out, sz, sha) in imgs.items():
        by_sha.setdefault(sha, []).append(out)
    dupes = {s: v for s, v in by_sha.items() if len(v) > 1}
    if dupes:
        print("\n== IDENTICAL BLOBS (same sha1) ==")
        for s, v in dupes.items():
            print("   %s  %s" % (s, sorted(v)))

    if not dead:
        print("\nnothing unused — already pruned.")
        return 0
    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    doomed = {u for _o, _s, u, _w, _c, _sh in dead}
    manifest = mgs_zip.load_json(zip_path, MANIFEST)
    before = len(manifest["images"])
    manifest["images"] = [i for i in manifest["images"] if i.get("id") not in doomed]

    mgs_zip.repack(zip_path, {MANIFEST: json.dumps(manifest, indent=1)},
                   drop=["assets/images/%s/" % u for u in doomed])

    print("\napplied. manifest %d -> %d entries" % (before, len(manifest["images"])))

    left = inventory(zip_path)
    man_ids = {i["id"] for i in mgs_zip.load_json(zip_path, MANIFEST)["images"]}
    ok = man_ids == set(left)
    print("manifest ids == asset dirs: %s (%d each)" % ("OK" if ok else "MISMATCH", len(left)))
    stale = widget_refs(zip_path) - set(left)
    print("widgets referencing a missing image: %s" % (sorted(stale) or "none"))
    print("\nOpen MGS -> Generate to shrink le_gen_images.c.")
    return 0 if ok and not stale else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
