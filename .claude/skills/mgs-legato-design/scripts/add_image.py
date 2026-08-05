#!/usr/bin/env python3
"""Add a PNG to a design.zip as a new image asset, optionally binding it to widgets.

MGS stores each image as three members under assets/images/{uuid}/ — imageconfig.json
(name, output size, memory location), rawconfig.json (color mode, RLE, mask) and
sourceData (the raw PNG bytes) — plus one entry in the assets/images/images.json
manifest. All four must be written for MGS to see the asset.

Rather than synthesize the configs from scratch, this CLONES an existing image's
config (--like) and overrides only the identity + dimensions, so every field this
design's MGS version expects is present and plausible. Pick a template of the same
kind (same color mode / RLE / memory location) — usually a sibling icon.

  add_image.py <design.zip> <file.png> <OUTPUT_NAME> --like <ExistingImageName>
               [--bind WIDGET:prop[,prop...]]... [--apply]

--bind repoints a widget's image-typed property at the new asset, e.g.
  --bind BUTTON_NAV_WIIMOTES:pressedImage,releasedImage
Only the property's "value" is rewritten; enabled/visible/type are preserved.

Dry-run by default. After --apply, open MGS -> Generate to emit the new leImage.
MGS recomputes derived fields (outputSize, and colorCount if it disagrees) on
Generate, so small mismatches there self-heal.
"""
import copy, json, os, re, struct, sys, uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

MANIFEST = "assets/images/images.json"


def png_info(blob):
    """(width, height, distinct RGBA colors) for a PNG, without a decoder library."""
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("!! not a PNG: %r" % blob[:8])
    w, h = struct.unpack(">II", blob[16:24])
    colors = None
    try:
        import zlib
        bitdepth, ctype = blob[24], blob[25]
        if bitdepth == 8 and ctype in (2, 6):
            idat = b""
            i = 8
            while i < len(blob):
                ln = struct.unpack(">I", blob[i:i + 4])[0]
                typ = blob[i + 4:i + 8]
                if typ == b"IDAT":
                    idat += blob[i + 8:i + 8 + ln]
                i += 12 + ln
            raw = zlib.decompress(idat)
            bpp = 4 if ctype == 6 else 3
            stride = w * bpp
            seen, prev = set(), bytearray(stride)
            pos = 0
            for _y in range(h):
                ft = raw[pos]; pos += 1
                line = bytearray(raw[pos:pos + stride]); pos += stride
                for x in range(stride):                      # undo PNG filters
                    a = line[x - bpp] if x >= bpp else 0
                    b = prev[x]
                    c = prev[x - bpp] if x >= bpp else 0
                    if ft == 1:   line[x] = (line[x] + a) & 0xFF
                    elif ft == 2: line[x] = (line[x] + b) & 0xFF
                    elif ft == 3: line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
                    elif ft == 4:
                        p = a + b - c
                        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                        pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                        line[x] = (line[x] + pr) & 0xFF
                for x in range(0, stride, bpp):
                    seen.add(bytes(line[x:x + bpp]))
                prev = line
            colors = len(seen)
    except Exception:
        colors = None
    return w, h, colors


def find_image(zip_path, name):
    """(uuid, imageconfig props) for the image whose outputName is `name`."""
    for n in mgs_zip.members(zip_path):
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        props = json.loads(mgs_zip.read(zip_path, n)).get("properties", {})
        if (props.get("outputName", {}) or {}).get("value") == name:
            return m.group(1), props
    return None, None


def setv(props, key, value):
    if key in props and isinstance(props[key], dict):
        props[key]["value"] = value


def walk_widgets(node, fn):
    fn(node)
    for c in node.get("children") or []:
        walk_widgets(c, fn)


