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

**Cloning inherits `colorMode`, `useRLE` and `memoryLocation` — so the template choice is a
settings choice, not just a shape choice.** If the design has deliberately turned RLE off for a
class of images (a decode bug, a size/robustness tradeoff), a template from that class carries
that decision forward for free; one from outside it silently reintroduces the old setting. When
adding a **per-state pair** (rest + selected/pressed variants of one icon), clone each variant
from its counterpart so the pair stays symmetric, and verify the two rasters are
**alpha-identical** — count opaque pixels in each; equal counts prove only the colour differs,
which is what the state swap relies on visually.

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

### Where a replaced image actually lands (alignment, not position)

`set_image_source.py` changes an asset's pixel dimensions, which changes **where the image draws**
without changing any widget property. The chain, all in the generated tree:

- `_leImageWidget_GetImageRect` seeds the rect at the image's `buffer.size` and hands it to
  `leUtils_ArrangeRectangle` with the widget's `style.halign` / `style.valign`.
- `_leImageWidget_Constructor` sets `rect.width`/`height`, border and background — **not**
  alignment. So the values in force are `leWidget_Constructor`'s: `LE_HALIGN_CENTER` and
  `LE_VALIGN_MIDDLE`.
- `leUtils_ArrangeRectangle`'s centre case is
  `sub->y = bounds.y + (bounds.height / 2) - (sub->height / 2)` — **two separate integer
  divisions**. `(bounds - sub)/2` is a different number when the parities differ.

Worked example, from marvin's titlebar (a 44 px-tall widget whose image shrank 43 → 41):
`44/2 − 43/2 = 22 − 21 = 1`, then `44/2 − 41/2 = 22 − 20 = 2`. The image moves **down one pixel**
and the centreline holds at 23 — with `setPosition` untouched. A 45 px widget going 45 → 41 moves
down two the same way. Useful when that is what you wanted; invisible damage when it isn't.

Two consequences worth acting on:

- **Make the rect equal the image and the whole mechanism switches off** (offset 0 on both axes),
  so the source states the position instead of implying it. This costs nothing visually: an
  opaque image covers its rect exactly, so `LE_WIDGET_BACKGROUND_FILL` + scheme and
  `LE_WIDGET_BACKGROUND_NONE` render identically once they are the same size. Assets stored as
  RGB are opaque by construction — `rawconfig.json`'s `backgroundColor` is what the PNG's alpha
  was flattened against, so the *parent* backdrop must match that colour, which is the real
  constraint and it is unaffected by a resize.
- **Verify the image edge is the visual edge.** `Image.open(p).convert("RGBA").split()[3].getbbox()`
  against the canvas size says whether the art is ink-tight or padded; only for ink-tight art does
  aligning the canvas align what the eye sees.

Reading the dimensions back after Generate: `leImage <NAME> = { … }` in
`<gen>/image/le_gen_images.c` carries the size and the RLE data length, and the `<NAME>_data[]`
array length is the flash cost. A zip newer than that file means the Generate predates the swap.

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

**Adding a string means writing BOTH lists** (`scripts/add_string.py <zip> <NAME> <value>
--font <FontName>`): a `strings[]` entry (fresh uuid, `name`, `path`, one `values[]` per
language) *and* a matching `bindings[]` entry. Skip the binding and the string generates a
`leTableString` with **no font**, and — because the binding is what MGS follows to collect
glyphs — none of its characters are guaranteed to be in any font. So the font is part of adding
the string, not a later decision; pick the one its neighbouring captions use. Refuse duplicate
names: the generated C symbol *is* the name. A string needs **no widget reference** to be
generated (`stringID_<NAME>` + `string_<NAME>` are emitted regardless), which is exactly what
lets a hand-built screen own its captions while the design keeps the text and its translations.

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
  and keeps the requested name. Seen in the wild: 31 fonts named `figmaFont_Menlo_*`,
  `figmaFont_Inter_13`, `figmaFont_Cousine_13` and `NotoSans_Regular_*` were **one identical
  TTF — Noto Sans Regular** — so the "Menlo" labels weren't even monospace. Group fonts by
  `sha1(sourceData)` and read the TTF `name` table (nameID 1/2/4/6) to learn the real faces.
