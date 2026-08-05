#!/usr/bin/env python3
"""Extract the resolved typography (px size + bold) of every text node in the Figma
mockup, honouring Tailwind class inheritance, and diff it against marvin's MGS design
string -> font bindings.

The mockup is a single-typeface design (Tailwind `font-mono` throughout, no @font-face),
so the only things that can deviate are **size** and **weight**. This finds the labels
where marvin's bound font disagrees with the mockup element it came from.

Usage:
    mockup_typography.py <mockup-dir> [design.zip]        # report
    mockup_typography.py <mockup-dir> <design.zip> --dump # every mockup text node
"""
import json
import os
import re
import sys
import zipfile
from collections import defaultdict

# Tailwind v4 scale at the mockup's 16px root (theme.css: html { font-size: 16px })
TW_SIZE = {"text-xs": 12, "text-sm": 14, "text-base": 16, "text-lg": 18,
           "text-xl": 20, "text-2xl": 24, "text-3xl": 30, "text-4xl": 36}
BOLD = ("font-bold", "font-semibold", "font-black")


def scan_jsx(text):
    """Yield ('open'|'close'|'self'|'text', name_or_content, attrs, line) honouring
    quotes and brace nesting inside attributes."""
    i, n, line = 0, len(text), 1
    while i < n:
        if text[i] == "<":
            j, depth, quote = i + 1, 0, None
            while j < n:
                c = text[j]
                if quote:
                    if c == quote:
                        quote = None
                elif c in "\"'":
                    quote = c
                elif c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                elif c == ">" and depth == 0:
                    break
                j += 1
            raw = text[i + 1:j]
            ln = line + text[i:j].count("\n")
            if raw.startswith("/"):
                yield ("close", raw[1:].strip(), "", ln)
            else:
                m = re.match(r"([A-Za-z][\w.]*)", raw)
                name = m.group(1) if m else ""
                selfclose = raw.rstrip().endswith("/")
                yield ("self" if selfclose else "open", name, raw[len(name):], ln)
            line += text[i:j + 1].count("\n")
            i = j + 1
        else:
            j = text.find("<", i)
            if j < 0:
                j = n
            chunk = text[i:j]
            if chunk.strip():
                yield ("text", chunk, "", line)
            line += chunk.count("\n")
            i = j


def attr_size_bold(attrs):
    """(size_px_or_None, bold_or_None) declared directly on this element."""
    size = None
    for cls, px in TW_SIZE.items():
        if re.search(r"[\s'\"`]%s\b" % cls, attrs):
            size = px
    m = re.search(r"fontSize:\s*['\"]?(\d+)", attrs)
    if m:
        size = int(m.group(1))
    bold = None
    if any(re.search(r"[\s'\"`]%s\b" % b, attrs) for b in BOLD):
        bold = True
    if re.search(r"fontWeight:\s*['\"]?(bold|[6-9]00)", attrs):
        bold = True
    return size, bold


def mockup_nodes(root):
    """[(file, line, size, bold, text)] for every visible text node."""
    out = []
    for dp, dn, fn in os.walk(root):
        if "node_modules" in dp or os.sep + "ui" in dp:
            continue
        for f in sorted(fn):
            if not f.endswith(".tsx"):
                continue
            p = os.path.join(dp, f)
            src = open(p, encoding="utf-8", errors="replace").read()
            stack = []
            for kind, val, attrs, ln in scan_jsx(src):
                if kind == "open":
                    stack.append(attr_size_bold(attrs))
                elif kind == "close":
                    if stack:
                        stack.pop()
                elif kind == "text":
                    size = next((s for s, _ in reversed(stack) if s is not None), None)
                    bold = next((b for _, b in reversed(stack) if b is not None), False)
                    rel = os.path.relpath(p, root)
                    # literal text
                    for piece in re.split(r"\{[^{}]*\}", val):
                        t = piece.strip()
                        if t and not t.startswith(("/*", "//")):
                            out.append((rel, ln, size, bold, t))
                    # interpolations — these are the mockup's dynamic labels, which is
                    # what marvin's runtime-updated strings correspond to
                    for expr in re.findall(r"\{([^{}]+)\}", val):
                        e = expr.strip()
                        if e and not e.startswith(("/*", "//")) and "=>" not in e \
                                and not e.startswith(("'", '"')):
                            out.append((rel, ln, size, bold, "«%s»" % e[:38]))
    return out


def marvin_bindings(zip_path):
    z = zipfile.ZipFile(zip_path)
    FN = {f["id"]: f["name"] for f in json.loads(z.read("assets/fonts/fonts.json"))["fonts"]}
    st = json.loads(z.read("stringtable.json"))
    S = {}
    for s in st["strings"]:
        S[s["properties"]["id"]["value"]] = (
            s["properties"]["name"]["value"],
            next((v.get("value", "") for v in s["values"]), ""))
    b = {x["string"]: FN[x["font"]] for x in st["bindings"]}
    return S, b, set(FN.values())


def font_for(size, bold):
    return "DejaVuSansMono%s_%d" % ("Bold" if bold else "", size)


def parse_font(name):
    m = re.match(r"DejaVuSansMono(Bold)?_(\d+)$", name)
    return (int(m.group(2)), bool(m.group(1))) if m else (None, None)


def norm(s):
    return re.sub(r"\s+", " ", s).strip().lower()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    root = os.path.join(argv[1], "src", "app")
    nodes = mockup_nodes(root)
    if "--dump" in argv:
        for f, ln, sz, b, t in nodes:
            print("  %-46s :%-4d %-5s %-7s %r" % (f, ln, sz, "BOLD" if b else "reg", t[:44]))
        print("\n%d text nodes" % len(nodes))
        return 0

    zip_path = argv[2]
    S, bind, have = marvin_bindings(zip_path)

    # mockup text -> set of (size, bold) it appears with
    idx = defaultdict(set)
    for f, ln, sz, b, t in nodes:
        if sz:
            idx[norm(t)].add((sz, b))

    rows, unmatched = [], []
    for sid, (name, val) in sorted(S.items(), key=lambda kv: kv[1][0]):
        cur = bind.get(sid, "")
        csz, cbold = parse_font(cur)
        cand = idx.get(norm(val))
        if not cand:
            unmatched.append((name, val, cur))
            continue
        if (csz, cbold) in cand:
            continue
        # prefer the candidate closest to the current size (screens diverged; weight is
        # the signal we trust, size only when it agrees)
        best = sorted(cand, key=lambda sb: (abs(sb[0] - (csz or 0)), not sb[1]))[0]
        rows.append((name, val, cur, best, sorted(cand)))

    print("== deviations from the mockup (%d of %d matched strings)"
          % (len(rows), len(S) - len(unmatched)))
    print("   %-30s %-22s %-24s %s" % ("string", "value", "marvin has", "mockup says"))
    for name, val, cur, best, cand in rows:
        want = font_for(*best)
        flag = "" if want in have else "  (font not in design!)"
        print("   %-30s %-22r %-24s %-24s%s"
              % (name, val[:20], cur, want, flag))
        if len(cand) > 1:
            print("        %s" % ("mockup uses this text at: "
                                  + ", ".join("%dpx%s" % (s, "/bold" if b else "") for s, b in cand)))
    print("\n== not found in the mockup (%d) — placeholders or marvin-only text"
          % len(unmatched))
    for name, val, cur in unmatched:
        print("   %-30s %-22r %s" % (name, val[:20], cur))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
