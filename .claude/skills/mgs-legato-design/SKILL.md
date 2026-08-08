---
name: mgs-legato-design
description: Audit and programmatically edit an MGS (MPLAB Graphics Suite) / Legato design database (default_design.zip) — schemes/colors, strings, widgets, palette, fonts, images — by transforming its JSON in place, then regenerating with MGS. Use when bulk-editing or cleaning up Legato schemes/colors, renaming/deduping/deleting design assets, standardizing a palette (e.g. to Tailwind), or otherwise scripting changes to a *_design.zip / MGS Composer project instead of clicking through the UI. Handles the round-trip safely: backup, uuid-reference validation, and C build-safety.
---

# Editing an MGS / Legato design.zip

`<name>_design.zip` (under `<proj>/<cfg>/src/config/<cfg>/`, typically `default`) is the
**MGS (MPLAB Graphics Suite) / Legato project database** — a zip of JSON files. MGS "Generate" turns it into the
`le_gen_*.c/.h` C sources the firmware compiles. Editing the JSON directly + regenerating
lets you make **bulk, scripted** changes the Composer UI makes tedious (rename 40 schemes,
merge duplicates, standardize a palette, retarget strings…).

This is an authoring-tool database, not hand source — treat edits with care. Follow the
workflow below; it is a **proven round-trip** (MGS reopens and regenerates from a
tool-edited zip, then re-serializes it on save).

## Golden rules

1. **Back up the zip first** — and know that `mgs_zip.repack(backup=True)` only writes `<zip>.bak`
   when one does **not** already exist, so on every edit after the first it silently makes none.
   That is deliberate (the first `.bak` is the last known MGS-written state), but it means the
   `.bak` sitting next to the zip may be months old and is **not** your undo. It now returns the
   backup path or `None`; check it. For a tracked zip the real safety net is
   `git checkout -- <zip>`, so confirm the zip is committed clean before editing.
2. **Assets are referenced BY UUID, not name.** Widgets store `{"type":"scheme","value":"{uuid}"}`.
   → **Renaming** an asset (change its `name`, keep its `id`) never disturbs a reference.
   → **Deleting/merging** one requires repointing every referencing uuid to a survivor.
3. **Generated C symbol names come from the asset `name`.** So renaming/deleting an asset
   that **hand-written source** uses (`&SCHEME_FOO`) breaks the C build even though the design
   is valid. Grep hand source (EXCLUDING the generated configuration tree — it declares
   *everything*, causing false positives) to find real deps, and emit a source-rename patch.
4. **Validate before repack:** every widget asset-uuid still resolves to a surviving asset;
   uuids and names are unique.
5. **You edit the zip; the user regenerates.** After repack, the user opens MGS →
   **Generate** (refreshes `le_gen_*`) → build. Only then are name changes reflected in C.
   Three consequences of the two halves not being atomic, all of them silent:
   - **Finish every zip edit before asking for a Generate.** A Generate that runs between two
     of your edits produces a *half-applied* design that builds clean and looks almost right —
     e.g. new pressed fills present, a new pressed border still at the old default.
   - **Verify it landed instead of assuming.** Read the generated value back and compare it to
     the zip: `awk '/leScheme SCHEME_X =/,/^};/' <gen>/le_gen_scheme.c` (or the equivalent for
     strings/images). Timestamps help — a zip newer than `le_gen_*` means the Generate predates
     your edit.
   - **MGS holding the design open will clobber a tool edit** on its next save, since it writes
     its in-memory copy. Ask whether Composer is open *before* editing, not after.
   - **A script reporting `applied` is not evidence the edit is in the zip.** Each script
     verifies the member it built and then repacks; neither can see a Composer save that lands
     afterwards. Observed: `add_image.py` printed `manifest 26 -> 27 entries / applied` and
     `add_scheme.py` printed `verified: 74 -> 75 schemes`, while the zip on disk still held 26
     and 74 — Composer had been open and re-saved over both. **Always finish with
     `audit_refs.py` and read its asset counts**, which is the cheap tell: the counts come from
     the zip as it now exists on disk, so they disagree with the scripts' output exactly when
     something clobbered you. Recovery is `git checkout -- <zip>` then re-apply.
     - The clobbered zip can look byte-identical in content while `git status` still reports it
       modified: Composer re-serializes the container (entry order, timestamps, compression) with
       every member unchanged. Diff *members*, not the file, before assuming the user lost work —
       if all members match HEAD, resetting is free.
     - Note the member counts are also the only check that catches this. Sizes don't: a re-saved
       zip can match HEAD's size to the byte.