- **Design-time strings get their glyphs automatically; runtime text does NOT.** This is the
  single most important font rule here.
  - **MGS auto-adds the glyphs required by bound strings** on Generate, beyond what
    `ranges.json` declares. A DejaVu 12 declaring 32–126 + 160–255 (191 glyphs) reports
    `glyphCount` 194 — the extras being exactly the U+2014 — / U+25A0 ■ / U+2605 ★ used by
    strings bound to it. So **retargeting a string onto a font that lacks its special
    characters self-heals on Generate** (confirmed: retargeting three em-dash strings onto
    `DejaVuSansMono_12` grew it 193→194 glyphs, +28 bytes, with no manual step).
  - **Characters that only ever appear at runtime must be declared in the font's `ranges.json`**
    (`add_font_range.py`, or in MGS: Font asset → add a range / add the characters). Do not rely
    on some *other* design string happening to contain the character — that keeps the glyph alive
    only until that string is retargeted or pruned, and the failure is a blank glyph with no build
    error. A declared range makes it a property of the font. MGS cannot see text
    built by `setString` from a C literal, a CSV / QSPI data file, or a `sprintf` of a device
    name — an uncovered codepoint there just renders as a missing glyph, silently, with no
    build error. Fonts whose ranges are ASCII-only are the exposure: check them against any
    data file whose text they may draw.
  - `scripts/audit_glyph_coverage.py` checks both sides — design-string coverage per font, plus
    non-ASCII in hand-source literals (C comments stripped) and data files. **Its blind spot:** it
    matches non-ASCII *characters*, so text assembled from bytes (`buf[p++] = 0xE2; … 0x98; … 0x85;`
    for ★) reads as pure ASCII and the audit passes while the screen draws a glyph nobody
    declared. A clean run is evidence, not proof — check byte-level string building by eye.
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

### Vertical metrics — where the baseline is, and what to align to

Everything above is horizontal (advance, ink width). For anything that must sit *level with*
text — a bullet dot, a rule, an accent bar, an icon beside a caption — you need the baseline,
and neither the zip nor `glyphs.json` names it. It is in the generated C.
`scripts/font_metrics.py <src-dir> [--box H]` prints the whole table; the parse, in case you
need it inline:

```
leRasterFont <name> = { {..header..}, <height>, <baseline>, <bpp>, <name>_data };

const uint8_t <name>_data[] = <u32 glyph count> then packed 20-byte records, all LE:
  codePoint u16, width i16, height i16, advance i16,
  bearingX  i16, bearingY i16, flags u16, dataRowWidth u16, dataOffset u32
```

`bearingY` is the rise above the baseline, which is how you recover the three landmarks the
struct doesn't name: **cap height** = `bearingY` of a digit, **x-height** = `bearingY` of
round lowercase (`acemnorsuvwxz`), **descender depth** = `height - bearingY` of `pqgyj`.

**Where Legato puts the text — it does NOT centre the font box.** This is the trap. Trace it:

```
_leString_GetLineRect      rect.height = fontHeight
leStringUtils_KerningRect  height -= fontHeight; height += fontBaseline
                             → for one line, height == fontBaseline  (descender dropped)
ArrangeRectangleRelative   VALIGN_MIDDLE: rect_y = H/2 - rect.height/2
leStringRenderer_DrawString  each glyph at rect_y + (fontBaseline - glyph.bearingY)
                             → rect_y IS the glyph-box top
```

So, with integer division on each term:

```
rect_y   = (H / 2) - (fontBaseline / 2)      <-- fontBaseline, NOT fontHeight
baseline = rect_y + fontBaseline
```

**Using `fontHeight` there is wrong by half the descender** — 3px for `DejaVuSansMono_16` —
and it fails silently: the text looks fine (Legato positioned it correctly all along), but
everything *you* align to your own calculation sits a few pixels off with nothing obviously
broken. This cost three rounds of "the dots are still too high" on hardware, because the model
said the dot was 0.5px low while it was really 3.5px high.

**Align markers to the line's optical middle, which is between the cap-height and x-height
middles — never the row-box centre.** The box centre reads ~3px high for the reason above. The
x-height middle (`baseline - xHeight/2`) is right for running lowercase, but UI labels are
usually caps- and digit-heavy, so their mass sits higher; the **mean of the cap-height and
x-height middles** is what reads level. Confirmed by eye on a 1280×800 panel across eight call
sites, after both the box centre and the pure x-height middle were rejected:

```c
#define TEXT_BASELINE(h, base)   (((h) / 2) - ((base) / 2) + (base))
#define DOT_Y(h, base, xh, d)    (TEXT_BASELINE(h, base) - ((xh) + (d)) / 2 - 1)
```

The `- 1` is what converts x-height middle into that mean; it holds within half a pixel for
every DejaVu Mono size 12–20 at dot diameters 6–9, so it is a constant rather than a per-site
tweak. Worked example: `DejaVuSansMono_16` in a 24px row is `base` 15, cap 12, x-height 9 — so
`rect_y` +5, baseline +20, cap middle +14, x-height middle +15.5, optical target **+14.75**. A
6px dot belongs at `y = 12`; centring it in the row box puts it at `y = 9`, **3px high.** Two
corollaries:

