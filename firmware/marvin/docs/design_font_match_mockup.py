#!/usr/bin/env python3
"""Align marvin's MGS string->font bindings with the Figma mockup's typography.

The mockup is a single-typeface design (Tailwind `font-mono`, no @font-face), so the only
things that can deviate are **size** and **weight**. Each entry below was read off the
mockup element that the marvin widget came from — see docs/mockup_typography.py, which
resolves the mockup's Tailwind classes (with inheritance) to px + bold.

DejaVu Sans Mono Bold has the *same advance* as Regular at every size, so the weight-only
changes are width-neutral: no label can start overflowing.

Dry-run by default; --apply rewrites the zip (backup at *.zip.bak). Then MGS -> Generate.
"""
import json
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(
    os.path.join(HERE, "../../../.claude/skills/mgs-legato-design/scripts")))
import mgs_zip  # noqa: E402

ZIP = os.path.abspath(os.path.join(HERE, "../default/src/config/default/default_design.zip"))

# marvin string name -> target font, with the mockup element it was read from.
# "weight" = same size, regular -> bold (width-neutral).
RETARGET = {
    # --- dashboard: robot + human player cards (PlayerPerformance.tsx) ---
    "PLAYER_ROBOT_Name":            ("DejaVuSansMonoBold_14", "PlayerPerformance:60  14/bold"),
    "PLAYER_HUMAN_Name":            ("DejaVuSansMonoBold_14", "PlayerPerformance:236 14/bold"),
    "PLAYER_ROBOT_Status":          ("DejaVuSansMonoBold_12", "PlayerPerformance:66  12/bold"),
    "PLAYER_MULTIPLIER_1x":         ("DejaVuSansMonoBold_12", "PlayerPerformance:81  12/bold"),
    "PLAYER_MULTIPLIER_2x":         ("DejaVuSansMonoBold_12", "PlayerPerformance:81  12/bold"),
    "PLAYER_MULTIPLIER_3x":         ("DejaVuSansMonoBold_12", "PlayerPerformance:81  12/bold"),
    "PLAYER_MULTIPLIER_4x":         ("DejaVuSansMonoBold_12", "PlayerPerformance:81  12/bold"),
    "PLAYER_ROBOT_Score":           ("DejaVuSansMonoBold_24", "PlayerPerformance:85  24/bold"),
    "PLAYER_STRUM_BAR_Status":      ("DejaVuSansMonoBold_12", "PlayerPerformance:145 12/bold"),
    "PLAYER_ROBOT_Neural_Network":  ("DejaVuSansMonoBold_12", "PlayerPerformance:166 12/bold"),
    "PLAYER_ROBOT_Computer_Vision": ("DejaVuSansMonoBold_12", "PlayerPerformance:166 12/bold"),
    # --- dashboard: song / gameplay card (NowPlaying.tsx) ---
    "SONG_INFO_Title":              ("DejaVuSansMonoBold_20", "NowPlaying:68  20/bold"),
    "SONG_GAMEPLAY_EASY":           ("DejaVuSansMonoBold_12", "NowPlaying:109 12/bold"),
    "GAMEPLAY_SELECT_SONG":         ("DejaVuSansMonoBold_12", "NowPlaying:123 12/bold"),
    "GAMEPLAY_START":               ("DejaVuSansMonoBold_12", "NowPlaying:138 12/bold"),
    # --- song-select dialog: MODE buttons are 12/bold, not 14 (DIFFICULTY is 14/bold) ---
    "SONG_SELECT_1P_ROBOT":         ("DejaVuSansMonoBold_12", "SongSelectModal:164 12/bold"),
    "SONG_SELECT_1P_HUMAN":         ("DejaVuSansMonoBold_12", "SongSelectModal:164 12/bold"),
    "SONG_SELECT_2P_ROBOT_vs_HUMAN": ("DejaVuSansMonoBold_12", "SongSelectModal:164 12/bold"),
    # --- wiimotes / manual override (ManualOverrideScreen.tsx) ---
    "WIIMOTES_ENABLED":             ("DejaVuSansMonoBold_12", "ManualOverride:230 12/bold"),
    "GUITAR_STRUM_UP":              ("DejaVuSansMonoBold_14", "ManualOverride:269 14/bold"),
    "GUITAR_STRUM_DOWN":            ("DejaVuSansMonoBold_14", "ManualOverride:279 14/bold"),
    "GUITAR_WHAMMY":                ("DejaVuSansMonoBold_12", "ManualOverride:285 13/bold -> 12"),
    "WIIMOTE_TILT":                 ("DejaVuSansMonoBold_12", "ManualOverride:101 13/bold -> 12"),
    # the +/- pair is one size in the mockup; marvin had minus at 12 and plus at 24
    "GUITAR_MINUS":                 ("DejaVuSansMono_24",     "ManualOverride:296 24/reg"),
}