## Workflow

1. **Inspect (read-only):** `python3 scripts/audit_schemes.py <zip> [hand-src-dir]`
   — counts, per-scheme role+hex, duplicates, unused, colorMode distribution, code refs.
2. **Plan** the transform: rename / merge-dupes / delete-unused / add / recolor / set colorMode.
3. **Dry-run** the transform in a script; print the before→after mapping and validate refs.
4. **Backup + repack** with `scripts/mgs_zip.py` (replaces named members, preserves fonts/
   images, backs up, verifies zip integrity).
5. **Patch hand source** for any renamed/deleted C symbols (only after step 3 confirms which).
6. **Hand off:** user opens MGS → Generate → build → verify. If bad, restore the backup.

Stage large jobs so each step is verifiable: e.g. *prune+dedupe+colorMode* (appearance- and
source-neutral) as one commit, then *renames* (with the source patch) as a second.

## Quick start

Set `Z` to the design zip and `SRC` to the project's source root — the directory holding both
your hand-written code and the generated configuration tree. The scripts locate that generated
tree from the zip's own directory, so the Harmony configuration needn't be named `default`.
`DATA` is optional: any directory of runtime data files (CSV, JSON) whose text reaches a label,
for the glyph audit.

```bash
S=.claude/skills/mgs-legato-design/scripts
Z=<proj>/<cfg>/src/config/<cfg>/<name>_design.zip
SRC=<proj>/<cfg>/src
python3 $S/audit_refs.py           $Z               # referential integrity (run after ANY edit)
python3 $S/audit_schemes.py        $Z $SRC          # schemes
python3 $S/audit_strings_fonts.py  $Z $SRC          # strings + fonts
python3 $S/audit_widget_strings.py $Z --dupes       # widget → string → font
python3 $S/audit_glyph_coverage.py $Z $SRC [$DATA]  # glyph coverage
```

`audit_refs.py` is the one to run **after** a transform, not just before: it exits non-zero if any
widget asset-uuid, manifest entry or string binding no longer resolves, which is the failure mode
where the design still opens in Composer but Generate emits a reference to something that is gone.

## Schemes, specifically

`set_scheme_color.py <zip> SCHEME_NAME:slot=#RRGGBB [...]` sets any of the 16 colour slots on
existing schemes (dry-run by default). Two things it encodes that are worth knowing even if you
write your own transform:

- **Splice the member text; do not round-trip the JSON.** MGS's serializer is not Python's — no
  `indent`/`separators`/`sort_keys` combination reproduces it (a plain `json.dumps(indent=2)`
  of marvin's `schemes.json` is 595 KB against the original's 810 KB). Re-dumping therefore
  rewrites the entire member and buries a four-value change in a whole-file diff, which also
  destroys your ability to prove you changed nothing else. This applies to **every** member, not
  just `screen.json` in `add_layer.py`. Then reparse the result and assert the set of differing
  fields is exactly the set you asked for: a regex edit to 800 KB of JSON should never be trusted
  because "the regex matched".
- **Which slot a widget reads depends on its TYPE and STATE.** A plain panel/widget only ever
  fills from `base` (`leWidget_SkinClassic_DrawStandardBackground`), but the classic **button**
  skin fills from `background` while pressed and `base` while up (`drawBackground` in
  `legato_widget_button_skin_classic.c`, and the string's lookup-table colour follows the same
  swap). Legato's default `background` is **white**, so a FILL scheme authored only for panels
  looks perfect until someone puts a button on it — and then it flashes white on every touch,
  with nothing wrong in the design and nothing to see in a static screenshot. When adding a
  button scheme, set `background` deliberately; the natural value is one step lighter than
  `base`, which is what a CSS `hover:` step means in an imported mockup.