- **A digits-and-caps-only line is the exception** — a row of figures, an all-caps caption —
  where cap-height middle is right, because there is no lowercase mass to answer to.
- **A marker beside a multi-line item still aligns to the first line**, but expect it to read
  high there even when correct, because the eye centres on the whole block. Fix that with
  *weight* (a larger marker) before reaching for more offset.

**A label box shorter than the font clips top *and* bottom.** Legato clips to the widget rect,
so the visible symptom is a cut descender and the cause looks like a font problem. Grow the box
to `fontHeight` and shift `y` by half the growth to fit the glyphs without moving them;
`font_metrics.py --box H` flags every font that would clip at that height.

Measured for the family marvin uses, as a sanity reference (`h`/`base` are the struct fields):

| font | h | base | adv | cap | x-ht | desc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| DejaVuSansMono_9 | 12 | 9 | 5 | 7 | 5 | 2 |
| DejaVuSansMono_12 | 16 | 12 | 7 | 9 | 7 | 3 |
| DejaVuSansMono_14 | 18 | 14 | 8 | 10 | 8 | 3 |
| DejaVuSansMono_16 | 20 | 15 | 10 | 12 | 9 | 3 |
| DejaVuSansMono_18 | 22 | 17 | 11 | 13 | 10 | 4 |
| DejaVuSansMono_20 | 25 | 19 | 12 | 15 | 11 | 4 |
| DejaVuSansMono_24 | 29 | 22 | 14 | 18 | 13 | 5 |

Bold is identical to Regular on every column at 14/16/18/20/24 — so a weight change needs no
*vertical* review either, extending the "regular→bold is free in a monospace family" rule above.
**The 12px pair is the exception**: Bold_12 is `h` 15 / `base` 11 against Regular_12's 16 / 12,
so a swap at that size shifts the baseline up 1px. Check the pair rather than assuming.

## Widget properties — shape, and how to read the enums

Every widget property is an object, not a bare value: `{"enabled":…, "type":…, "value":…,
"visible":…}`. Edit `value` and leave the rest alone — `enabled`/`visible` are Composer's
inspector state, and a property absent from a widget is one that widget doesn't support (treat
that as an error, not something to add).

`type` is `"integer"`, `"boolean"`, `"text"`, `"uuid"`, an asset kind (`"scheme"`, `"string"`,
`"image"`, `"font"`) — or **`"combo"`, which holds an enum ordinal as a plain integer**. The
names are nowhere in the zip, so don't guess: the property names are also shorter than the C
setters (`background` / `border` for `setBackgroundType` / `setBorderType`). Two ways to resolve
one, in order of reliability:

1. **Read it off a sibling whose generated C you can see.** Dump the property across several
   widgets, then match against the `setXType(...)` calls in `le_gen_screen_*.c` — a widget that
   emits no setter at all is sitting on the enum's default. This settles both the mapping and
   what value you actually want.
2. Cross-check against the Legato header's enum order (`LE_WIDGET_BACKGROUND_NONE` = 0,
   `_FILL` = 1, …) — consistent in practice, but confirm with (1) before writing.

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
  dynamic string) — grep `stringID_` outside the generated tree to confirm, and if it's empty,
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
by a programmatic builder in C, so the design should supply only the **empty root panel** the
builder attaches to. Delete every child
of that panel and leave the layer + root intact. `scripts/strip_subtree.py` does this.

Why hand-code at all: a re-layout of N widgets is unreviewable as a zip delta and miserable to
drag in Composer, and anything needing per-pixel paint (an arc control, a two-tone
fill-plus-ring, a slider that fills from its centre) **cannot be expressed by a widget +
scheme** at all — a `leScheme` carries 16 named colors, not "fill *and* a differently-coloured
ring". Layout constants in C are also diffable.

Three things to get right:

- **Keep the root panel — then check the panel ITSELF for import artifacts.** The builder needs
  it, and hand source references its generated global (`<Screen>_PANEL_<NAME>`). But a figma
  import often leaves the container carrying settings that made sense only inside the deleted
  tree: a *child's* scheme reused as the panel fill, or a full `border = LINE` box where the
  mockup drew a single edge rule. Retarget it to what the builder actually wants — usually an
  opaque fill in the surface's own background colour, border NONE, with any hairline drawn as a
  1px child widget. If the builder puts AA-rounded child panels on it, the opaque fill
  (`background = 1`) is a hard requirement: a corner-smoothing paint typically samples the
  parent's pixel for its backdrop.
  - **The reliable reference is another root panel in the same design that a builder already
    works against.** Dump the same properties across both and match them — that also settles the
    enum ordinals (see below) without guessing.
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
  widgets costs you nothing in the string table, as long as the new C actually drives each one.
  Dropping to C literals is a defensible shortcut for a throwaway/demo view that will never be
  translated — just make it a recorded decision, not a default.
