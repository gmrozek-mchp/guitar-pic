#!/usr/bin/env python3
"""Walk an MGS / Legato design.zip screen tree and report, per widget, the string +
font it uses — plus duplicate-value string groups and a per-layer breakdown.

Complements audit_strings_fonts.py (which reports the asset side). This is the
*consumer* side: which widget on which layer pulls which string, so a cleanup can
tell "live label" from "leftover mockup asset".

Usage:
    audit_widget_strings.py <design.zip> [--dupes] [--layers] [--json]
"""
import json
import re
import sys
import zipfile
from collections import defaultdict

NULL_UUID = "{00000000-0000-0000-0000-000000000000}"


def prop(o, key, default=None):
    p = (o.get("properties") or {}).get(key)
    if isinstance(p, dict):
        return p.get("value", default)
    return default


def load(z, name):
    return json.loads(z.read(name))


def walk_widgets(node, layer, path, out):
    """Yield dicts for every widget node under `node` (a layer or widget dict)."""
    for ch in node.get("children") or []:
        p = ch.get("properties") or {}
        name = prop(ch, "name")
        wtype = ch.get("type")
        refs = {}
        for k, v in p.items():
            if isinstance(v, dict) and v.get("type") in ("string", "font", "image", "scheme"):
                if v.get("value") and v["value"] != NULL_UUID:
                    refs.setdefault(v["type"], []).append((k, v["value"]))
        out.append({
            "layer": layer, "path": path, "name": name, "type": wtype, "refs": refs,
        })
        walk_widgets(ch, layer, (path + "/" + (name or "?")), out)
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    zp = argv[1]
    z = zipfile.ZipFile(zp)

    st = load(z, "stringtable.json")
    strings = {}
    for s in st["strings"]:
        sid = prop(s, "id")
        strings[sid] = {
            "name": prop(s, "name"),
            "value": next((v.get("value", "") for v in s["values"]), ""),
        }
    fonts = {f["id"]: f["name"] for f in load(z, "assets/fonts/fonts.json")["fonts"]}
    bind = defaultdict(dict)
    for b in st["bindings"]:
        bind[b["string"]][b["language"]] = b["font"]
    for sid, s in strings.items():
        s["font"] = ",".join(fonts.get(f, "<missing>") for f in bind.get(sid, {}).values())

    scr_members = [n for n in z.namelist() if n.endswith("screen.json")]
    widgets = []
    layer_names = []
    for m in scr_members:
        d = load(z, m)
        for i, layer in enumerate(d.get("layers") or []):
            lname = prop(layer, "name") or ("layer%d" % i)
            layer_names.append(lname)
            walk_widgets(layer, lname, "", widgets)

    rows = []
    for w in widgets:
        for key, sid in w["refs"].get("string", []):
            s = strings.get(sid, {"name": "<missing>", "value": "", "font": ""})
            rows.append({
                "layer": w["layer"], "widget": w["name"], "wtype": w["type"],
                "prop": key, "string": s["name"], "value": s["value"], "font": s["font"],
                "sid": sid,
            })

    if "--json" in argv:
        print(json.dumps({"layers": layer_names, "rows": rows}, indent=1))
        return 0

    print("== %s" % zp)
    print("   %d layers: %s" % (len(layer_names), ", ".join(layer_names)))
    print("   %d widgets, %d string-bearing widget props" % (len(widgets), len(rows)))

    if "--layers" in argv or True:
        bylayer = defaultdict(list)
        for r in rows:
            bylayer[r["layer"]].append(r)
        for ln in layer_names:
            rs = bylayer.get(ln, [])
            print("\n-- layer %s (%d string refs)" % (ln, len(rs)))
            for r in rs:
                print("   %-46s %-14s %-30s %-22s %r"
                      % (r["widget"], r["wtype"], r["string"], r["font"], r["value"][:28]))

    if "--dupes" in argv:
        byval = defaultdict(list)
        for sid, s in strings.items():
            byval[s["value"]].append((s["name"], s["font"], sid))
        used = {r["sid"] for r in rows}
        print("\n== DUPLICATE STRING VALUES (same text, >1 asset)")
        for v, group in sorted(byval.items(), key=lambda kv: -len(kv[1])):
            if len(group) < 2:
                continue
            print("  %r  x%d" % (v[:40], len(group)))
            for name, font, sid in sorted(group):
                print("     %-40s %-26s %s" % (name, font, "USED" if sid in used else "unused"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
