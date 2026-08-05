#!/usr/bin/env python3
"""Audit strings + fonts in an MGS / Legato design.zip.

Reports, for a design zip:
  * every string: name, value, the font bound to it (per language), and whether any
    widget (screens/state JSON) or hand-written C source references it
  * every font: name, source face/size, byte cost of its glyph data, and how many
    strings bind to it (+ direct C references)
  * the unused sets on both sides, and the "non-preferred face" string set

Usage:
    audit_strings_fonts.py <design.zip> [hand-src-dir] [--face DejaVuSansMono]
    audit_strings_fonts.py <design.zip> [hand-src-dir] --json      # machine-readable

Hand-source scanning EXCLUDES any path containing 'config/default' — the generated
tree declares every symbol and would make everything look used.
"""
import json
import os
import re
import sys
import zipfile
from collections import defaultdict

NULL_UUID = "{00000000-0000-0000-0000-000000000000}"

# widget asset references look like {"type": "string", "value": "{uuid}"}
def _ref_re(kind):
    return re.compile(r'"type":\s*"%s",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"' % kind)


def prop(obj, key, default=None):
    p = obj.get("properties", {}).get(key)
    return p.get("value") if p else default


class Design:
    def __init__(self, path):
        self.path = path
        self.z = zipfile.ZipFile(path)
        self.names = self.z.namelist()

    def read(self, name):
        return self.z.read(name)

    def load(self, name):
        return json.loads(self.read(name))

    def screen_members(self):
        return [n for n in self.names
                if n.startswith("screens/") and n.endswith("screen.json")]

    def ref_text(self):
        """Concatenated JSON text of everything that can reference an asset."""
        parts = []
        for n in ["state.json", "screens/screens.json"] + self.screen_members():
            if n in self.names:
                parts.append(self.read(n).decode("utf-8", "ignore"))
        return "\n".join(parts)

    # ---- strings ----
    def strings(self):
        st = self.load("stringtable.json")
        langs = {prop(l, "id"): prop(l, "name") for l in st["languages"]}
        out = {}
        for s in st["strings"]:
            sid = prop(s, "id")
            out[sid] = {
                "id": sid,
                "name": prop(s, "name"),
                "path": prop(s, "path"),
                "values": {v["language"]: v.get("value", "") for v in s["values"]},
            }
        bindings = defaultdict(dict)   # string uuid -> {lang: font uuid}
        for b in st["bindings"]:
            bindings[b["string"]][b["language"]] = b["font"]
        for sid, s in out.items():
            s["fonts"] = bindings.get(sid, {})
        return out, langs, st

    # ---- fonts ----
    def fonts(self):
        fj = self.load("assets/fonts/fonts.json")
        out = {}
        for f in fj["fonts"]:
            fid, name = f["id"], f["name"]
            cfgname = "assets/fonts/%s/fontconfig.json" % fid
            cfg = self.load(cfgname) if cfgname in self.names else {}
            gl = "assets/fonts/%s/glyphs.json" % fid
            rg = "assets/fonts/%s/ranges.json" % fid
            sd = "assets/fonts/%s/sourceData" % fid
            info = self.z.getinfo(sd) if sd in self.names else None
            ranges = self.load(rg) if rg in self.names else None
            glyphs = self.load(gl) if gl in self.names else None
            out[fid] = {
                "id": fid,
                "name": name,
                "cfg": cfg,
                "source_bytes": info.file_size if info else 0,
                "source_bytes_z": info.compress_size if info else 0,
                "ranges": ranges,
                "glyphs": glyphs,
            }
        return out, fj


def scan_hand_src(root):
    """Return (text, files) for hand-written C/H source, excluding generated tree."""
    chunks, files = [], []
    for dirpath, dirnames, filenames in os.walk(root):
        if "config/default" in dirpath.replace(os.sep, "/"):
            dirnames[:] = []
            continue
        for fn in filenames:
            if fn.endswith((".c", ".h", ".cpp")):
                p = os.path.join(dirpath, fn)
                try:
                    with open(p, "r", errors="ignore") as fh:
                        chunks.append((p, fh.read()))
                except OSError:
                    pass
                files.append(p)
    return chunks, files


def font_face(f):
    """Best-effort human description of a font's source face + size."""
    cfg = f.get("cfg") or {}
    p = cfg.get("properties", cfg)
    def g(*keys):
        for k in keys:
            v = p.get(k)
            if isinstance(v, dict):
                v = v.get("value")
            if v not in (None, ""):
                return v
        return None
    return {
        "family": g("fontFamily", "family", "font", "typeface", "sourceFile", "fileName"),
        "size": g("fontSize", "size", "height", "pointSize"),
        "style": g("fontStyle", "style"),
        "bold": g("bold"),
        "italic": g("italic"),
        "antialias": g("antialias", "antiAlias", "aa"),
    }


