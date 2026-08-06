#!/usr/bin/env python3
"""Add a layer holding one empty root panel to an MGS design — the surface a
hand-written C builder attaches its widget tree to.

    add_layer.py <zip> <SUFFIX> --like <TEMPLATE_SUFFIX> [--dry-run]

A Legato "screen" spans N layers; each layer owns one root panel, and MGS emits that
panel as `<Screen>_PANEL_<NAME>`. Adding a layer-screen for a builder-owned view is
therefore: clone a sibling layer that is already builder-owned, give the layer and its
panel fresh uuids and names, and append. MGS derives `LE_LAYER_COUNT` from the layer
count, so the next Generate raises it for free.

`SUFFIX` is the part of the names that varies between siblings. With a template layer
`SCREEN_BUS` / panel `PANEL_BUS`, `add_layer.py z SYSTEM --like BUS` writes layer
`SCREEN_SYSTEM` / panel `PANEL_SYSTEM` — the prefixes are read off the template rather
than assumed, so a design using other conventions works unchanged.

Why clone instead of synthesizing: a layer carries ~25 properties (colorMode,
renderMode, clearMode, alpha, margins, editor state) whose enum ordinals are nowhere in
the zip. A sibling that the firmware already renders correctly is the only trustworthy
source for all of them at once.

The edit is a **text splice**, not a JSON round-trip: MGS serializes empty containers as
`[\\n]` rather than `[]`, so re-dumping a 96 KB screen.json rewrites every empty
container and buries the one block you added under hundreds of spurious hunks. Splicing
the cloned block keeps the git diff to exactly the layer being added.

The template's root panel must be empty. A layer whose panel still holds imported
widgets is not a builder-owned surface — run strip_subtree.py on it first, or point
--like at one that is.
"""
import argparse, json, os, re, sys, uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mgs_zip


def find_array_spans(text, key):
    """(start, end) offsets of each top-level element of the JSON array at "key".

    Scans rather than parses so the caller can slice the original bytes back out and
    keep MGS's exact formatting. String-aware, so a brace inside a value is ignored."""
    m = re.search(r'"%s"\s*:\s*\[' % re.escape(key), text)
    if m is None:
        raise KeyError("no array member %r" % key)
    i = m.end()
    spans, depth, start, instr, esc = [], 0, None, False, False
    while i < len(text):
        c = text[i]
        if instr:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c in "[{":
            if depth == 0:
                start = i
            depth += 1
        elif c in "]}":
            if depth == 0:          # the array's own closing bracket
                return spans
            depth -= 1
            if depth == 0:
                spans.append((start, i + 1))
        i += 1
    raise ValueError("unterminated array %r" % key)


def layer_name(layer):
    return layer["properties"]["name"]["value"]


def sole_panel(layer, where):
    kids = layer.get("children", [])
    if len(kids) != 1:
        sys.exit("%s has %d children; a layer-screen root is exactly one panel" % (where, len(kids)))
    panel = kids[0]
    if panel.get("children"):
        sys.exit("%s root panel %r still holds %d widget(s) — strip_subtree.py it first, "
                 "or pick a --like layer that is already builder-owned"
                 % (where, panel["properties"]["name"]["value"], len(panel["children"])))
    return panel


