#!/usr/bin/env python3
"""Add a new scheme to an MGS design zip by cloning an existing one.

    add_scheme.py <zip> <NEW_NAME> --like <SIBLING> [slot=#RRGGBB ...] [--uuid {...}] [--apply]

Dry-run by default: prints the clone's deltas and verifies, then does nothing.
Pass --apply to repack.

Clone rather than synthesize, for the same reason add_layer.py does: a scheme carries 16
colour slots plus `colorMode`, two 16-entry mono tables and the editor's category state —
~37 properties whose defaults appear nowhere else in the zip. A sibling the firmware
already renders correctly is the only trustworthy source for all of them at once, so pick
the sibling by ROLE (an accent for an accent, a fill for a fill) and recolour only the
slots that carry meaning.

Which slots to pass is the part worth thinking about, because it depends on how the widget
reads the scheme, not on what the colour "is":

  * A node-accent-style scheme, used for both text and small filled shapes, needs
    `base`, `foreground` AND `text` set to the same colour — a plain panel fills from
    `base` (leWidget_SkinClassic_DrawStandardBackground) while a label reads `text`. Set
    only `text` and the fills come out the sibling's colour; set only `base` and the
    caption does. This is why the stock `SCHEME_TEXT_*` schemes cannot serve as accents:
    they carry the colour in `text` alone and fill grey.
  * A scheme a BUTTON will use also needs `background`, which the classic button skin
    fills with while pressed. Legato's default is white — see SKILL.md.

Verification is the same contract as set_scheme_color.py: it splices the member text
rather than re-serializing (MGS's serializer is not Python's), then reparses and asserts
the new element differs from its sibling in exactly `id`, `name` and the requested slots,
and that every pre-existing scheme is byte-identical. A regex edit to a large JSON file
should never be trusted on the strength of "the regex matched".

The new uuid is derived from the name with uuid5 unless --uuid is given, so re-running the
same command is reproducible and two people adding the same scheme collide loudly (on the
name) rather than silently (on a random uuid).
"""
import json
import re
import struct
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mgs_zip  # noqa: E402

SLOTS = {
    "base", "highlight", "highlightLight", "shadow", "shadowDark",
    "foreground", "foregroundInactive", "foregroundDisabled",
    "background", "backgroundInactive", "backgroundDisabled",
    "text", "textHighlight", "textHighlightText", "textInactive", "textDisabled",
}

# Any fixed namespace works; it only has to be stable across runs.
NS = uuid.UUID("6f9619ff-8b86-d011-b42d-00c04fc964ff")


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def emit(x):
    """MGS writes an exact 1 as `1`; everything else at float32 precision."""
    return "1" if x == 1.0 else repr(f32(x))


def hexof(p):
    return "%02X%02X%02X" % tuple(round(p[k] * 255) for k in ("red", "green", "blue"))


def element_spans(text):
    """Yield (start, end) for each top-level element of the "schemes" array.

    Brace-matching from the array's own bracket, rather than searching outward from a
    name hit — a scheme element contains ~40 nested objects, so "the brace before the
    name" lands inside one of them.
    """
    i = text.index("[", text.index('"schemes"'))
    depth, start = 0, None
    for j in range(i, len(text)):
        c = text[j]
        if c == "{":
            if depth == 0:
                start = j
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                yield (start, j + 1)
        elif c == "]" and depth == 0:
            return


def recolour(text, slot, rgb):
    m = re.search(r'("%s": \{)(.*?)(\n\s*\})' % re.escape(slot), text, re.S)
    if not m:
        raise SystemExit(f"clone has no {slot!r} slot")
    body = m.group(2)
    for key, v in zip(("red", "green", "blue"), rgb):
        body, n = re.subn(rf'("{key}": )[-0-9.eE]+',
                          lambda mm, v=v: mm.group(1) + emit(v), body, count=1)
        if n != 1:
            raise SystemExit(f"{slot}: no {key} field to replace")
    return text[:m.start()] + m.group(1) + body + m.group(3) + text[m.end():]