def glyph_count(f):
    g = f.get("glyphs")
    if isinstance(g, dict):
        for k in ("glyphs", "list", "entries"):
            if isinstance(g.get(k), list):
                return len(g[k])
        return len(g)
    if isinstance(g, list):
        return len(g)
    return None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    zip_path = argv[1]
    hand_dir = None
    as_json = "--json" in argv
    preferred = "DejaVuSansMono"
    for i, a in enumerate(argv[2:], 2):
        if a == "--face" and i + 1 < len(argv):
            preferred = argv[i + 1]
        elif not a.startswith("--") and (argv[i - 1] != "--face") and os.path.isdir(a):
            hand_dir = a

    d = Design(zip_path)
    strings, langs, _ = d.strings()
    fonts, _ = d.fonts()
    reftext = d.ref_text()

    str_refs = defaultdict(int)
    for u in _ref_re("string").findall(reftext):
        str_refs[u] += 1
    font_refs = defaultdict(int)
    for u in _ref_re("font").findall(reftext):
        font_refs[u] += 1
    str_refs.pop(NULL_UUID, None)
    font_refs.pop(NULL_UUID, None)

    # hand-source references, by generated C symbol name
    code_str, code_font = defaultdict(list), defaultdict(list)
    if hand_dir:
        chunks, _files = scan_hand_src(hand_dir)
        sname = {s["name"]: sid for sid, s in strings.items()}
        fname = {f["name"]: fid for fid, f in fonts.items()}
        for path, text in chunks:
            for tok in set(re.findall(r"stringID_([A-Za-z_][A-Za-z0-9_]*)", text)):
                if tok in sname:
                    code_str[sname[tok]].append(path)
            for tok in set(re.findall(r"[&\b]([A-Za-z_][A-Za-z0-9_]*)", text)):
                if tok in fname:
                    code_font[fname[tok]].append(path)

    # font usage from string bindings
    font_bind = defaultdict(list)
    for sid, s in strings.items():
        for lang, fid in s["fonts"].items():
            font_bind[fid].append(sid)

    result = {
        "zip": zip_path,
        "counts": {
            "strings": len(strings),
            "fonts": len(fonts),
            "languages": len(langs),
            "screens": len(d.screen_members()),
        },
        "strings": [], "fonts": [],
    }
    for sid, s in sorted(strings.items(), key=lambda kv: kv[1]["name"] or ""):
        fids = list(s["fonts"].values())
        result["strings"].append({
            "id": sid, "name": s["name"], "path": s["path"],
            "value": next(iter(s["values"].values()), ""),
            "fonts": [fonts.get(f, {}).get("name", "<missing %s>" % f) for f in fids],
            "widget_refs": str_refs.get(sid, 0),
            "code_refs": sorted(set(code_str.get(sid, []))),
        })
    for fid, f in sorted(fonts.items(), key=lambda kv: kv[1]["name"] or ""):
        result["fonts"].append({
            "id": fid, "name": f["name"],
            "face": font_face(f),
            "glyphs": glyph_count(f),
            "source_bytes": f["source_bytes"],
            "bound_strings": len(font_bind.get(fid, [])),
            "widget_refs": font_refs.get(fid, 0),
            "code_refs": sorted(set(code_font.get(fid, []))),
        })

    if as_json:
        print(json.dumps(result, indent=1))
        return 0

    c = result["counts"]
    print("== %s" % zip_path)
    print("   %d strings, %d fonts, %d languages, %d screen(s)"
          % (c["strings"], c["fonts"], c["languages"], c["screens"]))
    if hand_dir:
        print("   hand source: %s" % hand_dir)

    print("\n== FONTS (name | face/size | glyphs | srcBytes | boundStrings | widgetRefs | codeRefs)")
    for f in result["fonts"]:
        face = f["face"]
        print("  %-32s %-22s %5s %9d  bind=%-4d wref=%-3d code=%d"
              % (f["name"], "%s/%s" % (face.get("family"), face.get("size")),
                 f["glyphs"], f["source_bytes"], f["bound_strings"],
                 f["widget_refs"], len(f["code_refs"])))

    dead_fonts = [f for f in result["fonts"]
                  if not f["bound_strings"] and not f["widget_refs"] and not f["code_refs"]]
    print("\n== UNUSED FONTS (%d of %d) — no string binding, no widget ref, no C ref"
          % (len(dead_fonts), len(result["fonts"])))
    for f in dead_fonts:
        print("  %-32s %8d bytes" % (f["name"], f["source_bytes"]))

    dead_strs = [s for s in result["strings"] if not s["widget_refs"] and not s["code_refs"]]
    print("\n== UNUSED STRINGS (%d of %d) — no widget ref, no C ref"
          % (len(dead_strs), len(result["strings"])))
    for s in dead_strs:
        print("  %-40s %-24s %r" % (s["name"], ",".join(s["fonts"]), s["value"][:40]))

    off = [s for s in result["strings"]
           if any(not (fn or "").startswith(preferred) for fn in s["fonts"])]
    print("\n== STRINGS NOT ON '%s*' (%d of %d)" % (preferred, len(off), len(result["strings"])))
    byfont = defaultdict(list)
    for s in off:
        byfont[",".join(s["fonts"])].append(s)
    for fn, ss in sorted(byfont.items(), key=lambda kv: -len(kv[1])):
        live = [s for s in ss if s["widget_refs"] or s["code_refs"]]
        print("  %-30s %3d strings (%d live)" % (fn, len(ss), len(live)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
