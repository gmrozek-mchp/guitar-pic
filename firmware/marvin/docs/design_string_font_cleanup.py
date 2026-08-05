#!/usr/bin/env python3
"""One-off transform: clean up strings + fonts in marvin's MGS design.zip.

Four stages, applied together because they interlock (pruning strings frees fonts;
freeing the `GUITAR_*` names lets the live labels take them):

  A  retarget every non-DejaVu font binding -> DejaVu Sans Mono at the same point size
  B  delete strings referenced by no widget (all 127)
  C  delete fonts left with no binding and no hand-source symbol reference
  D  rename the live `figmaStr_*` leftovers to semantic names

Dry-run by default; pass --apply to rewrite the zip (backed up to *.zip.bak).
Afterwards: open MGS -> Generate -> build. Re-run
`.claude/skills/mgs-legato-design/scripts/audit_strings_fonts.py` to verify.

See firmware/marvin/docs/journal.md (2026-08-05) for the audit this is based on.
"""
import json
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SKILL = os.path.join(HERE, "../../../.claude/skills/mgs-legato-design/scripts")
sys.path.insert(0, os.path.abspath(SKILL))
import mgs_zip  # noqa: E402

ZIP = os.path.abspath(os.path.join(HERE, "../default/src/config/default/default_design.zip"))
HAND_SRC = os.path.abspath(os.path.join(HERE, "../default/src"))

# --- stage A: font retarget. Every source font here is really Noto Sans Regular
# (proportional) under a name claiming otherwise; the target is the DejaVu Sans Mono
# of the same point size. 13px -> 12 because DejaVu 14 would overflow TILT's 28px rect.
RETARGET = {
    "figmaFont_Menlo_12":   "DejaVuSansMono_12",
    "figmaFont_Menlo_12_1": "DejaVuSansMono_12",
    "figmaFont_Menlo_12_2": "DejaVuSansMono_12",
    "figmaFont_Menlo_14":   "DejaVuSansMono_14",
    "figmaFont_Menlo_14_2": "DejaVuSansMono_14",
    "figmaFont_Menlo_18_2": "DejaVuSansMono_18",
    "figmaFont_Menlo_24_2": "DejaVuSansMono_24",
    "figmaFont_Cousine_13": "DejaVuSansMono_12",
    "figmaFont_Inter_13":   "DejaVuSansMono_12",
}

# --- stage D: rename live figmaStr_* -> semantic. Names derived from the widget and its
# parent panel (see the journal entry). The four GUITAR_* names are freed by stage B.
RENAME = {
    # wiimotes / guitar-extension card
    "figmaStr_GUITAR_EXTENSION": "GUITAR_EXTENSION",
    "figmaStr__UP":              "GUITAR_STRUM_UP",
    "figmaStr__DN":              "GUITAR_STRUM_DOWN",
    "figmaStr_WHAMMY":           "GUITAR_WHAMMY",
    "figmaStr___3":              "GUITAR_PLUS",
    "figmaStr___1":              "GUITAR_MINUS",
    "figmaStr_ENABLED":          "WIIMOTES_ENABLED",
    # wiimotes / wiimote card
    "figmaStr_WIIMOTE":          "WIIMOTE_HEADING",
    "figmaStr___1_0":            "WIIMOTE_DPAD_UP",
    "figmaStr___2_0":            "WIIMOTE_DPAD_LEFT",
    "figmaStr___3_0":            "WIIMOTE_DPAD_RIGHT",
    "figmaStr___4":              "WIIMOTE_DPAD_DOWN",
    "figmaStr_HOME":             "WIIMOTE_HOME",
    "figmaStr_A":                "WIIMOTE_A",
    "figmaStr_B_0":              "WIIMOTE_B",
    "figmaStr_1":                "WIIMOTE_ONE",
    "figmaStr_2":                "WIIMOTE_TWO",
    "figmaStr_TILT":             "WIIMOTE_TILT",
    # dashboard / robot + shared player values
    "figmaStr__0_0":             "PLAYER_Streak",
    "figmaStr__":                "PLAYER_Accuracy",
    "figmaStr___0":              "PLAYER_StarPower",
    "figmaStr_IDLE_0":           "PLAYER_STRUM_BAR_Status",
    # dashboard / human controller card
    "figmaStr_CONTROLLER":       "PLAYER_HUMAN_CONTROLLER",
    "figmaStr_Wii_guitar":       "PLAYER_HUMAN_Wii_guitar",
    "figmaStr_Connected_0":      "PLAYER_HUMAN_GuitarStatus",
    "figmaStr_Wii_remote":       "PLAYER_HUMAN_Wii_remote",
    "figmaStr_Connected_0_0":    "PLAYER_HUMAN_RemoteStatus",
    "figmaStr_Battery":          "PLAYER_HUMAN_Battery",
    "figmaStr_68_":              "PLAYER_HUMAN_BatteryLevel",
    # dashboard / video
    "figmaStr_NO_SIGNAL_0":      "VIDEO_NO_SIGNAL",
    # nav drawer footer
    "figmaStr_STATUS":           "NAV_STATUS",
    "figmaStr_Connected":        "NAV_ConnectionStatus",
}


def prop(o, k):
    p = (o.get("properties") or {}).get(k)
    return p.get("value") if isinstance(p, dict) else None