**Adding one:** `add_scheme.py <zip> <NEW_NAME> --like <SIBLING> [slot=#RRGGBB …]` clones an
existing scheme under a fresh uuid and recolours the slots you name (dry-run by default; uuid5
from the name, so re-running is reproducible). Clone rather than synthesize, for `add_layer.py`'s
reason: a scheme carries ~37 properties — 16 colour slots, `colorMode`, two mono tables, editor
category state — whose defaults appear nowhere else in the zip. Pick the sibling by **role**, and
note it must not be the last element of `schemes[]` (the splice inserts after it).

**Which slots to set is the decision, and it follows the reading widget, not the colour.** An
accent used for both text and small filled shapes needs `base`, `foreground` **and** `text` all
carrying it: a panel fills from `base`, a label reads `text`. Set only one and the other half of
the card comes out the sibling's colour. This is also why the stock `SCHEME_TEXT_*` schemes
cannot be reused as accents — they carry the colour in `text` alone and fill grey, so a widget
filled from one gets a grey edge with no error anywhere. Verified in marvin: of 73 schemes, only
the seven `SCHEME_NODE_*` had `base == foreground == text`, so an eighth accent needed a new
scheme rather than a reuse.

**Remember RGB565.** The generated C quantizes to the canvas's colour mode, so `#E4002B` reads
back as `#E60029` in `le_gen_scheme.c` — that is the correct nearest value in 5/6/5, not a
mis-set slot. Compare against the quantized target before concluding a colour didn't land.

## Strings and fonts, specifically

Two things trip people up here, both covered in [REFERENCE.md](REFERENCE.md):

- **A font is bound per *string*, not per widget** (`stringtable.json` `bindings[]`). "Change
  this label's font" means retargeting that string's binding — and two labels sharing a string
  can't differ, which is why Figma imports breed near-duplicate strings.
- **Font names lie.** MGS substitutes a face it can't find and keeps the requested name, so
  group fonts by `sha1(sourceData)` and read the TTF `name` table before trusting any name.
  Real flash cost is the generated `le_gen_fonts.c` arrays, not the shared TTF blob.
- **Adding a string means TWO lists.** `add_string.py <zip> <NAME> <value> --font <FontName>`
  appends to `strings[]` (identity + one value per language) *and* `bindings[]` (one
  `{string, language, font}` per language). An unbound string generates a `leTableString` with no
  font, and the binding is also what makes MGS include the value's glyphs — so the font choice
  belongs in this step. A string needs no widget reference to be generated, which is what lets a
  hand-built screen own its captions while the design still owns the text.
- **Glyphs: design-time is automatic, runtime is not.** MGS auto-adds whatever glyphs the
  *bound design strings* need on Generate — so retargeting a string to a font missing its
  special characters self-heals. But text produced at **runtime** (`setString` from a C
  literal, a CSV / QSPI data file, a formatted device name) is invisible to MGS, and renders as a
  missing glyph with no build error. Until a design string happens to contain the same
  character, that glyph is on loan: it disappears the day that unrelated string is retargeted or
  pruned. **Declare it instead** — `add_font_range.py <zip> <FontName> U+2605` writes the font's
  `ranges.json`, making the glyph a property of the font rather than a side effect of someone
  else's caption. (This supersedes the earlier advice to add such characters by hand in MGS.)
  Run `audit_glyph_coverage.py` to check both sides, but know its blind spot: it greps hand
  source for non-ASCII *literals*, so a character assembled byte-by-byte (`\xE2\x98\x85`) reads
  as pure ASCII and passes.

