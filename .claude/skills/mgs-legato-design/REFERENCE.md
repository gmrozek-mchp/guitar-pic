# MGS / Legato design.zip — reference

## Zip anatomy

`default_design.zip` is a plain zip of JSON (+ binary font/image asset blobs). Top-level members:

| Member | Holds |
|---|---|
| `schemes.json` | `{"schemes":[…], "userDefault":"{uuid}"}` — all color schemes |
| `globalpalette.json` | global palette entries |
| `stringtable.json` | strings + languages/translations |
| `screens/screens.json` | screen list |
| `screens/{uuid}/screen.json` | one screen's full widget tree (large) |
| `state.json` | project/editor state (also holds asset refs) |
| `memorylocations.json` | asset memory placement |
| `version.json` | format version |
| `assets/fonts/{uuid}/…` | font config JSON + `sourceData` blobs |
| `assets/images/{uuid}/…` | image config JSON + raw blobs |
| `assets/images/images.json` | **manifest** — `{"images":[{id,name,type},…]}` listing every image |

Everything you'd edit programmatically is JSON. Preserve all other members verbatim on repack
(the font/image blobs are large — don't touch them).

### Adding an image asset (yes, this works — no Composer import needed)

`scripts/add_image.py <zip> <file.png> <NAME> --like <ExistingImage> [--bind WIDGET:prop,…]`
adds a PNG as a real image asset and can repoint widgets at it in the same pass. Four writes
are needed and all four matter:

| write | contents |
|---|---|
| `assets/images/{new-uuid}/imageconfig.json` | identity + sizes: `id`, `outputName`, `source/outputWidth`, `source/outputHeight`, `sourceFormat: "png"`, `memoryLocation` |
| `assets/images/{new-uuid}/rawconfig.json` | `colorMode`, `useRLE`, `colorCount`, and `maskColor.image` — which is **self-referential**, so it must be repointed to the new uuid |
| `assets/images/{new-uuid}/sourceData` | the raw PNG bytes, verbatim |
| `assets/images/images.json` | one `{id, name, type}` entry (`name` == `outputName`) |

**Clone an existing image's configs rather than synthesizing them** (`--like`), overriding only
identity and dimensions: that way every field this design's MGS version expects is present with
a plausible value, including `memoryLocation`. Pick a template of the same kind — a sibling
icon, not a full-screen bitmap. Derived fields (`outputSize`, and `colorCount` if it disagrees)
are recomputed by MGS on Generate, so small mismatches self-heal.

`mgs_zip.repack` only *replaces* existing members, so the three new asset members must be
appended (`zipfile.ZipFile(..., "a")`) after the repack that updates the manifest and screen.

Match the surrounding art when rasterizing: sample an existing sibling's opaque pixels for the
stroke colour and reuse it, so a new icon doesn't stand out. For lucide SVGs, substitute the
`currentColor` stroke for that hex and `rsvg-convert -w N -h N` at the icon's native size.

**Assets are listed in a manifest AND stored in their own directory — you must update both.**
Deleting an image means dropping `assets/images/{uuid}/` *and* removing its entry from
`assets/images/images.json`; dropping only the directory leaves a manifest entry pointing at
nothing. Fonts follow the same pattern (`fonts.json` `fonts[]` + `assets/fonts/{uuid}/`). Note
the image manifest lives **under `assets/`**, so a "scan the design JSON for references" loop
that skips `assets/` will not see it — check the manifest explicitly instead, and assert
`manifest ids == asset dirs` afterwards.

## The generated-C relationship

MGS **Generate** reads this zip and emits `.../gfx/legato/generated/le_gen_*.c/.h`
(`le_gen_scheme.*`, `le_gen_screen_<Name>.c`, `le_gen_string.*`, `le_gen_assets.*`).
- A scheme's C symbol `extern const leScheme <NAME>` comes from its `name` field.
- Widget setup in `le_gen_screen_*.c` emits `widget->fn->setScheme(widget, &<NAME>)` from the
  widget's scheme **uuid** → resolved to that scheme's current name at generate time.
So: edit the zip → **the user must Generate** for `le_gen_*` (and thus the build) to change.
Do **not** hand-edit `le_gen_*` — it's regenerated and overwritten.

## schemes.json structure

```jsonc
{ "schemes": [ {
    "backgroundTable": { …16 booleans… },   // per-role "use background color?" flags
    "foregroundTable": { …16 booleans… },
    "properties": {
      "name":      { "type":"text", "value":"SCHEME_TEXT_ZINC_400" },
      "id":        { "type":"uuid", "value":"{fb9498cd-…}" },
      "colorMode": { "type":"combo","value":6 },              // see enum below
      "base":      { "type":"color","red":0.62,"green":0.62,"blue":0.66,"alpha":1, … },
      "text":      { "type":"color", … },
      …14 more color fields…
      "<field>_mono": { "type":"boolean","value":false }      // one per color field
    } } ],
  "userDefault": "{uuid}" }
```

**Color fields (16), matching the `leScheme` struct order** used in `le_gen_scheme.c`:
`base, highlight, highlightLight, shadow, shadowDark, foreground, foregroundInactive,
foregroundDisabled, background, backgroundInactive, backgroundDisabled, text, textHighlight,
textHighlightText, textInactive, textDisabled`. Each color is `{red,green,blue,alpha}` floats 0–1.

**What a widget actually uses:**
- A **label** draws its string in the scheme's **`text`** color. For a *transparent* label
  (`BACKGROUND_NONE`) the anti-aliased edges blend against the real pixels underneath, so
  `base`/`background` are irrelevant. (An *opaque* label fills its rect with `base` first —
  then `base` must match the surface behind it.)
- A **filled panel / dot / button background** uses **`base`**.
- A **button** is 2-tone: `base` fill + `text` label.
So a "text scheme" carries the color in `text`; a "fill scheme" in `base`; a "component
scheme" (button/nav) is a genuine 2-tone pair that can't collapse to a single color name.

**`colorMode` enum** (combo `value`): `0`=GS_8, `1`=RGB_332, `2`=RGB_565, `3`=RGB_888,
`4`=RGBA_5551, `5`=RGB_565(alt), `6`=RGBA_8888, … Confirm against a known scheme in the target
project (e.g. one already set to the mode you want). Changing `colorMode` only affects how MGS
displays/edits the scheme; the `{red,green,blue}` floats are the source of truth.

## stringtable.json + fonts — structure

```jsonc
{ "defaultLanguage": "{uuid}", "encoding": "UTF8",
  "languages": [ { "properties": { "id":…, "name":{"value":"Default"}, "enabled":… } } ],
  "strings":   [ { "properties": { "id":…, "name":{"value":"SONG_INFO_Title"},
                                   "description":…, "path":{"value":"/"} },
                   "values": [ {"language":"{uuid}", "value":"Title"} ] } ],
  "bindings":  [ { "string":"{uuid}", "language":"{uuid}", "font":"{uuid}" } ] }
```

**A font is bound per (string, language) — not per widget.** Widgets carry only
`{"type":"string","value":"{uuid}"}`; there is normally **no `"type":"font"` ref in
`screen.json` at all`**. So "change this label's font" = change that *string's* binding.
Two labels sharing one string cannot have different fonts — that's why Figma-imported
designs sprout near-duplicate strings (`figmaStr_Connected`, `_Connected_0`, `_0_0`).

`assets/fonts/fonts.json` is a flat `{"fonts":[{"id","name"}],"paths":[]}` index; each font
owns **4 zip members** under `assets/fonts/{uuid}/`:

| Member | Holds |
|---|---|
| `fontconfig.json` | `outputName` (→ the C symbol), `pointSize`, `height`, `maxHeight`, `maxBaseline`, `antialias`, `glyphCount`, `glyphDataSize`, memory locations |
| `ranges.json` | `{"user":[{"name","start","end"}]}` — the **declared** codepoint ranges |
| `glyphs.json` | `{"<char>": {"codePoint","advance","width","height","bearingX","bearingY"}}` — the **actual** generated glyph set |
| `sourceData` | the raw TTF blob (hundreds of KB) |

**Containment (verified):** font uuids appear only in `fonts.json`, the font's own
`fontconfig.json`, and `stringtable.json`. String uuids only in `stringtable.json` and
`screen.json`. `state.json` / `memorylocations.json` / `styles.json` reference neither.
So string+font cleanups touch exactly those members.

### Font gotchas worth checking before any font work

- **Names lie — hash the `sourceData` blob.** MGS silently substitutes a face it can't find
  and keeps the requested name. In marvin, 31 fonts named `figmaFont_Menlo_*`,
  `figmaFont_Inter_13`, `figmaFont_Cousine_13`, and `NotoSans_Regular_*` were **one identical
  TTF: Noto Sans Regular** — so the "Menlo" labels weren't even monospace. Group fonts by
  `sha1(sourceData)` and read the TTF `name` table (nameID 1/2/4/6) to learn the real faces.
- **Design-time strings get their glyphs automatically; runtime text does NOT.** This is the
  single most important font rule here.
  - **MGS auto-adds the glyphs required by bound strings** on Generate, beyond what
    `ranges.json` declares. A DejaVu 12 declaring 32–126 + 160–255 (191 glyphs) reports
    `glyphCount` 194 — the extras being exactly the U+2014 — / U+25A0 ■ / U+2605 ★ used by
    strings bound to it. So **retargeting a string onto a font that lacks its special
    characters self-heals on Generate** (confirmed: retargeting three em-dash strings onto
    `DejaVuSansMono_12` grew it 193→194 glyphs, +28 bytes, with no manual step).
  - **Characters that only ever appear at runtime must be added to the font's character set
    by hand in MGS** (Font asset → add a range, or add the characters). MGS cannot see text
    built by `setString` from a C literal, a CSV / QSPI data file, or a `sprintf` of a device
    name — an uncovered codepoint there just renders as a missing glyph, silently, with no
    build error. Fonts whose ranges are ASCII-only are the exposure: check them against any
    data file whose text they may draw.
  - `scripts/audit_glyph_coverage.py` checks both sides — design-string coverage per font, plus
    non-ASCII in hand-source literals (C comments stripped) and data files.
- **Real flash cost is in the generated C, not `sourceData`.** All sizes of a face share one
  TTF blob, so `sourceData` size says nothing about cost. Sum the
  `const uint8_t <font>_{data,glyphs}[N]` arrays in
  `.../gfx/legato/generated/font/le_gen_fonts.c` for the per-font byte cost.
- **A 0-glyph font may not be emitted at all** (an empty stub font produced no
  `leRasterFont` in `le_gen_fonts.c`), so a hand-source reference to it would fail to link.
- **Predict layout damage before retargeting.** Sum the per-glyph `advance` from each font's
  own `glyphs.json` for the string's text and compare old vs new against the widget's
  `width` — that catches overflow (and finds labels *already* overflowing).
- **Regular→bold is free in a monospace family.** DejaVu Sans Mono Bold has the *same*
  advance as Regular at every size, so a weight-only rebinding cannot change any label's
  width. Verify with `glyphs.json` (one distinct advance per font, equal across the pair)
  and then weight changes need no layout review at all.
- **When a symbol looks wrong beside a sibling, compare glyph ink extents before changing
  size or padding — it is usually the wrong codepoint.** Advance is uniform in a monospace
  font, so mismatches come from the *ink*. Measure with fontTools `BoundsPen` on the real
  TTF: in DejaVu Sans Mono, `+` (U+002B) and `−` (U+2212 MINUS SIGN) are a designed pair —
  both 12.4px wide with the bar at +7.5px at 24px — whereas `—` (U+2014 EM DASH) is 14.4px
  and sits 0.9px lower. A `-`/`—` standing in for a real minus is a common Figma-import
  artifact and reads as misaligned next to a `+`.
- **Deleting a font must drop its 4 zip members**, not just its `fonts.json` entry — use
  `mgs_zip.repack(..., drop=["assets/fonts/{uuid}/"])`.

## How references work

Widgets reference schemes (and other assets) by **uuid**, in `screen.json` / `state.json`:
```json
{ "type":"scheme", "value":"{a37721fd-…}" }
```
The null uuid `{00000000-0000-0000-0000-000000000000}` means "none / inherit". Count real
references by regex over the screen/state JSON:
`"type":\s*"scheme",\s*"value":\s*"(\{[0-9a-fA-F-]+\})"`.

## Transform recipes

- **Rename:** set `properties.name.value`; keep `id`. Widgets unaffected. Emit a hand-source
  symbol rename (`&OLD`→`&NEW`) for any code-referenced scheme.
- **Merge duplicates:** pick a survivor uuid; delete the clones; replace every clone-uuid in
  screen/state JSON with the survivor uuid. Prefer a survivor whose name is code-referenced so
  no source edit is needed.
- **Delete unused:** remove schemes referenced by **neither** a widget uuid **nor** hand code.
- **Add:** deep-copy an existing scheme of the same shape as a template, assign a fresh uuid
  (`uuid.uuid4()`), set `name` + `colorMode` + the relevant color field(s). For a scheme usable
  as *both* text and fill (e.g. a status dot + its label), set `base = foreground = text`.
- **Recolor / normalize:** overwrite the `{red,green,blue}` floats (e.g. merge Tailwind v3→v4
  near-duplicates to one value).
- **Retarget a string's font:** rewrite that string's entry in `stringtable.json` `bindings[]`
  → `"font": "{new uuid}"`. Widget-invisible; the only risk is metrics (check widths first).
- **Delete unused strings:** drop from `strings[]` **and** drop their `bindings[]` entries.
  Live = the string uuid appears in a widget's `properties.string` in `screen.json`. Hand
  source usually references **no** design string (runtime text goes through `setString` with a
  dynamic string) — grep `stringID_` outside `config/default/` to confirm, and if it's empty,
  string deletes and renames cannot break the C build.
- **Delete unused fonts:** drop from `fonts.json` `fonts[]` **and** `drop=` the font's
  `assets/fonts/{uuid}/` directory. Keep any font whose `outputName` is referenced by hand
  source even when no string binds to it.
- **Merging duplicate string *values* is usually wrong — check first.** Two strings with the
  same text are often independent fields that merely share a placeholder (a start-time and a
  stop-time both `'0:00'`), and merging couples them through one asset. Split the live set by
  whether hand source references the widget: **code-referenced ⇒ the design text is a
  placeholder overwritten at runtime** (never merge those), not-referenced ⇒ it is the shipped
  static text (mergeable, but merging a heading across *screens* means editing one screen
  silently changes the other). Also **only merge strings that share a font binding** — one
  string can carry only one font per language.
- **Prefer renaming a live string onto a freed name over repointing the widget.** When a dead
  asset holds the name you want, deleting it frees the name for the live string — same end
  state, zero visual change, and you don't inherit the dead asset's font binding (which may be
  a size you're about to delete).

## Icon as a glyph, or as an image?

A figma import gives you every icon as a small PNG, and it is tempting to convert the ones with
Unicode equivalents — a tick → U+2713, a solid play triangle → U+25B6, arrows →
U+25B2/25C0/25B6/25BC — since MGS auto-includes the glyph on Generate and the asset disappears.

**Decide per control group, and let the least-representable icon in the group decide.** If any
icon in the same set of buttons has no sane character (a playlist glyph; a hamburger — U+2630 is
absent from DejaVu Sans Mono), that set can only be all-images, and converting its siblings makes
the group *less* consistent. Different groups may legitimately differ: one screen's d-pad can be
arrow glyphs while another screen's toolbar stays images. Judge consistency within the group the
user sees together, not across the firmware.

Two practical notes:

- **File size rarely decides it.** A 14–24 px icon is a few hundred bytes, and an added glyph
  costs about the same, so argue from consistency and asset-management instead.
- **Folding an icon into the label changes layout.** Legato centres image+text as a unit with
  `setImageMargin`; as a glyph it becomes part of the text run, so spacing shifts. Watch for
  imported strings that carry a **leading space** (`' SELECT SONG'`) — that space may be doing
  the icon/text spacing where no `imageMargin` is declared, so it is not automatically a
  removable artifact. Prefer an explicit `imageMargin` over an invisible character, but verify
  visually.

## Recipe: strip a widget subtree to hand-code a screen

A recurring move on a figma-imported design: a screen's imported widget tree is being replaced
by a programmatic builder in C (marvin's `screen_bus.c` / `screen_wiimotes.c` model), so the
design should supply only the **empty root panel** the builder attaches to. Delete every child
of that panel and leave the layer + root intact. `scripts/strip_subtree.py` does this.

Why hand-code at all: a re-layout of N widgets is unreviewable as a zip delta and miserable to
drag in Composer, and anything needing per-pixel paint (an arc control, a two-tone
fill-plus-ring, a slider that fills from its centre) **cannot be expressed by a widget +
scheme** at all — a `leScheme` carries 16 named colors, not "fill *and* a differently-coloured
ring". Layout constants in C are also diffable.

Three things to get right:

- **Keep the root panel** — the builder needs it, and hand source references it
  (`Marvin_PANEL_FOO`). Confirm it has `background = 1` (FILL) if the builder puts AA-rounded
  cards on it: `PanelAA_Enable` samples the parent's pixel for its corner backdrop and assumes
  an opaque parent. Compare against a panel already known to work.
- **The deleted widgets' STRINGS are usually worth keeping, and driving from C via
  `leTableString` + `stringID_*`** — the opposite of the "hand-coded screens use C literals"
  habit. Two reasons, in order:
  1. **Localization.** A design string carries a value per language; a C literal can never be
     translated. Table strings are the default for any user-facing caption.
  2. **Glyphs.** MGS auto-includes glyphs only for strings **it can see in the design**, so a
     non-ASCII caption in a C literal renders as a missing glyph with **no build error** (see
     the glyph rule above). Imported captions hit this constantly: d-pad arrows
     (▲ ◀ ▶ ▼ = U+25B2/25C0/25B6/25BC), a true minus (− = U+2212), check / backspace glyphs.

  A table string also keeps the design's per-string font binding. Net effect: deleting the
  widgets costs you nothing in the string table, and `audit_strings_fonts.py` stays at 0 unused.
  Dropping to C literals is a defensible shortcut for a throwaway/demo view that will never be
  translated — just make it a recorded decision, not a default.
- **This is not a build-breaking handoff.** The generated `le_gen_*` on disk are untouched by a
  zip edit, so the project still compiles before Generate *provided the new C references none of
  the deleted widget globals*. What you get until Generate is a **visual** artifact: the screen's
  `screenShow_*` still constructs the old subtree, which renders underneath the new layout.

Report what the deletion orphans (strings / images / fonts) rather than sweeping it in the same
pass — compare asset refs from the doomed subtree against refs from everything that survives, so
shared assets aren't miscounted. Keeping the sweep separate leaves the zip delta reviewable as
"subtree removed".

## Gotchas (learned the hard way)

- **A Generate can lag an external zip edit — verify, don't assume.** Observed once (marvin,
  2026-08-05): after several Generates the output reflected two earlier edits but not a third
  (all 44 `leImage` still declared while the zip had 16). It cleared on a later Generate; root
  cause never established. MGS re-serializes the design on save and keeps a
  `.legato_generate_cache.zip`, so an edit made while Composer holds the project open is at
  least plausibly missable — **close and reopen the design before Generate** as a cheap
  precaution. Always give the user a *countable* check ("the design should list N images") so a
  stale Generate is distinguishable from a bad edit.
- **`mgs_zip.repack`'s `.bak` is the PRISTINE original, not the previous state.** It only
  copies when no `.bak` exists (`if not os.path.exists(bak)`), so on the second and later
  repacks the backup is *not* a one-step rollback — it can be many sessions old. Restoring it
  to "undo the last run" silently reverts every earlier change too (asked once for the previous
  step, got a zip from two cleanups ago). **Use `git checkout -- <zip>` to roll back a step**;
  the zip is tracked, so HEAD is the reliable baseline. Don't print "backup at X.bak" in a
  transform script — it implies a rollback point that isn't there.
- **"Unused by widget" ≠ unused — hand source references assets by generated symbol name.**
  An audit that only checks widget uuid refs will call logos, LED indicators and icons unused
  when `titlebar.c` et al. draw them from C. Always intersect with a grep of hand source for the
  asset's `outputName` (excluding `config/default/`) before deleting. In marvin, 35 of 47 images
  had no widget ref but 5 of those were live from C.
- **Grep hand source EXCLUDING `config/default/`** when checking code references — the
  generated tree declares every symbol and yields false positives.
- **Renames are widget-safe, deletes are not** — repoint uuids on delete/merge.
- **Palette version drift:** designs often mix Tailwind v3 and v4 shades of the "same" color
  (e.g. zinc-400 `#A1A1AA` v3 vs `#9F9FA9` v4). Decide whether to consolidate (a recolor).
- **Pure/semantic colors aren't palette entries:** test-pattern bars (`#FF0000`), tier ramps,
  per-node identity colors — keep *function* names, don't force a `TEXT_/FILL_<color>` name.
- **CRLF:** generated files may be CRLF; git may warn on commit — harmless.
- **MGS re-serializes** the zip on save, so its committed form may differ byte-for-byte from
  your edit (same content). Commit the MGS-saved version after regen.
- **Static vs pooled widgets** (Legato, not the zip): `leX_New()` uses `LE_MALLOC` (a fixed
  block pool, not libc malloc); the public `leX_Constructor(&static_struct)` builds a widget in
  BSS with no pool use — preferred for fixed screens to avoid pool exhaustion.

## Validation checklist (before repack)

- [ ] every scheme-uuid in screen/state JSON resolves to a surviving scheme
- [ ] scheme `id`s unique; scheme `name`s unique
- [ ] no code-referenced name deleted without a source-rename patch
- [ ] `userDefault` uuid still resolves
- [ ] zip `testzip()` passes after repack
- [ ] **asset manifests agree with asset directories** — `images.json` ids == `assets/images/*/`
      dirs, `fonts.json` ids == `assets/fonts/*/` dirs (a delete must update both)
- [ ] no widget references an image/font whose asset directory is gone
- [ ] `stringtable.json`: every `bindings[].string` resolves to a surviving string; string `id`s
      and `name`s unique
