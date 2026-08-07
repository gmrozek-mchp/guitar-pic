#!/usr/bin/env python3
"""Set colour slots on existing schemes in an MGS design zip.

    set_scheme_color.py <zip> SCHEME_NAME:slot=#RRGGBB [...] [--apply]

`slot` is any of the 16 Legato scheme colours, in the design's own camelCase spelling:
base, highlight, highlightLight, shadow, shadowDark, foreground, foregroundInactive,
foregroundDisabled, background, backgroundInactive, backgroundDisabled, text,
textHighlight, textHighlightText, textInactive, textDisabled.

Dry-run by default: prints before -> after per slot and verifies, then does nothing.
Pass --apply to repack (mgs_zip.repack writes a .bak first).

Two things it does deliberately:

  * It SPLICES the original member text instead of re-serializing the JSON. MGS's
    serializer is not Python's — no indent/separators/sort_keys combination reproduces
    it — so a json.dumps round-trip rewrites the whole 800 KB member and buries the
    change in noise. Splicing keeps every byte outside the edited numbers identical,
    which is also what makes the verification below meaningful.
  * It re-parses the result and asserts that the set of differing fields is exactly the
    set requested. A regex edit to a large JSON file should never be trusted on the
    strength of "the regex matched".

Slots MGS wrote as an exact 1 are emitted as `1`, not `1.0`, matching its own output;
other values are float32-rounded, which is the precision MGS stores.

Worth knowing which slot you want: Legato's classic BUTTON skin fills with `background`
while pressed and `base` while up, whereas a plain panel/widget only ever uses `base`
(legato_widget_skin_classic.c). So a FILL scheme left at Legato's default white
`background` makes every button on it flash white on touch while panels look fine —
see REFERENCE.md.
"""
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mgs_zip  # noqa: E402

SLOTS = {
    "base", "highlight", "highlightLight", "shadow", "shadowDark",
    "foreground", "foregroundInactive", "foregroundDisabled",
    "background", "backgroundInactive", "backgroundDisabled",
    "text", "textHighlight", "textHighlightText", "textInactive", "textDisabled",
}


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def emit(x):
    """MGS writes an exact 1 as `1`; everything else at float32 precision."""
    return "1" if x == 1.0 else repr(f32(x))


def hexof(p):
    return "%02X%02X%02X" % tuple(round(p[k] * 255) for k in ("red", "green", "blue"))


def parse_edit(arg):
    m = re.fullmatch(r"([A-Za-z0-9_]+):([A-Za-z]+)=#?([0-9A-Fa-f]{6})", arg)
    if not m:
        raise SystemExit(f"bad edit {arg!r}; want SCHEME_NAME:slot=#RRGGBB")
    name, slot, val = m.group(1), m.group(2), m.group(3).upper()
    if slot not in SLOTS:
        raise SystemExit(f"unknown slot {slot!r}; one of: {' '.join(sorted(SLOTS))}")
    return name, slot, val


def main(argv):
    args = [a for a in argv[1:] if a != "--apply"]
    apply_ = "--apply" in argv
    if len(args) < 2:
        raise SystemExit(__doc__)

    zip_path, edits = args[0], [parse_edit(a) for a in args[1:]]

    text = mgs_zip.read(zip_path, "schemes.json").decode()
    out = text
    names = {s["properties"]["name"]["value"] for s in json.loads(text)["schemes"]}

    wanted, noop = {}, []
    for name, slot, val in edits:
        if name not in names:
            raise SystemExit(f"no scheme named {name}")

        # The scheme's properties are serialized alphabetically, so every colour slot
        # sits before its own "name" entry — anchor on the name, then walk back.
        i = out.index(f'"{name}"')
        j = out.rindex(f'"{slot}": {{', 0, i)
        end = out.index("}", j)
        block = out[j:end]

        before = re.search(r'"red": ([-0-9.eE]+)', block)
        r, g, b = (int(val[k:k + 2], 16) / 255.0 for k in (0, 2, 4))
        new = block
        for key, v in (("red", r), ("green", g), ("blue", b)):
            new, n = re.subn(rf'("{key}": )[-0-9.eE]+',
                             lambda m, v=v: m.group(1) + emit(v), new, count=1)
            if n != 1:
                raise SystemExit(f"{name}.{slot}: no {key} field to replace")
        if new == block:
            noop.append(f"{name}.{slot}")
        out = out[:j] + new + out[end:]
        wanted[f"{name}.{slot}"] = val
        print(f"{name:28} {slot:20} -> #{val}"
              + ("   (already)" if f"{name}.{slot}" in noop else ""))
        assert before is not None

    # Verify by reparsing: the set of changed fields must be exactly what was asked for.
    a = {s["properties"]["name"]["value"]: s for s in json.loads(text)["schemes"]}
    b = {s["properties"]["name"]["value"]: s for s in json.loads(out)["schemes"]}
    if a.keys() != b.keys():
        raise SystemExit("scheme set changed — refusing to write")

    changed = []
    for n in a:
        pa, pb = a[n]["properties"], b[n]["properties"]
        if pa.keys() != pb.keys():
            raise SystemExit(f"{n}: property set changed — refusing to write")
        changed += [f"{n}.{k}" for k in pa if pa[k] != pb[k]]

    unexpected = set(changed) - set(wanted)
    if unexpected:
        raise SystemExit(f"unexpected edits {sorted(unexpected)} — refusing to write")
    for k, val in wanted.items():
        n, slot = k.rsplit(".", 1)
        got = hexof(b[n]["properties"][slot])
        if got != val:
            raise SystemExit(f"{k}: wrote #{got}, wanted #{val}")

    print(f"\nverified: {len(changed)} field(s) differ, all requested"
          + (f"; {len(noop)} already correct" if noop else ""))
    print(f"bytes {len(text)} -> {len(out)}")

    if not changed:
        print("nothing to do")
        return 0
    if apply_:
        bak = mgs_zip.repack(zip_path, {"schemes.json": out.encode()}, backup=True)
        print(f"repacked; backup {bak}" if bak else
              "repacked; NO fresh backup (a .bak already existed — rely on git)")
        print("user must now run MGS Generate")
    else:
        print("DRY RUN — pass --apply to write")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