- **Aligning anything to text needs the baseline, which is only in the generated C.** Not the
  zip, not `glyphs.json`. `font_metrics.py <src-dir> [--box H]` reads `le_gen_fonts.c` and
  reports height / baseline / advance / cap height / x-height / descender per font, plus where
  a label box of height `H` puts the baseline. **Align markers to the x-height middle**
  (`baseline - xHeight/2`), not to the row box and not to the cap-height middle — text mass
  sits at the x-height, so both other choices read visibly high. Full parse, the worked
  2px example, and the box-too-short clipping rule in [REFERENCE.md](REFERENCE.md).

Before retargeting a font, predict layout damage from the glyph `advance` tables; before
deleting one, check hand source for its symbol and `drop=` its whole
`assets/fonts/{uuid}/` directory.

## Images

Two things you can do without touching Composer, both in [REFERENCE.md](REFERENCE.md):

- **Prune** — `prune_unused_images.py <zip> <src-dir>` deletes images referenced by neither a
  widget nor hand code. Both halves are required: logos and button icons are routinely drawn
  from C with `setImage(w, (leImage *)&NAME)`, so widget-refs alone under-counts, while the
  generated tree declares every symbol so it must be excluded from the grep. Strip comments
  before name-matching, or an asset whose name is also a common word in the codebase (a screen
  name, a product name) looks live off prose hits alone.
- **Add** — `add_image.py <zip> <file.png> <NAME> --like <sibling> [--bind WIDGET:prop,…]`
  writes the three `assets/images/{uuid}/` members plus the `images.json` manifest entry, and
  can repoint widgets at the new asset in the same pass. Clone a sibling's config rather than
  synthesizing it, and remember `rawconfig.json`'s `maskColor.image` is self-referential.
- **Replace the artwork, keep the asset** — `set_image_source.py <zip> NAME=<file.png> […]`
  swaps `sourceData` + the dimensions + `colorCount`, leaving uuid, bindings, memory location,
  format and RLE alone. Use this whenever the *art* changed but the identity shouldn't; nothing
  needs rebinding, because widgets and `setImage` calls both reference the asset, not the file.

  **New artwork of a different size does not move a widget — and that is the trap.** Legato's
  image widget centres: `leWidget_Constructor` sets `LE_HALIGN_CENTER`/`LE_VALIGN_MIDDLE` and
  `_leImageWidget_Constructor` never overrides them, so a smaller image silently re-centres
  inside the unchanged rect and the position in the source stops describing what is on screen.
  **Shrink the widget rect to the image's exact size** — then the arrange offset is zero, the
  widget position *is* the image position, and the layout is explicit. Two things make this
  safe and worth doing: a rect equal to an opaque image fully overdraws its own background, so
  the `backgroundType`/scheme becomes irrelevant; and it is how a correctly-authored logo widget
  already looks, which makes the odd one out easy to spot.

  When you do compute positions, use Legato's real formula —
  `bounds.height/2 - sub.height/2`, **two independent integer divisions**, not `(bounds-sub)/2`.
  They differ by a pixel whenever the parities differ, which is exactly the size of error being
  chased. Check the ink bounding box before assuming the image edge is the visual edge: art with
  transparent padding needs the *bbox* aligned, not the canvas. Anchoring is a design decision
  the resize forces, not a derivable one — a shrunken right-aligned mark either keeps its gutter
  or keeps its centreline, and the widths no longer allow both, so ask.

## Replacing an imported screen with hand-written C

When an imported screen is being rebuilt by a programmatic builder in C, the design should keep
only the **empty root panel** the builder attaches to.
`scripts/strip_subtree.py <zip> <PANEL_NAME> [--layer NAME]` deletes that panel's children and
reports what the deletion orphans. A screen spanning several layers has one root panel per layer,
so strip each by name — the script takes a widget, not a screen.

