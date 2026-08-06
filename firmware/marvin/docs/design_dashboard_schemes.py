#!/usr/bin/env python3
"""Recolour the song-tier ramp and add the dashboard's toggling-fill schemes.

Two independent edits to schemes.json, both for the C-built dashboard:

1. TIERS — SCHEME_TEXT_TIER_1..8 become two-shade difficulty-band pairs (green /
   amber / orange / red, lighter shade = lower tier of the pair), so the ramp matches
   the EASY/MEDIUM/HARD/EXPERT palette the difficulty pills already use. The previous
   ramp had two defects: T7 #F87171 and T8 #EF4444 were both red (the pair most needing
   separation, dE 21), and T4 violet -> T5 yellow reversed direction mid-ramp. The new
   pairs keep every adjacent tier at dE >= 27 with luminance stepping down band over
   band. Only the colours change — song_detail.c's tier -> scheme mapping is untouched.

2. FILLS — four schemes whose BASE is the idle/track colour and BACKGROUND the active
   fill, which is the pair a toggling widget needs (a button's pressed state and
   ProgressBarAA's fill both read BACKGROUND). The robot's multiplier pills already had
   this shape in SCHEME_PILL_ZINC_800 (zinc-800 + cyan); the human pills were on
   SCHEME_FILL_YELLOW_400, which is yellow in BOTH states, and the playtime bar was on
   SCHEME_FILL_ZINC_800, whose BACKGROUND is white — so it filled white, not green.

Idempotent, dry-run by default. schemes.json stores colours as float RGBA.

  python3 design_dashboard_schemes.py <design.zip> [--apply]
"""
import copy, json, os, sys, uuid

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                "../../../.claude/skills/mgs-legato-design/scripts"))
import mgs_zip

# tier -> hex. Lighter member of each pair is the lower tier.
TIERS = {
    1: "86EFAC", 2: "22C55E",     # green-300  / green-500   (EASY band)
    3: "FCD34D", 4: "F59E0B",     # amber-300  / amber-500   (MEDIUM band)
    5: "FDBA74", 6: "F97316",     # orange-300 / orange-500  (HARD band)
    7: "FCA5A5", 8: "EF4444",     # red-300    / red-500     (EXPERT band)
}

# Test-pattern bars at the SMPTE 75% level the mockup draws (191 = 0xBF), not full
# intensity. These are bar colours only; the two things in that card that must stay
# bright get their own schemes below.
BARS = {
    "SCHEME_TEST_PATTERN_WHITE":   "BFBFBF",
    "SCHEME_TEST_PATTERN_YELLOW":  "BFBF00",
    "SCHEME_TEST_PATTERN_CYAN":    "00BFBF",
    "SCHEME_TEST_PATTERN_GREEN":   "00BF00",
    "SCHEME_TEST_PATTERN_MAGENTA": "BF00BF",
    "SCHEME_TEST_PATTERN_RED":     "BF0000",
    "SCHEME_TEST_PATTERN_BLUE":    "0000BF",
}

# name -> (base = idle/track, background = active fill, text)
FILLS = {
    "SCHEME_PILL_HUMAN":    ("27272A", "FDC700", "000000"),   # human multiplier pills

    # Test-pattern extras, all from the mockup's canvas: the middle row's separators are
    # #131313, not black; the centre crosshair stays full white; the NO SIGNAL dot is
    # red-500 (it was borrowing the test-pattern red, which is now dimmed).
    "SCHEME_TEST_PATTERN_DARK": ("131313", "131313", "FFFFFF"),
    "SCHEME_FILL_WHITE":        ("FFFFFF", "FFFFFF", "000000"),
    "SCHEME_FILL_RED_500":      ("EF4444", "EF4444", "FFFFFF"),

    # Selected / unselected look for the robot card's option rows (detector choice,
    # actuator state). Text colour differs per state, and a scheme carries only one
    # text slot, so these are a PAIR the code swaps between — the same idiom the nav
    # drawer uses for its rows. cyan-900 fill + cyan-300 text when on.
    "SCHEME_TOGGLE_ON":     ("164E63", "164E63", "67E8F9"),
    "SCHEME_TOGGLE_OFF":    ("27272A", "27272A", "71717B"),
}

# Added earlier in this session, then made redundant: the bars are now ui/widgets/bar on
# a plain leWidget, which takes its fill colours as arguments (a gradient needs two, and
# a scheme has one BACKGROUND slot) and its track from SCHEME_FILL_ZINC_800. Removed
# rather than left behind, so the design doesn't accumulate schemes nothing references.
DEAD = ("SCHEME_BAR_PLAYTIME", "SCHEME_BAR_ROBOT", "SCHEME_BAR_HUMAN")