def main(argv):
    apply_ = "--apply" in argv
    args = [a for a in argv[1:] if a != "--apply"]

    like = new_uuid = None
    for flag, attr in (("--like", "like"), ("--uuid", "uuid")):
        if flag in args:
            i = args.index(flag)
            val = args[i + 1]
            args = args[:i] + args[i + 2:]
            if attr == "like":
                like = val
            else:
                new_uuid = val
    if len(args) < 2 or like is None:
        raise SystemExit(__doc__)

    zip_path, new_name, edits = args[0], args[1], args[2:]

    slots = {}
    for a in edits:
        m = re.fullmatch(r"([A-Za-z]+)=#?([0-9A-Fa-f]{6})", a)
        if not m:
            raise SystemExit(f"bad slot edit {a!r}; want slot=#RRGGBB")
        if m.group(1) not in SLOTS:
            raise SystemExit(f"unknown slot {m.group(1)!r}; one of: {' '.join(sorted(SLOTS))}")
        slots[m.group(1)] = m.group(2).upper()

    if new_uuid is None:
        new_uuid = "{%s}" % uuid.uuid5(NS, new_name)

    text = mgs_zip.read(zip_path, "schemes.json").decode()
    before = json.loads(text)
    names = {s["properties"]["name"]["value"] for s in before["schemes"]}
    uuids = {s["properties"]["id"]["value"] for s in before["schemes"]}
    if new_name in names:
        raise SystemExit(f"{new_name} already exists")
    if like not in names:
        raise SystemExit(f"no scheme named {like} to clone")
    if new_uuid in uuids:
        raise SystemExit(f"uuid {new_uuid} already in use")

    spans = list(element_spans(text))
    if len(spans) != len(before["schemes"]):
        raise SystemExit("span count != parsed count — refusing to write")
    start, end = next((s, e) for s, e in spans
                      if json.loads(text[s:e])["properties"]["name"]["value"] == like)

    clone = text[start:end]
    clone = re.sub(r'("name": \{[^{}]*?"value": ")%s(")' % re.escape(like),
                   lambda m: m.group(1) + new_name + m.group(2), clone)
    clone = re.sub(r'("id": \{[^{}]*?"value": ")\{[0-9a-fA-F-]{36}\}(")',
                   lambda m: m.group(1) + new_uuid + m.group(2), clone)
    for slot, val in slots.items():
        clone = recolour(clone, slot, [int(val[k:k + 2], 16) / 255.0 for k in (0, 2, 4)])

    sep = re.match(r",\n\s*", text[end:])
    if not sep:
        # Cloning the last element would need the separator inserted before it instead.
        raise SystemExit(f"{like} is the last element; clone an earlier sibling")
    out = text[:end] + sep.group(0) + clone + text[end:]

    # ---- verify -------------------------------------------------------------
    after = json.loads(out)
    if len(after["schemes"]) != len(before["schemes"]) + 1:
        raise SystemExit("element count wrong — refusing to write")
    got_names = [s["properties"]["name"]["value"] for s in after["schemes"]]
    got_uuids = [s["properties"]["id"]["value"] for s in after["schemes"]]
    if len(set(got_names)) != len(got_names) or len(set(got_uuids)) != len(got_uuids):
        raise SystemExit("name/uuid collision — refusing to write")

    new = next(s for s in after["schemes"] if s["properties"]["name"]["value"] == new_name)
    old = next(s for s in before["schemes"] if s["properties"]["name"]["value"] == like)
    if new.keys() != old.keys() or new["properties"].keys() != old["properties"].keys():
        raise SystemExit("clone lost a property — refusing to write")

    delta = sorted(k for k in old["properties"]
                   if old["properties"][k] != new["properties"][k])
    expect = sorted({"id", "name"} | set(slots))
    if delta != expect:
        raise SystemExit(f"deltas {delta} != requested {expect} — refusing to write")

    rest = [s for s in after["schemes"] if s["properties"]["name"]["value"] != new_name]
    if json.dumps(rest, sort_keys=True) != json.dumps(before["schemes"], sort_keys=True):
        raise SystemExit("a pre-existing scheme changed — refusing to write")

    for slot, val in slots.items():
        got = hexof(new["properties"][slot])
        if got != val:
            raise SystemExit(f"{slot}: wrote #{got}, wanted #{val}")

    print(f"{new_name}  cloned from {like}")
    print(f"  uuid {new_uuid}")
    for slot in sorted(slots):
        print(f"  {slot:20} #{hexof(old['properties'][slot])} -> #{slots[slot]}")
    for slot in sorted(SLOTS - set(slots)):
        print(f"  {slot:20} #{hexof(old['properties'][slot])}   (inherited)")
    print(f"\nverified: {len(before['schemes'])} -> {len(after['schemes'])} schemes, "
          f"deltas exactly {delta}, all pre-existing schemes byte-identical")
    print(f"bytes {len(text)} -> {len(out)}")

    if apply_:
        bak = mgs_zip.repack(zip_path, {"schemes.json": out.encode()})
        print(f"repacked. backup: {bak or 'NONE (a .bak already existed — see SKILL.md rule 1)'}")
        print("Now: MGS -> Generate, then check le_gen_scheme.c for the new symbol.")
    else:
        print("dry run — pass --apply to write")


if __name__ == "__main__":
    main(sys.argv)