**A brand-new builder-owned screen needs a layer that doesn't exist yet** —
`scripts/add_layer.py <zip> <SUFFIX> --like <TEMPLATE_SUFFIX>` clones a sibling layer plus its
root panel under fresh uuids (`--like BUS` + `SYSTEM` → layer `SCREEN_SYSTEM`, panel
`PANEL_SYSTEM` → C global `<Screen>_PANEL_SYSTEM`). Clone rather than synthesize: a layer carries
~25 properties (`colorMode`, `renderMode`, `clearMode`, alpha, margins, editor state) whose enum
ordinals appear nowhere in the zip, and a sibling the firmware already renders correctly is the
only trustworthy source for all of them at once. Pick the template by *role* — for a full-screen
base view take another full-screen one (in marvin, `BUS`/`WIIMOTES` are `100x100` `sizeLocked`
with the canvas window set at runtime, while modal layers carry real design sizes). Layer and
panel uuids are contained in `screen.json` alone, so this touches exactly one member; the script
verifies the pre-existing layers and everything outside `layers[]` are byte-identical afterwards.
MGS derives `LE_LAYER_COUNT` from the layer count, so Generate raises it for free — but the
firmware also needs its canvas pool (`CONFIG_CANVAS_NUM_OBJ`) and framebuffer RAM to have room
for the new surface, which the design cannot tell you about.

**Deleting widgets can delete a widget TYPE.** `legato_config.h`'s `LE_<TYPE>_WIDGET_ENABLED`
flags are derived from the types the *design* instantiates, so removing the last design widget of
a type compiles that type out — `leXWidget` becomes an unknown type name at the next Generate, in
code that was building minutes earlier. Before stripping, list what the firmware actually uses
(`grep -rhoE '\ble[A-Z][A-Za-z]*Widget\b' <hand-src>`) and check it against those flags. Two
resolutions, both worth doing: **pin** the types the code needs in the Legato/MGS component so the
design can't take them away, and **prefer a plain `leWidget` plus a paint override** to a
specialised type wherever the code already owns the drawing — a widget whose fill you paint
yourself gains nothing from the stock implementation but a vtable to hijack.

**How to tell a pin is real before you rely on it:** a flag reading `1` proves nothing while the
design still instantiates that type. Look instead for a flag that is `1` with **no** design
instance anywhere — that can only be a pin, and it confirms pins are in effect for this project.
This matters most on the *last* strip: once the design holds no widgets at all, every type the
firmware uses is on loan from a pin, with nothing left in the design to keep any of them alive.

The counter-intuitive part: **keep the deleted widgets' strings and drive them from C with
`leTableString` + `stringID_*`** rather than switching to C literals. First for localization — a
design string has a value per language, a C literal can never be translated. Second for glyphs:
imported captions are often non-ASCII (arrows ▲ ◀ ▶ ▼, a true minus U+2212, ✓/⌫), and per the
glyph rule above MGS only auto-includes glyphs for strings it can see in the design, so a literal
would render blank after the next Generate with no build error. Same reasoning applies to a
caption the design *lacks*: `add_string.py` it (bound to the font its neighbours use) rather than
reaching for a C literal. Full recipe — plus the opaque-parent precondition for AA-rounded child
panels, and why a widget + scheme can't express a two-tone fill — in [REFERENCE.md](REFERENCE.md).

## Details

Zip anatomy, `schemes.json` structure (16 color fields + `colorMode` enum), `stringtable.json`
+ font-asset structure, how widgets reference assets, the generated-C relationship, and the
full gotcha list are in [REFERENCE.md](REFERENCE.md). The scripts in [scripts/](scripts/) are
the reusable core — `mgs_zip.py` (load member / repack+backup with a `drop` set / validate
refs), `audit_refs.py`, `audit_schemes.py`, `audit_strings_fonts.py`, `audit_widget_strings.py`,
`audit_glyph_coverage.py`, `font_metrics.py`, `strip_subtree.py`, `add_layer.py`, `prune_unused_images.py`,
`add_image.py`, `add_string.py`, `add_font_range.py`, `add_scheme.py`, `set_scheme_color.py`, `set_image_source.py`,
`rename_images.py`, `export_assets.py`.

Note that `font_metrics.py` is the one script here that reads **generated output** rather than
the design zip — vertical metrics exist only in `le_gen_fonts.c`. It is read-only, so it does
not breach the "never edit the generated tree" rule.