# Character fixes — the mockup's glyph, where marvin's differs.
# U+2212 MINUS SIGN is drawn to pair with U+002B PLUS: identical 12.4px ink width and
# identical +7.5px bar height at 24px. The em dash marvin carried is 14.4px wide (16%
# wider than the plus) and sits 0.9px lower, which reads as mismatched next to it.
VALUE_FIX = {
    "GUITAR_MINUS": ("−", "ManualOverride:296 uses U+2212 MINUS, not U+2014 EM DASH"),
}


def prop(o, k):
    p = (o.get("properties") or {}).get(k)
    return p.get("value") if isinstance(p, dict) else None


def main(argv):
    apply = "--apply" in argv
    z = zipfile.ZipFile(ZIP)
    fj = json.loads(z.read("assets/fonts/fonts.json"))
    FN = {f["id"]: f["name"] for f in fj["fonts"]}
    ID = {v: k for k, v in FN.items()}
    st = json.loads(z.read("stringtable.json"))
    screen = [n for n in z.namelist() if n.endswith("screen.json")][0]
    stext = z.read(screen).decode()
    glyphs = {f["name"]: json.loads(z.read("assets/fonts/%s/glyphs.json" % f["id"]))
              for f in fj["fonts"]}
    z.close()

    S = {prop(s, "id"): (prop(s, "name"),
                         next((v.get("value", "") for v in s["values"]), ""))
         for s in st["strings"]}
    by_name = {n: sid for sid, (n, _) in S.items()}

    # widget rect widths per string, for the two size changes
    widths = {}
    d = json.loads(stext)

    def walk(node):
        for ch in node.get("children") or []:
            p = ch.get("properties") or {}
            sid = (p.get("string") or {}).get("value")
            if sid:
                widths.setdefault(sid, []).append(
                    ((p.get("name") or {}).get("value"), (p.get("width") or {}).get("value")))
            walk(ch)
    for layer in d["layers"]:
        walk(layer)

    def text_w(font, s):
        g = glyphs[font]
        fb = g.get("M", {}).get("advance", 0)
        return sum(g.get(c, {}).get("advance", fb) for c in s)

    changed, errs = 0, []
    print("%-30s %-24s -> %-24s %s" % ("string", "from", "to", "mockup source"))
    for name, (target, why) in sorted(RETARGET.items()):
        sid = by_name.get(name)
        if sid is None:
            errs.append("no such string: %s" % name)
            continue
        if target not in ID:
            errs.append("font not in design: %s (needed by %s)" % (target, name))
            continue
        cur = next((b for b in st["bindings"] if b["string"] == sid), None)
        if cur is None:
            errs.append("no binding for %s" % name)
            continue
        old = FN[cur["font"]]
        if old == target:
            continue
        val = S[sid][1]
        # width check — only meaningful when the size changes (bold is width-neutral)
        note = ""
        if re.sub(r"Bold", "", old) != re.sub(r"Bold", "", target):
            for wname, w in widths.get(sid, []):
                nw = text_w(target, val)
                if w is not None and nw > w:
                    errs.append("%s would overflow %s (%dpx text in %dpx)" % (name, wname, nw, w))
                else:
                    note = "  [%s: %dpx text in %dpx rect]" % (wname, nw, w or 0)
        else:
            note = "  [width-neutral]"
        print("%-30s %-24s -> %-24s %s%s" % (name, old, target, why, note))
        cur["font"] = ID[target]
        changed += 1

    print("\n%d bindings changed" % changed)

    vfix = 0
    for s in st["strings"]:
        name = prop(s, "name")
        if name in VALUE_FIX:
            want, why = VALUE_FIX[name]
            for v in s["values"]:
                if v.get("value") != want:
                    print("value  %-24s %r -> %r   %s" % (name, v["value"], want, why))
                    v["value"] = want
                    vfix += 1
    if vfix:
        print("%d string values changed" % vfix)

    if errs:
        print("\nPROBLEMS:")
        for e in errs:
            print("  ! %s" % e)
        return 1

    # glyph-coverage warning for runtime-set labels moving onto a thinner font
    z = zipfile.ZipFile(ZIP)
    for name in ("SONG_INFO_Title",):
        sid = by_name.get(name)
        tgt = FN[next(b for b in st["bindings"] if b["string"] == sid)["font"]]
        cps = {v["codePoint"] for v in glyphs[tgt].values()}
        if not (cps & set(range(160, 256))):
            print("\n!! %s is runtime-set from the song catalog and now binds to %s,"
                  "\n   which has NO Latin-1 range. Catalog text includes 'Mauvais Garçon'"
                  "\n   (U+00E7). MGS will NOT auto-add it — that glyph only appears in runtime"
                  "\n   data, not in a design string. ACTION: in MGS, add range 160-255 to %s."
                  % (name, tgt, tgt))
    z.close()

    if not apply:
        print("\n(dry run — pass --apply to write)")
        return 0
    mgs_zip.repack(ZIP, {"stringtable.json": json.dumps(st, indent=1, ensure_ascii=False)})
    print("\nwrote %s (backup at %s.bak)\nnext: MGS -> Generate -> build" % (ZIP, ZIP))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
