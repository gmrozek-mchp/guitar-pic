---
name: mgs-legato-design
description: Audit and programmatically edit an MGS (MPLAB Graphics Suite) / Legato design database (default_design.zip) — schemes/colors, strings, widgets, palette, fonts, images — by transforming its JSON in place, then regenerating with MGS. Use when bulk-editing or cleaning up Legato schemes/colors, renaming/deduping/deleting design assets, standardizing a palette (e.g. to Tailwind), or otherwise scripting changes to a *_design.zip / MGS Composer project instead of clicking through the UI. Handles the round-trip safely: backup, uuid-reference validation, and C build-safety.
---

# Editing an MGS / Legato design.zip

`default_design.zip` (under `<proj>/default/src/config/default/`) is the
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
   is valid. Grep hand source (EXCLUDING the generated `config/default/` tree — it declares
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

```bash
S=.claude/skills/mgs-legato-design/scripts
Z=firmware/marvin/default/src/config/default/default_design.zip
python3 $S/audit_schemes.py       $Z firmware/marvin/default/src   # schemes
python3 $S/audit_strings_fonts.py $Z firmware/marvin/default/src   # strings + fonts
python3 $S/audit_widget_strings.py $Z --dupes                      # widget → string → font
python3 $S/audit_glyph_coverage.py $Z firmware/marvin/default/src \
                                      firmware/marvin/data         # glyph coverage
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

## Replacing an imported screen with hand-written C

When a figma-imported screen is being rebuilt programmatically (marvin's `screen_bus.c` /
`screen_wiimotes.c`), the design should keep only the **empty root panel** the builder attaches
to. `scripts/strip_subtree.py <zip> <PANEL_NAME> [--layer NAME]` deletes that panel's children
and reports what the deletion orphans.

The counter-intuitive part: **keep the deleted widgets' strings and drive them from C with
`leTableString` + `stringID_*`** rather than switching to C literals. Imported captions are
often non-ASCII — d-pad arrows ▲ ◀ ▶ ▼, a true minus U+2212 — and per the glyph rule above MGS
only auto-includes glyphs for strings it can see in the design. A C literal would render blank
after the next Generate with no build error. Full recipe (plus the `PanelAA_Enable` opaque-parent
precondition and why a widget+scheme can't express a two-tone fill) in
[REFERENCE.md](REFERENCE.md).

## Details

Zip anatomy, `schemes.json` structure (16 color fields + `colorMode` enum), `stringtable.json`
+ font-asset structure, how widgets reference assets, the generated-C relationship, and the
full gotcha list are in [REFERENCE.md](REFERENCE.md). The scripts in [scripts/](scripts/) are
the reusable core — `mgs_zip.py` (load member / repack+backup with a `drop` set / validate
refs), `audit_schemes.py`, `audit_strings_fonts.py`, `audit_widget_strings.py`,
`audit_glyph_coverage.py`, `strip_subtree.py`.
