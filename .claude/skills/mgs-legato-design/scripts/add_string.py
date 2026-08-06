#!/usr/bin/env python3
"""Add a string to a design.zip's string table and bind it to a font.

MGS keeps strings in stringtable.json as two parallel lists: `strings[]` (identity +
one value per language) and `bindings[]` (one {string, language, font} per language).
BOTH are required — an unbound string generates a leTableString with no font, and it
is the binding that makes MGS auto-include the value's glyphs in that font on
Generate. So adding a string is the moment to get the font right, not later.

A string needs no widget reference to be generated: it emits `stringID_<NAME>` +
`string_<NAME>` regardless, which is what lets a hand-built screen own its captions
(leTableString_Constructor(&s, stringID_X)) while the design still owns the text and
its translations. That is the intended path for a stripped/hand-coded screen — see
SKILL.md "Replacing an imported screen with hand-written C".

  add_string.py <design.zip> <NAME> <value> --font <FontName> [--apply]
                [--lang <LanguageName>] [--path /group]

Multi-language designs: the value is written for --lang (default: the design's default
language) and bound there. Re-run per language to fill the rest.

Refuses a duplicate name (the generated C symbol would collide) and an unknown font.
Dry-run by default; open MGS -> Generate afterwards.
"""
import json, os, sys, uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip

FONTS = "assets/fonts/fonts.json"


def pval(props, key, default=None):
    v = props.get(key)
    return v.get("value") if isinstance(v, dict) else default


def prop(kind, value):
    return {"enabled": True, "type": kind, "value": value, "visible": True}


def arg(argv, flag, default=None):
    return argv[argv.index(flag) + 1] if flag in argv else default


def main(argv):
    if len(argv) < 4 or "--font" not in argv:
        print(__doc__)
        return 2

    zip_path, name, value = argv[1], argv[2], argv[3]
    font_name = arg(argv, "--font")
    lang_name = arg(argv, "--lang")
    path      = arg(argv, "--path", "/")
    apply_it  = "--apply" in argv

    st = mgs_zip.load_json(zip_path, "stringtable.json")

    # ---- font: name -> uuid ----
    fonts = {f.get("name"): f.get("id") for f in mgs_zip.load_json(zip_path, FONTS)["fonts"]}
    if font_name not in fonts:
        print("!! no font named %s. Available:" % font_name)
        for n in sorted(fonts):
            print("   %s" % n)
        return 1
    font_id = fonts[font_name]

    # ---- language: name -> uuid (default = the design's defaultLanguage) ----
    langs = {pval(l.get("properties", {}), "name"): pval(l.get("properties", {}), "id")
             for l in st["languages"]}
    if lang_name is None:
        lang_id = st["defaultLanguage"]
        lang_name = next((n for n, u in langs.items() if u == lang_id), "?")
    elif lang_name in langs:
        lang_id = langs[lang_name]
    else:
        print("!! no language named %s. Available: %s" % (lang_name, ", ".join(sorted(langs))))
        return 1

    # ---- refuse a duplicate name: the generated C symbol is the name ----
    existing = {pval(s.get("properties", {}), "name"): s for s in st["strings"]}
    if name in existing:
        cur = [v.get("value") for v in existing[name].get("values", [])]
        print("!! a string named %s already exists (values %r)." % (name, cur))
        print("   Pick another name, or edit that entry's value instead.")
        return 1

    string_id = "{%s}" % uuid.uuid4()

    print("=== adding string %s ===" % name)
    print("   uuid      %s" % string_id)
    print("   value     %r  (language %s)" % (value, lang_name))
    print("   font      %s  %s" % (font_name, font_id))
    print("   strings   %d -> %d" % (len(st["strings"]), len(st["strings"]) + 1))
    print("   bindings  %d -> %d" % (len(st["bindings"]), len(st["bindings"]) + 1))

    st["strings"].append({
        "properties": {
            "__categories": [],
            "__groups": [],
            "description": prop("text", ""),
            "id":          prop("uuid", string_id),
            "name":        prop("text", name),
            "path":        prop("text", path),
        },
        "values": [{"language": lang_id, "value": value}],
    })
    st["bindings"].append({"font": font_id, "language": lang_id, "string": string_id})

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, {"stringtable.json": json.dumps(st, indent=1)})
    print("\napplied. Open MGS -> Generate to emit stringID_%s." % name)
    print("Generate also adds this value's glyphs to %s, so non-ASCII text is covered." % font_name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
