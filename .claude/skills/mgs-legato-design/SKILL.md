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
python3 .claude/skills/mgs-legato-design/scripts/audit_schemes.py \
        firmware/marvin/default/src/config/default/default_design.zip \
        firmware/marvin/default/src
```

## Details

Zip anatomy, `schemes.json` structure (16 color fields + `colorMode` enum), how widgets
reference assets, the generated-C relationship, and the full gotcha list are in
[REFERENCE.md](REFERENCE.md). The scripts in [scripts/](scripts/) are the reusable core —
`mgs_zip.py` (load member / repack+backup / validate refs) and `audit_schemes.py`.
