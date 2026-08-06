#!/usr/bin/env python3
"""Retarget the nav drawer's root panel + unselected row scheme for the C-built drawer.

Companion to `strip_subtree.py <zip> PANEL_NAVIGATION`: with the figma subtree gone, the
design supplies only the drawer's backdrop and the two row schemes, and both carry figma
artifacts that the hand-built drawer would otherwise have to work around.

  1. PANEL_NAVIGATION becomes a plain opaque backdrop — SCHEME_FILL_ZINC_900 (the mockup's
     bg-zinc-900), background FILL, border NONE. It carried SCHEME_NAV_BUTTON_UNSELECTED
     (a row's fill, reused as the panel's) and a full BORDER_LINE box; the mockup's
     border-r is a single 1px rule, which the builder draws itself. The fill also has to
     be opaque because AaCorners_Render samples the backdrop pixel behind each rounded row.

  2. SCHEME_NAV_BUTTON_UNSELECTED's text goes #FFFFFF -> #D4D4D8 (zinc-300), so an
     inactive row's LABEL tracks its zinc-300 icon. The mockup styles the whole row
     text-zinc-300 -> text-white on select; marvin only ever moved the icon and the fill,
     because both nav schemes shipped white text. Safe: that scheme is referenced by the
     nav widgets only, and they are gone.

Idempotent, dry-run by default. `schemes.json` stores colors as float RGBA.

  python3 design_nav_panel_retarget.py <design.zip> [--apply]
"""
import json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                "../../../.claude/skills/mgs-legato-design/scripts"))
import mgs_zip

PANEL       = "PANEL_NAVIGATION"
LAYER       = "SCREEN_NAVIGATION"
PANEL_SCHEME = "SCHEME_FILL_ZINC_900"
ROW_SCHEME   = "SCHEME_NAV_BUTTON_UNSELECTED"
ZINC_300     = (212, 212, 216)

# `background` / `border` are combo properties holding the leWidget enum ordinal.
# Values read off the already-stripped root panels (PANEL_BUS, PANEL_WIIMOTES), which
# are exactly the state this panel is being moved to.
BACKGROUND_FILL = 1
BORDER_NONE     = 0


def pval(props, key, default=None):
    v = props.get(key)
    return v.get("value") if isinstance(v, dict) else default


def wname(node):
    return pval(node.get("properties", {}), "name")


def find(node, name):
    if wname(node) == name:
        return node
    for c in node.get("children") or []:
        hit = find(c, name)
        if hit is not None:
            return hit
    return None


def set_prop(props, key, value, log, label):
    """Set props[key]['value'], reporting the change. Property must already exist."""
    if key not in props:
        raise SystemExit("!! %s has no '%s' property" % (label, key))
    was = props[key].get("value")
    if was == value:
        return False
    log.append("   %-18s %r -> %r" % (key, was, value))
    props[key]["value"] = value
    return True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    zip_path = argv[1]
    apply_it = "--apply" in argv
    repl = {}

    # ---- 1. the root panel ----
    member = mgs_zip.screen_members(zip_path)[0]
    doc = mgs_zip.load_json(zip_path, member)
    schemes = mgs_zip.load_json(zip_path, "schemes.json")

    by_name = {pval(s.get("properties", {}), "name"): s for s in schemes["schemes"]}
    if PANEL_SCHEME not in by_name:
        print("!! scheme %s not found" % PANEL_SCHEME)
        return 1
    panel_scheme_id = pval(by_name[PANEL_SCHEME].get("properties", {}), "id")

    layer = next((l for l in doc["layers"] if wname(l) == LAYER), None)
    panel = find(layer, PANEL) if layer is not None else None
    if panel is None:
        print("!! %s not found on %s" % (PANEL, LAYER))
        return 1
    if panel.get("children"):
        print("!! %s still has %d children — run strip_subtree.py first"
              % (PANEL, len(panel["children"])))
        return 1

    log = []
    props = panel["properties"]
    set_prop(props, "scheme", panel_scheme_id, log, PANEL)
    set_prop(props, "background", BACKGROUND_FILL, log, PANEL)
    set_prop(props, "border", BORDER_NONE, log, PANEL)
    if log:
        print("=== %s ===" % PANEL)
        print("\n".join(log))
        print("   (scheme value is %s's uuid)" % PANEL_SCHEME)
        repl[member] = json.dumps(doc, indent=1)
    else:
        print("%s already retargeted" % PANEL)

    # ---- 2. the unselected row scheme's text colour ----
    row = by_name.get(ROW_SCHEME)
    if row is None:
        print("!! scheme %s not found" % ROW_SCHEME)
        return 1

    text = row["properties"]["text"]
    want = {"red": ZINC_300[0] / 255.0, "green": ZINC_300[1] / 255.0, "blue": ZINC_300[2] / 255.0}
    have = tuple(round(text[k] * 255) for k in ("red", "green", "blue"))
    if have != ZINC_300:
        print("\n=== %s.text ===" % ROW_SCHEME)
        print("   #%02X%02X%02X -> #%02X%02X%02X" % (have + ZINC_300))
        text.update(want)
        repl["schemes.json"] = json.dumps(schemes, indent=1)
    else:
        print("\n%s.text already zinc-300" % ROW_SCHEME)

    if not repl:
        print("\nnothing to do — already retargeted.")
        return 0

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, repl)
    print("\napplied.")
    print("Greg: open MGS -> Generate to refresh le_gen_* before building.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
