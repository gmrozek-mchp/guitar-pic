#!/usr/bin/env python3
"""Reusable helpers for reading / repacking an MGS (MPLAB Graphics Suite) / Legato design.zip.

The design.zip is a plain zip of JSON (+ font/image blobs). These helpers let a
transform script load the JSON members it cares about, repack the zip in place
(replacing only those members, preserving everything else) with a backup, and
validate that every widget scheme-uuid still resolves.

Library use:
    import mgs_zip
    doc = mgs_zip.load_json(zip_path, "schemes.json")
    screens = mgs_zip.screen_members(zip_path)          # ["screens/{uuid}/screen.json", ...]
    used = mgs_zip.scheme_uuids_used(zip_path)          # set of uuids referenced by widgets
    mgs_zip.repack(zip_path, {"schemes.json": json.dumps(doc, indent=1)})   # backs up + writes

CLI:
    mgs_zip.py list   <zip>
    mgs_zip.py cat    <zip> <member>
    mgs_zip.py used   <zip>            # scheme uuids referenced by widgets
"""
import json, os, re, shutil, sys, zipfile

NULL_UUID = "{00000000-0000-0000-0000-000000000000}"
_SCHEME_REF = re.compile(r'"type":\s*"scheme",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"')


def generated_root(zip_path):
    """Absolute path of the MCC/Harmony generated tree, derived from the zip's own
    location. The design zip lives in the configuration directory
    (`<proj>/default/src/config/<CONFIG_NAME>/`), which is exactly the tree that
    holds the generated le_gen_* sources — so this needs no guess about the
    configuration being named "default"."""
    return os.path.dirname(os.path.abspath(zip_path))


def hand_source_files(src_dir, zip_path=None, exts=(".c", ".h")):
    """Every source file under `src_dir` OUTSIDE the generated tree.

    The generated tree declares every asset symbol, so including it makes
    everything look code-referenced. Pass `zip_path` to locate that tree exactly;
    without it, fall back to skipping any `config/<name>/` segment."""
    skip_abs = os.path.abspath(generated_root(zip_path)) if zip_path else None
    out = []
    for root, _dirs, files in os.walk(src_dir):
        rp = os.path.abspath(root)
        if skip_abs is not None:
            if rp == skip_abs or rp.startswith(skip_abs + os.sep):
                continue
        elif re.search(r"(^|/)config/[^/]+(/|$)", root.replace(os.sep, "/")):
            continue
        for f in files:
            if f.endswith(exts):
                out.append(os.path.join(root, f))
    return out


def strip_c_comments(src):
    """Drop /* */ and // comments. Do this before matching an asset name in source:
    prose mentions of a name produce false 'still referenced' verdicts."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def members(zip_path):
    with zipfile.ZipFile(zip_path) as z:
        return z.namelist()


def read(zip_path, name):
    with zipfile.ZipFile(zip_path) as z:
        return z.read(name)


def load_json(zip_path, name):
    return json.loads(read(zip_path, name))


def screen_members(zip_path):
    return [n for n in members(zip_path)
            if n.startswith("screens/") and n.endswith("screen.json")]


def scheme_uuids_used(zip_path):
    """UUIDs of schemes referenced by any widget (screens + state), minus null."""
    used = set()
    names = members(zip_path)
    targets = [n for n in names if n in ("state.json",)] + screen_members(zip_path)
    for n in targets:
        used.update(_SCHEME_REF.findall(read(zip_path, n).decode("utf-8", "ignore")))
    used.discard(NULL_UUID)
    return used


def repoint_scheme_refs(text, remap):
    """Rewrite widget scheme-uuid refs: {dead: survivor} -> survivor, in raw JSON text."""
    for dead, surv in remap.items():
        text = text.replace('"value": "%s"' % dead, '"value": "%s"' % surv)
        text = text.replace('"value":"%s"' % dead, '"value":"%s"' % surv)
    return text


def repack(zip_path, replacements, backup=True, backup_suffix=".bak", drop=()):
    """Write a new zip with `replacements` (name -> str|bytes) swapped in, members in
    `drop` omitted, every other member copied verbatim. Backs up the original first.
    Verifies integrity.

    `drop` takes member names or prefixes ending in "/" (e.g. deleting a font means
    dropping its whole "assets/fonts/{uuid}/" directory — 4 members)."""
    drop_exact = {d for d in drop if not d.endswith("/")}
    drop_pref = tuple(d for d in drop if d.endswith("/"))

    def dropped(name):
        return name in drop_exact or name.startswith(drop_pref) if drop_pref \
            else name in drop_exact

    if backup:
        bak = zip_path + backup_suffix
        if not os.path.exists(bak):
            shutil.copy2(zip_path, bak)
    tmp = zip_path + ".new"
    with zipfile.ZipFile(zip_path) as zin, \
         zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            if dropped(item.filename):
                continue
            if item.filename in replacements:
                data = replacements[item.filename]
                zout.writestr(item, data.encode("utf-8") if isinstance(data, str) else data)
            else:
                zout.writestr(item, zin.read(item.filename))
    if zipfile.ZipFile(tmp).testzip() is not None:
        os.remove(tmp)
        raise RuntimeError("repacked zip failed integrity check")
    os.replace(tmp, zip_path)


def validate_scheme_refs(zip_path, schemes_doc=None):
    """Return list of dangling widget scheme-uuids (referenced but no surviving scheme).
    Pass schemes_doc to validate an in-memory edit before repack; else reads the zip."""
    if schemes_doc is None:
        schemes_doc = load_json(zip_path, "schemes.json")
    ids = {s["properties"]["id"]["value"] for s in schemes_doc["schemes"]}
    return sorted(u for u in scheme_uuids_used(zip_path) if u not in ids)


def _cli(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    cmd, zp = argv[1], argv[2]
    if cmd == "list":
        for n in members(zp):
            print(n)
    elif cmd == "cat":
        sys.stdout.buffer.write(read(zp, argv[3]))
    elif cmd == "used":
        for u in sorted(scheme_uuids_used(zp)):
            print(u)
    else:
        print("unknown cmd:", cmd); return 2
    return 0


if __name__ == "__main__":
    sys.exit(_cli(sys.argv))