def hand_src_font_refs(fonts):
    """outputName -> True for fonts whose C symbol appears in hand source."""
    names = set(fonts.values())
    hit = set()
    for dp, dn, fn in os.walk(HAND_SRC):
        if "config/default" in dp.replace(os.sep, "/"):
            dn[:] = []
            continue
        for f in fn:
            if not f.endswith((".c", ".h")):
                continue
            text = open(os.path.join(dp, f), errors="ignore").read()
            for n in names:
                if n not in hit and re.search(r"\b%s\b" % re.escape(n), text):
                    hit.add(n)
    return hit


def main(argv):
    apply = "--apply" in argv
    z = zipfile.ZipFile(ZIP)
    st = json.loads(z.read("stringtable.json"))
    fj = json.loads(z.read("assets/fonts/fonts.json"))
    fonts = {f["id"]: f["name"] for f in fj["fonts"]}
    fid_by_name = {v: k for k, v in fonts.items()}
    screen = [n for n in z.namelist() if n.endswith("screen.json")][0]
    stext = z.read(screen).decode()
    z.close()

    live = set(re.findall(r'"type":\s*"string",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"', stext))
    live.discard(mgs_zip.NULL_UUID)

    by_name = {prop(s, "name"): prop(s, "id") for s in st["strings"]}
    name_of = {v: k for k, v in by_name.items()}

    # ---- stage B: prune dead strings
    dead = {prop(s, "id") for s in st["strings"] if prop(s, "id") not in live}
    st["strings"] = [s for s in st["strings"] if prop(s, "id") in live]
    st["bindings"] = [b for b in st["bindings"] if b["string"] in live]
    print("B  deleted %d unused strings (%d remain)" % (len(dead), len(st["strings"])))

    # ---- stage A: retarget bindings
    moved = 0
    per = {}
    for b in st["bindings"]:
        old = fonts.get(b["font"])
        if old in RETARGET:
            new = RETARGET[old]
            b["font"] = fid_by_name[new]
            moved += 1
            per["%s -> %s" % (old, new)] = per.get("%s -> %s" % (old, new), 0) + 1
    print("A  retargeted %d bindings onto DejaVu Sans Mono" % moved)
    for k, v in sorted(per.items(), key=lambda kv: -kv[1]):
        print("     %-46s %d" % (k, v))

    # ---- stage D: rename live figmaStr_*
    renamed = 0
    for s in st["strings"]:
        n = prop(s, "name")
        if n in RENAME:
            s["properties"]["name"]["value"] = RENAME[n]
            renamed += 1
    print("D  renamed %d strings" % renamed)
    left = [prop(s, "name") for s in st["strings"] if (prop(s, "name") or "").startswith("figmaStr")]
    if left:
        print("   !! still figmaStr_*: %s" % left)

    # ---- stage C: drop fonts with no binding and no hand-source reference
    bound = {b["font"] for b in st["bindings"]}
    code = hand_src_font_refs(fonts)
    keep = {fid for fid in fonts if fid in bound or fonts[fid] in code}
    gone = {fid: fonts[fid] for fid in fonts if fid not in keep}
    fj["fonts"] = [f for f in fj["fonts"] if f["id"] in keep]
    print("C  deleted %d fonts (%d remain: %s)"
          % (len(gone), len(fj["fonts"]), ", ".join(sorted(fonts[f] for f in keep))))
    for fid, n in sorted(gone.items(), key=lambda kv: kv[1]):
        print("     %s" % n)

    # ---- validate
    ids = {prop(s, "id") for s in st["strings"]}
    names = [prop(s, "name") for s in st["strings"]]
    errs = []
    errs += ["dangling widget string uuid %s" % u for u in sorted(live - ids)]
    errs += ["duplicate string name %r" % n for n in sorted({n for n in names if names.count(n) > 1})]
    errs += ["binding -> deleted font %s" % fonts[b["font"]]
             for b in st["bindings"] if b["font"] not in keep]
    errs += ["binding -> unknown string %s" % b["string"]
             for b in st["bindings"] if b["string"] not in ids]
    errs += ["code-referenced font deleted: %s" % n for n in sorted(code & set(gone.values()))]
    errs += ["string with no font binding: %s" % name_of.get(u, u)
             for u in sorted(ids - {b["string"] for b in st["bindings"]})]
    non_dejavu = sorted({fonts[b["font"]] for b in st["bindings"]
                         if not fonts[b["font"]].startswith("DejaVu")})
    errs += ["binding still on non-DejaVu font: %s" % n for n in non_dejavu]
    if errs:
        print("\nVALIDATION FAILED:")
        for e in errs:
            print("  ! %s" % e)
        return 1
    print("\nvalidation OK — %d strings, %d fonts, all bindings DejaVu Sans Mono"
          % (len(st["strings"]), len(fj["fonts"])))

    if not apply:
        print("\n(dry run — pass --apply to write)")
        return 0
    mgs_zip.repack(ZIP,
                   {"stringtable.json": json.dumps(st, indent=1, ensure_ascii=False),
                    "assets/fonts/fonts.json": json.dumps(fj, indent=1, ensure_ascii=False)},
                   drop=["assets/fonts/%s/" % fid for fid in gone])
    print("\nwrote %s (backup at %s.bak)" % (ZIP, ZIP))
    print("next: open MGS -> Generate -> build")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
