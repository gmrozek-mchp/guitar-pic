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

Everything you'd edit programmatically is JSON. Preserve all other members verbatim on repack
(the font/image blobs are large — don't touch them).

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

## Gotchas (learned the hard way)

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
