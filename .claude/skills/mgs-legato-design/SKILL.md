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

1. **Back up the zip first.** Always keep the pre-edit `.zip` until MGS has reopened + built.
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
python3 $S/audit_schemes.py        $Z $SRC          # schemes
python3 $S/audit_strings_fonts.py  $Z $SRC          # strings + fonts
python3 $S/audit_widget_strings.py $Z --dupes       # widget → string → font
python3 $S/audit_glyph_coverage.py $Z $SRC [$DATA]  # glyph coverage
```

## Strings and fonts, specifically

Two things trip people up here, both covered in [REFERENCE.md](REFERENCE.md):

- **A font is bound per *string*, not per widget** (`stringtable.json` `bindings[]`). "Change
  this label's font" means retargeting that string's binding — and two labels sharing a string
  can't differ, which is why Figma imports breed near-duplicate strings.
- **Font names lie.** MGS substitutes a face it can't find and keeps the requested name, so
  group fonts by `sha1(sourceData)` and read the TTF `name` table before trusting any name.
  Real flash cost is the generated `le_gen_fonts.c` arrays, not the shared TTF blob.
- **Glyphs: design-time is automatic, runtime is not.** MGS auto-adds whatever glyphs the
  *bound design strings* need on Generate — so retargeting a string to a font missing its
  special characters self-heals. But text produced at **runtime** (`setString` from a C
  literal, a CSV / QSPI data file, a formatted device name) is invisible to MGS: those
  characters must be **added to the font's character set by hand in MGS**, or they render as a
  missing glyph with no build error. Run `audit_glyph_coverage.py` to check both sides.

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

## Replacing an imported screen with hand-written C

When an imported screen is being rebuilt by a programmatic builder in C, the design should keep
only the **empty root panel** the builder attaches to.
`scripts/strip_subtree.py <zip> <PANEL_NAME> [--layer NAME]` deletes that panel's children and
reports what the deletion orphans.

The counter-intuitive part: **keep the deleted widgets' strings and drive them from C with
`leTableString` + `stringID_*`** rather than switching to C literals. First for localization — a
design string has a value per language, a C literal can never be translated. Second for glyphs:
imported captions are often non-ASCII (arrows ▲ ◀ ▶ ▼, a true minus U+2212, ✓/⌫), and per the
glyph rule above MGS only auto-includes glyphs for strings it can see in the design, so a literal
would render blank after the next Generate with no build error. Full recipe — plus the
opaque-parent precondition for AA-rounded child panels, and why a widget + scheme can't express a
two-tone fill — in [REFERENCE.md](REFERENCE.md).

## Details

Zip anatomy, `schemes.json` structure (16 color fields + `colorMode` enum), `stringtable.json`
+ font-asset structure, how widgets reference assets, the generated-C relationship, and the
full gotcha list are in [REFERENCE.md](REFERENCE.md). The scripts in [scripts/](scripts/) are
the reusable core — `mgs_zip.py` (load member / repack+backup with a `drop` set / validate
refs), `audit_schemes.py`, `audit_strings_fonts.py`, `audit_widget_strings.py`,
`audit_glyph_coverage.py`, `strip_subtree.py`, `prune_unused_images.py`, `add_image.py`.
