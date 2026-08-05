#!/usr/bin/env python3
"""Export the original image / font source blobs out of a design.zip.

MGS stores the *source* asset (the PNG you imported, the TTF) inside the zip as
`sourceData`, alongside the config that describes how to convert it. That makes the
zip the only copy of your original artwork unless you archive it separately — which is
awkward to review, easy to lose, and invisible to anyone browsing the repo.

This writes each asset out under `<outdir>`, named after its design asset name:

    <outdir>/image/<outputName>.<sourceFormat>
    <outdir>/font/<PostScript name>.ttf      (deduped — one TTF backs many sizes)

Fonts are deduplicated by content hash and named from the TTF's own `name` table
rather than the design's label, because MGS keeps a requested font name even when it
substituted a different face (see REFERENCE.md).

Reports per file whether it is new, identical to what's already there, or DIFFERS
(so re-running after design changes shows you exactly what moved).

  export_assets.py <design.zip> <outdir> [--apply] [--images-only|--fonts-only]

Dry-run by default. Never deletes anything: files present in <outdir> but no longer in
the design are listed as "extra" and left alone.
"""
import hashlib, json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip


def ttf_names(blob):
    """{nameID: str} from a TTF/OTF `name` table. Enough to get family/PostScript."""
    try:
        if blob[:4] not in (b"\x00\x01\x00\x00", b"true", b"ttcf", b"OTTO"):
            return {}
        numtables = struct.unpack(">H", blob[4:6])[0]
        off = None
        for i in range(numtables):
            rec = 12 + i * 16
            if blob[rec:rec + 4] == b"name":
                off = struct.unpack(">I", blob[rec + 8:rec + 12])[0]
                break
        if off is None:
            return {}
        fmt, count, stroff = struct.unpack(">HHH", blob[off:off + 6])
        out = {}
        for i in range(count):
            r = off + 6 + i * 12
            pid, eid, lid, nid, ln, o = struct.unpack(">HHHHHH", blob[r:r + 12])
            raw = blob[off + stroff + o: off + stroff + o + ln]
            try:
                s = raw.decode("utf-16-be") if (pid == 3 or (pid == 0)) else raw.decode("latin-1")
            except Exception:
                continue
            out.setdefault(nid, s.strip())
        return out
    except Exception:
        return {}


def safe(name):
    return re.sub(r"[^A-Za-z0-9._-]", "_", name or "unnamed")


def collect(zip_path):
    """[(subdir, filename, blob)] for every asset worth archiving."""
    names = mgs_zip.members(zip_path)
    out = []

    for n in names:
        m = re.match(r"assets/images/(\{[0-9a-fA-F-]+\})/imageconfig.json", n)
        if not m:
            continue
        p = json.loads(mgs_zip.read(zip_path, n)).get("properties", {})
        nm = (p.get("outputName", {}) or {}).get("value")
        ext = ((p.get("sourceFormat", {}) or {}).get("value") or "png").lstrip(".")
        sd = "assets/images/%s/sourceData" % m.group(1)
        if sd in names and nm:
            out.append(("image", "%s.%s" % (safe(nm), ext), mgs_zip.read(zip_path, sd)))

    seen = {}
    for n in names:
        m = re.match(r"assets/fonts/(\{[0-9a-fA-F-]+\})/fontconfig.json", n)
        if not m:
            continue
        p = json.loads(mgs_zip.read(zip_path, n)).get("properties", {})
        nm = (p.get("outputName", {}) or {}).get("value")
        sd = "assets/fonts/%s/sourceData" % m.group(1)
        if sd not in names:
            continue
        blob = mgs_zip.read(zip_path, sd)
        h = hashlib.sha1(blob).hexdigest()
        if h in seen:
            continue                       # one TTF backs many design sizes
        seen[h] = True
        nt = ttf_names(blob)
        base = nt.get(6) or nt.get(4) or re.sub(r"_\d+$", "", nm or "font")
        out.append(("font", "%s.ttf" % safe(base), blob))

    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2

    zip_path, outdir = argv[1], argv[2]
    apply_it = "--apply" in argv
    want = {"image", "font"}
    if "--images-only" in argv:
        want = {"image"}
    if "--fonts-only" in argv:
        want = {"font"}

    items = [i for i in collect(zip_path) if i[0] in want]
    if not items:
        print("no assets to export.")
        return 0

    new = same = differs = 0
    for sub, fn, blob in sorted(items):
        dest = os.path.join(outdir, sub, fn)
        if os.path.exists(dest):
            cur = open(dest, "rb").read()
            if cur == blob:
                state, same = "same", same + 1
            else:
                state, differs = "DIFFERS (%d -> %d B)" % (len(cur), len(blob)), differs + 1
        else:
            state, new = "new", new + 1
        print("   %-6s %-34s %8d B  %s" % (sub, fn, len(blob), state))

    print("\n%d new, %d unchanged, %d differ" % (new, same, differs))

    # anything already archived that the design no longer contains
    expected = {os.path.join(sub, fn) for sub, fn, _ in items}
    extra = []
    for sub in sorted(want):
        d = os.path.join(outdir, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.startswith("."):
                continue
            if os.path.join(sub, f) not in expected:
                extra.append(os.path.join(sub, f))
    if extra:
        print("\nalready in %s but not in the design (left alone):" % outdir)
        for e in extra:
            print("   %s" % e)

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the files")
        return 0

    for sub, fn, blob in items:
        d = os.path.join(outdir, sub)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, fn), "wb") as fh:
            fh.write(blob)
    print("\nwrote %d files under %s" % (len(items), outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