def rgb(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def as_hex(c):
    return "%02X%02X%02X" % tuple(round(c[k] * 255) for k in ("red", "green", "blue"))


def set_color(props, key, h):
    """Set one colour field, preserving alpha/type/visible. Returns the old hex or None."""
    was = as_hex(props[key])
    if was == h.upper():
        return None
    r, g, b = rgb(h)
    props[key].update({"red": r, "green": g, "blue": b})
    return was


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    zip_path = argv[1]
    apply_it = "--apply" in argv

    doc = mgs_zip.load_json(zip_path, "schemes.json")
    by_name = {s["properties"]["name"]["value"]: s for s in doc["schemes"]}
    changed = False

    # ---- 1. tier ramp ----
    print("=== tier ramp ===")
    for tier, h in sorted(TIERS.items()):
        name = "SCHEME_TEXT_TIER_%d" % tier
        s = by_name.get(name)
        if s is None:
            print("!! %s not found" % name)
            return 1
        was = set_color(s["properties"], "text", h)
        if was is None:
            print("   T%d %-22s already #%s" % (tier, name, h))
        else:
            print("   T%d %-22s #%s -> #%s" % (tier, name, was, h))
            changed = True

    # ---- 1b. test-pattern bars to the mockup's 75% level ----
    print("\n=== test-pattern bars (SMPTE 75 pct) ===")
    for name, hexv in sorted(BARS.items()):
        s = by_name.get(name)
        if s is None:
            print("!! %s not found" % name)
            return 1
        was = set_color(s["properties"], "base", hexv)
        if was is None:
            print("     %-30s already #%s" % (name, hexv))
        else:
            print("   %-30s #%s -> #%s" % (name, was, hexv))
            changed = True

    # ---- 2. toggling fills ----
    # Clone a scheme of the same shape rather than synthesizing one, so every field
    # this MGS version expects is present (same rule as add_image.py's --like).
    template = by_name.get("SCHEME_PILL_ZINC_800")
    if template is None:
        print("!! template scheme SCHEME_PILL_ZINC_800 not found")
        return 1

    print("\n=== toggling fills ===")
    for name, (base, background, text) in sorted(FILLS.items()):
        s = by_name.get(name)
        if s is None:
            s = copy.deepcopy(template)
            s["properties"]["id"]["value"] = "{%s}" % uuid.uuid4()
            s["properties"]["name"]["value"] = name
            doc["schemes"].append(s)
            by_name[name] = s
            print("   + %-22s (cloned from SCHEME_PILL_ZINC_800)" % name)
            changed = True
        else:
            print("     %-22s exists" % name)

        for key, h in (("base", base), ("background", background), ("text", text)):
            was = set_color(s["properties"], key, h)
            if was is not None:
                print("       %-11s #%s -> #%s" % (key, was, h))
                changed = True

    # ---- 3. remove the schemes this session added and then stopped needing ----
    print("\n=== dead schemes ===")
    for name in DEAD:
        s = by_name.get(name)
        if s is None:
            print("     %-22s already gone" % name)
            continue
        uuid_ = s["properties"]["id"]["value"]
        if uuid_ in mgs_zip.scheme_uuids_used(zip_path):
            print("!! %s is referenced by a widget — refusing to delete" % name)
            return 1
        doc["schemes"] = [x for x in doc["schemes"] if x is not s]
        del by_name[name]
        print("   - %-22s removed" % name)
        changed = True

    if not changed:
        print("\nnothing to do — already applied.")
        return 0

    # Uniqueness is what the generated C symbols depend on.
    names = [s["properties"]["name"]["value"] for s in doc["schemes"]]
    ids = [s["properties"]["id"]["value"] for s in doc["schemes"]]
    if len(set(names)) != len(names) or len(set(ids)) != len(ids):
        print("!! duplicate scheme name or id after edit — refusing")
        return 1
    print("\n%d schemes, names + ids unique" % len(doc["schemes"]))

    if not apply_it:
        print("\n[dry run] re-run with --apply to write the zip")
        return 0

    mgs_zip.repack(zip_path, {"schemes.json": json.dumps(doc, indent=1)})
    print("\napplied.")
    print("Greg: open MGS -> Generate to refresh le_gen_* before building.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