- **Assets the rebuilt screen deliberately does NOT use yet need a recorded intent.** A hand-built
  screen is often *narrower* than the import (only the rows/tabs/controls that exist today), so
  some kept strings and images end up referenced by neither a widget nor C. `audit_strings_fonts.py`
  will list them as unused and `prune_unused_images.py` will offer to delete them — correctly, on
  the evidence available. Write the intent down where the next person looks (the project's asset
  README, or a comment beside the builder's entry table) so "unused" reads as "reserved", not as a
  leak. Don't expect the unused count to stay at 0 after a narrowing rebuild.
- **The handoff is build-breaking as soon as the new C names a NEW asset.** A pure strip is safe —
  the generated `le_gen_*` on disk are untouched by a zip edit, so the project still compiles
  before Generate, and the only symptom is **visual**: `screenShow_*` still constructs the old
  subtree under the new layout. But if the same pass *adds* a string or image (a caption or icon
  the import lacked), the builder references `stringID_*` / `<IMAGE>` that Generate hasn't emitted
  yet, and the build fails until the user regenerates. That's fine — just say so explicitly, and
  prove the rest of the work is sound first:
  - Compile the touched translation unit alone with the pending symbols aliased to existing ones
    (`-D'stringID_NEW=stringID_EXISTING' -D'IMG_NEW=IMG_EXISTING'`, pulled from the project's
    `compile_commands.json`). A clean `-Wall -Wextra` run then means the *only* thing standing
    between here and a build is Generate.
  - Before that, confirm the un-aliased failure lists **exactly** the expected new symbols and
    nothing else — that is the check that catches a typo'd asset name or a stale reference to a
    deleted widget global.

Report what the deletion orphans (strings / images / fonts) rather than sweeping it in the same
pass — compare asset refs from the doomed subtree against refs from everything that survives, so
shared assets aren't miscounted. Keeping the sweep separate leaves the zip delta reviewable as
"subtree removed".

## Gotchas (learned the hard way)

- **A button's pressed fill is a DIFFERENT scheme slot, and its default is white.** The classic
  button skin fills from `LE_SCHM_BACKGROUND` whenever `state != LE_BUTTON_STATE_UP` and from
  `LE_SCHM_BASE` when up (`drawBackground`, `legato_widget_button_skin_classic.c`; the string
  render's lookup-table colour follows the same swap). A plain panel or widget never reads
  `background` at all — `leWidget_SkinClassic_DrawStandardBackground` only uses `base`. So a FILL
  scheme authored by eye for panels can leave `background` at Legato's default **white** and look
  perfect forever, until a button is given that scheme and flashes white under the finger. In
  marvin every `SCHEME_FILL_ZINC_*` had exactly this: correct `base`, `background` still
  `#FFFFFF`. Two consequences worth internalising:
  - It is invisible to every static check. Nothing is unreferenced, no uuid is dangling, a
    screenshot of the idle screen is right, and `audit_schemes.py`'s role column reports `base`.
    Only touching the widget shows it.
  - The fix belongs in the design, not in code. A custom paint override can repair the AA corners
    (it reads the same slot, so it stays *consistent* with the skin — and therefore consistently
    wrong), but the button's body is drawn by the stock skin from the scheme, so no widget-side
    hack fixes the fill. Set the slot: `set_scheme_color.py <zip> NAME:background=#RRGGBB`.
  - Natural value: one step lighter than `base`. An imported mockup states it directly — a
    Tailwind `hover:bg-zinc-800` on a `bg-zinc-900` card means `background` = the design's own
    zinc-800.

- **MGS's JSON serializer is not reproducible from Python, so edit by splicing text.** No
  combination of `indent` / `separators` / `sort_keys` reproduces it: marvin's 810 KB
  `schemes.json` comes back as 595 KB from `json.dumps(indent=2)` and 809 KB from
  `indent=4, sort_keys=True` — close, but not equal. A `json.loads` → mutate → `json.dumps`
  round-trip is therefore never a minimal diff; it rewrites every member you touch, which both
  buries the intended change and removes any way to show you changed nothing else. Locate the
  value in the raw member text and splice it (`add_layer.py` and `set_scheme_color.py` both do
  this), then reparse the result and assert the set of differing fields equals the set you
  intended. Useful anchor for schemes: `properties` is serialized alphabetically, so every colour
  slot appears *before* that scheme's own `"name"` entry — find the name, then `rindex` back to
  the slot.

- **A decorative widget stacked over interactive ones steals their touches.**
  `leUtils_PickFromWidget` keeps the **last** child whose rect contains the point, so "paints on
  top" and "wins the pick" are the same property. A full-card frame overlay — the standard way to
  round a card whose image reaches its edge — therefore swallows every touch inside the card
  unless it carries `LE_WIDGET_IGNOREPICK`. Same for a state dot drawn over a button (a Legato
  button paints its own caption and cannot host children). Symptom: one screen goes completely
  dead to touch while chrome outside the overlay still works.

- **A Generate can lag an external zip edit — verify, don't assume.** Observed once: after
  several Generates the output reflected two earlier edits but not a third
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
- **A json round-trip is NOT byte-neutral — MGS renders empty containers differently.** MGS
  serializes with the equivalent of `json.dumps(indent=4, sort_keys=True)` **except** that an
  empty array is `"__groups": [\n]`, not `[]`. So `load_json` → mutate → `json.dumps(indent=4,
  sort_keys=True)` reproduces the file exactly *apart from* every empty container, which in one
  73-scheme `schemes.json` was 73 spurious hunks — a 2-line-per-scheme diff swamping the 8 lines
  you meant to change. Harmless to MGS, but it makes the change unreviewable in git and hides
  mistakes. For a **small, surgical** edit (recolor N roles, flip a flag) **edit the member as
  text**: regex the specific block, splice the new values, then `json.loads` both versions and
  assert the parsed diff is exactly the fields you intended. That gives a diff of literally the
  bytes you changed *and* a stronger correctness check than the dump path. Reserve the
  dump path for structural edits where the diff is large anyway.
  - Floats are full-precision doubles (`0.8313725590705872` = `212/255`), but exact 0 and 1
    serialize as bare `0` / `1` — match that or the diff grows.
  - Verify a repack didn't disturb assets by comparing member-by-member against `git show
    HEAD:<zip>`: same name set, and only the members you touched differing by sha1. Total zip
    *size* is not a signal — `ZIP_DEFLATED`'s default level differs from MGS's, so a
    content-identical repack of a 3.2 MB design came out 18 KB smaller.
- **Editing the zip retriggers MCC autosave.** The mtime change makes MPLAB rewrite
  `<proj>/<cfg>/mcc/mcc-manifest-autosave.yml` with a fresh `creation_date`. It shows up as an
  unexpected modified file in `git status`; it's timestamp-only and safe to ignore or discard.
- **"Unused by widget" ≠ unused — hand source references assets by generated symbol name.**
  An audit that only checks widget uuid refs will call logos, LED indicators and icons unused
  when a hand-built titlebar or chrome module draws them from C. Always intersect with a grep of
  hand source for the asset's `outputName` (excluding the generated tree) before deleting.
  Observed ratio in one project: 35 of 47 images had no widget ref, but 5 of those were live
  from C.
- **Grep hand source EXCLUDING the generated configuration tree** when checking code references
  — it declares every symbol, so including it makes everything look referenced. The tree is the
  design zip's own directory (`src/config/<cfg>/`); derive it from the zip path rather than
  assuming the configuration is named `default` (`mgs_zip.hand_source_files` does this).
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

`scripts/audit_refs.py <zip>` automates the structural half of this list (all four asset kinds,
manifests vs directories, binding integrity, duplicate names) and exits non-zero, so it can gate
a repack. The judgement items — code-referenced names, appearance — still need you.

- [ ] every scheme-uuid in screen/state JSON resolves to a surviving scheme
- [ ] scheme `id`s unique; scheme `name`s unique
- [ ] no code-referenced name deleted without a source-rename patch
- [ ] `userDefault` uuid still resolves
- [ ] zip `testzip()` passes after repack
- [ ] **asset manifests agree with asset directories** — `images.json` ids == `assets/images/*/`
      dirs, `fonts.json` ids == `assets/fonts/*/` dirs (a delete must update both)
- [ ] no widget references an image/font whose asset directory is gone
- [ ] `stringtable.json`: every `bindings[].string` **and `bindings[].font`** resolves to a
      surviving asset; string `id`s and `name`s unique
- [ ] **no string without a binding** (it would generate with no font, and no glyph guarantee)
- [ ] every widget asset-uuid of *every* kind resolves — not just schemes: one regex over the
      screen/state JSON for `"type":"(string|image|font|scheme)","value":"{uuid}"` against the
      four manifests catches a half-finished delete that a scheme-only check passes