def main(argv):
    if len(argv) < 4 or "--like" not in argv:
        print(__doc__)
        return 2

    zip_path, png_path, out_name = argv[1], argv[2], argv[3]
    template = argv[argv.index("--like") + 1]
    apply_it = "--apply" in argv
    binds = []
    for i, a in enumerate(argv):
        if a == "--bind":
            w, _, props = argv[i + 1].partition(":")
            binds.append((w, [p for p in props.split(",") if p]))

    if find_image(zip_path, out_name)[0]:
        print("image %s already exists — nothing to do." % out_name)
        return 0

    tpl_uuid, tpl_props = find_image(zip_path, template)
    if tpl_uuid is None:
        print("!! template image %s not found" % template)
        return 1

    blob = open(png_path, "rb").read()
    w, h, ncolors = png_info(blob)
    new_uuid = "{%s}" % uuid.uuid4()

    print("=== adding image %s ===" % out_name)
    print("   uuid      %s" % new_uuid)
    print("   png       %s  %dx%d, %d B%s"
          % (os.path.basename(png_path), w, h, len(blob),
             ", %d colors" % ncolors if ncolors else ""))
    print("   template  %s (%s)" % (template, tpl_uuid))

    # --- imageconfig: clone, then override identity + dimensions ---
    img_cfg = json.loads(mgs_zip.read(zip_path, "assets/images/%s/imageconfig.json" % tpl_uuid))
    p = img_cfg["properties"]
    setv(p, "id", new_uuid)
    setv(p, "outputName", out_name)
    for k in ("sourceWidth", "outputWidth"):
        setv(p, k, w)
    for k in ("sourceHeight", "outputHeight"):
        setv(p, k, h)
    setv(p, "sourceFormat", "png")

    # --- rawconfig: clone, repoint the self-referential mask, refresh colorCount ---
    raw_cfg = json.loads(mgs_zip.read(zip_path, "assets/images/%s/rawconfig.json" % tpl_uuid))
    if isinstance(raw_cfg.get("maskColor"), dict) and "image" in raw_cfg["maskColor"]:
        raw_cfg["maskColor"]["image"] = new_uuid
    if ncolors is not None:
        setv(raw_cfg, "colorCount", ncolors)

    # --- manifest ---
    manifest = mgs_zip.load_json(zip_path, MANIFEST)
    tpl_entry = next((i for i in manifest["images"] if i.get("id") == tpl_uuid), None)
    entry = dict(tpl_entry or {"type": "RGB"})
    entry["id"] = new_uuid
    entry["name"] = out_name
    print("   manifest  %d -> %d entries" % (len(manifest["images"]), len(manifest["images"]) + 1))

    # --- widget bindings ---
    screen_member = mgs_zip.screen_members(zip_path)[0]
    screen = mgs_zip.load_json(zip_path, screen_member)
    done, missing = [], []
    for wname, props in binds:
        hits = []

        def visit(node, wname=wname, props=props, hits=hits):
            wp = node.get("properties", {})
            if (wp.get("name", {}) or {}).get("value") != wname:
                return
            for prop in props:
                cur = wp.get(prop)
                if isinstance(cur, dict) and cur.get("type") == "image":
                    hits.append((prop, cur.get("value")))
                    cur["value"] = new_uuid
                else:
                    hits.append((prop, "!! not an image property"))

        for layer in screen["layers"]:
            walk_widgets(layer, visit)
        if hits:
            done.append((wname, hits))
        else:
            missing.append(wname)

    if binds:
        print("\n=== bindings ===")
        for wname, hits in done:
            for prop, old in hits:
                print("   %-28s %-16s %s -> %s" % (wname, prop, old, new_uuid))
    if missing:
        print("!! widget(s) not found: %s" % ", ".join(missing))
        return 1

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, {
        MANIFEST: json.dumps({**manifest, "images": manifest["images"] + [entry]}, indent=1),
        screen_member: json.dumps(screen, indent=1),
    })
    # repack only replaces existing members, so append the new asset dir separately.
    import zipfile
    with zipfile.ZipFile(zip_path, "a", zipfile.ZIP_DEFLATED) as z:
        z.writestr("assets/images/%s/imageconfig.json" % new_uuid, json.dumps(img_cfg, indent=1))
        z.writestr("assets/images/%s/rawconfig.json" % new_uuid, json.dumps(raw_cfg, indent=1))
        z.writestr("assets/images/%s/sourceData" % new_uuid, blob)

    if zipfile.ZipFile(zip_path).testzip() is not None:
        print("!! zip integrity check FAILED")
        return 1

    print("\napplied. Open MGS -> Generate to emit leImage %s." % out_name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