def common_prefixes(layer_nm, panel_nm, suffix):
    """Split the template's names into (prefix, suffix) so the clone can re-join them."""
    if not layer_nm.endswith(suffix) or not panel_nm.endswith(suffix):
        sys.exit("template names %r / %r do not both end in %r — pass --layer-name/--panel-name"
                 % (layer_nm, panel_nm, suffix))
    return layer_nm[: -len(suffix)], panel_nm[: -len(suffix)]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("zip")
    ap.add_argument("suffix", help="name suffix for the new layer/panel pair, e.g. SYSTEM")
    ap.add_argument("--like", required=True, metavar="SUFFIX",
                    help="suffix of the template layer to clone, e.g. BUS")
    ap.add_argument("--layer-name", help="override the derived layer name")
    ap.add_argument("--panel-name", help="override the derived panel name")
    ap.add_argument("--screen", help="screen member to edit (default: the only one)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    screens = mgs_zip.screen_members(a.zip)
    member = a.screen or (screens[0] if len(screens) == 1 else None)
    if member is None:
        sys.exit("multiple screens; pass --screen one of: %s" % ", ".join(screens))

    text = mgs_zip.read(a.zip, member).decode("utf-8")
    doc = json.loads(text)
    layers = doc["layers"]

    matches = [i for i, l in enumerate(layers) if layer_name(l).endswith(a.like)]
    if len(matches) != 1:
        sys.exit("--like %r matched %d layers (%s)"
                 % (a.like, len(matches), ", ".join(layer_name(l) for l in layers)))
    ti = matches[0]
    tmpl = layers[ti]
    tmpl_layer_nm = layer_name(tmpl)
    tmpl_panel = sole_panel(tmpl, tmpl_layer_nm)
    tmpl_panel_nm = tmpl_panel["properties"]["name"]["value"]

    lpre, ppre = common_prefixes(tmpl_layer_nm, tmpl_panel_nm, a.like)
    new_layer_nm = a.layer_name or (lpre + a.suffix)
    new_panel_nm = a.panel_name or (ppre + a.suffix)

    existing = {layer_name(l) for l in layers} | {
        k["properties"]["name"]["value"] for l in layers for k in l.get("children", [])}
    for nm in (new_layer_nm, new_panel_nm):
        if nm in existing:
            sys.exit("name %r already exists in this screen" % nm)

    # Fresh uuids, checked against the whole zip: a layer/panel uuid appears only in
    # screen.json today, but a collision anywhere would be silent and awful.
    blob = "".join(mgs_zip.read(a.zip, m).decode("utf-8", "ignore") for m in mgs_zip.members(a.zip)
                   if m.endswith(".json"))
    def fresh():
        while True:
            u = "{%s}" % uuid.uuid4()
            if u not in blob:
                return u
    new_layer_id, new_panel_id = fresh(), fresh()

    spans = find_array_spans(text, "layers")
    if len(spans) != len(layers):
        sys.exit("scanner found %d layer blocks but json has %d" % (len(spans), len(layers)))
    s, e = spans[ti]
    block = text[s:e]
    if json.loads(block) != tmpl:
        sys.exit("sliced template block does not round-trip to the parsed layer")

    subs = [(tmpl["properties"]["id"]["value"], new_layer_id),
            (tmpl_panel["properties"]["id"]["value"], new_panel_id),
            ('"value": "%s"' % tmpl_layer_nm, '"value": "%s"' % new_layer_nm),
            ('"value": "%s"' % tmpl_panel_nm, '"value": "%s"' % new_panel_nm)]
    new_block = block
    for old, new in subs:
        if new_block.count(old) != 1:
            sys.exit("expected exactly one %r in the template block, found %d"
                     % (old, new_block.count(old)))
        new_block = new_block.replace(old, new)

    last = spans[-1][1]
    out = text[:last] + ",\n" + new_block + text[last:]

    # The only intended change: layers grows by one, and the new entry is the template
    # with four fields swapped. Anything else is a scanner bug.
    new_doc = json.loads(out)
    if len(new_doc["layers"]) != len(layers) + 1:
        sys.exit("expected %d layers after splice, got %d" % (len(layers) + 1, len(new_doc["layers"])))
    if new_doc["layers"][:-1] != layers:
        sys.exit("splice disturbed an existing layer")
    added = new_doc["layers"][-1]
    want = json.loads(json.dumps(tmpl))
    want["properties"]["id"]["value"] = new_layer_id
    want["properties"]["name"]["value"] = new_layer_nm
    want["children"][0]["properties"]["id"]["value"] = new_panel_id
    want["children"][0]["properties"]["name"]["value"] = new_panel_nm
    if added != want:
        sys.exit("added layer is not the expected clone of %r" % tmpl_layer_nm)
    if {k: v for k, v in new_doc.items() if k != "layers"} != \
       {k: v for k, v in doc.items() if k != "layers"}:
        sys.exit("splice disturbed something outside layers[]")

    screen_nm = doc["properties"]["name"]["value"]
    print("clone %s -> %s   (layer %d of %d)" % (tmpl_layer_nm, new_layer_nm, len(layers), len(layers) + 1))
    print("  layer  %s  id %s" % (new_layer_nm, new_layer_id))
    print("  panel  %s  id %s   -> C global %s_%s"
          % (new_panel_nm, new_panel_id, screen_nm, new_panel_nm))
    print("  LE_LAYER_COUNT will become %d on Generate" % len(new_doc["layers"]))

    if a.dry_run:
        print("\ndry run — nothing written")
        return 0

    mgs_zip.repack(a.zip, {member: out})
    dangling = mgs_zip.validate_scheme_refs(a.zip)
    if dangling:
        sys.exit("FAIL: dangling scheme refs after repack: %s" % dangling)
    print("\nwrote %s — now: open MGS, Generate, and confirm %s_%s exists and "
          "LE_LAYER_COUNT is %d" % (a.zip, screen_nm, new_panel_nm, len(new_doc["layers"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
