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
