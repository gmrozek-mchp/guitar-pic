# How much of guitar-pic was written by an AI agent — a measured audit

> **SNAPSHOT: 2026-08-07, at commit [`6da9caf`](https://github.com/gmrozek-mchp/guitar-pic/commit/6da9caf), branch `crunchtime`.**
>
> **Every number on this page is a point-in-time measurement and is already out of date.**
> The repo gains code, docs and commits most days. Do not quote these figures as current —
> re-run [§7 Reproducing this audit](#7-reproducing-this-audit) against `HEAD` and, if you
> need the new numbers to persist, add a fresh dated row to [§8](#8-snapshot-history) rather
> than editing the ones below.

Context: nearly all application-level source, tooling, and documentation in this repo was
written and maintained by an AI coding agent (Claude, via Claude Code) working alongside a
developer. This document exists because "virtually zero human hand coding" is a claim, and a
claim about scale deserves counted evidence rather than an impression.

The figures here are the source for the **BUILT WITH AI** story card on marvin's *System
Info* screen ([`screen_system.c`](../firmware/marvin/default/src/ui/screens/system/screen_system.c),
the `guitar-pic` entry's `story` / `story_kicker` / `story_list` fields). On-screen values are
rounded **down** to the nearest thousand — 41,000 / 19,000 / 3,400 / 23,000 — because a
headline row on that card holds 51 monospaced characters and has no room for precision.

### What this audit can and cannot establish

It measures **scale**: ~88,000 lines of authored source and prose, produced over 626 commits
and 110 recorded sessions in six months, across four silicon families and three toolchains.
Every row below reproduces from the recipe in §7.

It does **not** apportion those lines between the two hands. Git has a single author for all
626 commits, and the `Co-Authored-By: Claude` trailer marks agent involvement without
attributing lines, so **no measurement here distinguishes human-typed source from
agent-written source.** The developer's account is that fewer than ~100 lines were ever
hand-typed; that figure is testimony, not a count, and is deliberately kept off the System
Info card, whose lead-in ("*This is what it wrote*") presents the magnitudes as evidence and
leaves the reader to draw the conclusion.

---

## 1. What is counted, and what is not

The interesting quantity is **authored** source — the code a person or an agent had to think
about. That is a minority of the bytes in this repo: most of the C here is vendor code the
silicon or the graphics library brings with it. So every count below excludes any path
matching:

```
/config/default/        MCC (MPLAB Code Configurator) / Harmony generated output
/packs/                 device family packs — register headers, startup, linker
/mcc_generated_files/    MCC Melody output (dsPIC / XC-DSC projects)
/le_gen_                Legato / MGS design generator output
/CMSIS/                 ARM CMSIS core headers
/FreeRTOS/              FreeRTOS kernel
third_party/            vendored libraries (oa-tc6-lib, embedded-cli, …)
```

Two consequences worth stating plainly:

- **`.specstory/` transcripts are excluded from the documentation totals.** They are raw chat
  logs, counted separately in §6 as a measure of process volume, not of authored artefact.
- **MCC configuration itself is authored work that this audit does not capture.** Choosing a
  clock tree, a DMA channel, a pin mux is real design effort; it lands as generated C, which
  the exclusion list throws away. The code totals therefore *understate* the design work, in
  both the human and the agent columns.

### The scale of the exclusion

| C/H in the repo at `6da9caf` | Lines |
| --- | --- |
| Total, everything | 577,905 |
| Vendor / generated (excluded) | 536,695 (**92.9 %**) |
| **Authored (counted below)** | **41,210 (7.1 %)** |

---

## 2. Application firmware — C

**41,116 lines across 241 files, seven boards.** (A further 94 lines of host-side C live in
`tools/`; see §3.)

| Board | Role | Silicon | Lines | Files |
| --- | --- | --- | ---: | ---: |
| [marvin](../firmware/marvin/docs/spec.md) | the brain — vision, game logic, UI | SAM9X75 (ARM926) | 28,752 | 141 |
| [fauxmote](../firmware/fauxmote/SPEC.md) | the stand-in — emulates the controller | ESP32 | 2,817 | 26 |
| [fretboard](../firmware/fretboard/SPEC.md) | the eyes — phototransistor detector | PIC32CM PL10 (Cortex-M0+) | 2,643 | 20 |
| [beatbox](../firmware/beatbox/SPEC.md) | the ears — audio FFT beat detection | dsPIC33AK (DSC) | 2,387 | 20 |
| [lemmy](../firmware/lemmy/SPEC.md) | the body — servo puppet animation | PIC32CM PL10 (Cortex-M0+) | 1,991 | 14 |
| [lightshow](../firmware/lightshow/SPEC.md) | the lights — addressable LED strands | PIC32CM PL10 (Cortex-M0+) | 1,572 | 12 |
| [guitar](../firmware/guitar/SPEC.md) | the hands — open-drain button actuator | PIC32CM PL10 (Cortex-M0+) | 954 | 8 |
| **Total** | | | **41,116** | **241** |

**4 silicon families, 3 toolchains** — XC32 (marvin + the four PL10 nodes), XC-DSC (beatbox),
ESP-IDF (fauxmote). Every board is bare-metal or FreeRTOS with **static allocation only**; no
`malloc` anywhere in authored firmware.

---

## 3. Host-side tooling — Python

**19,315 lines across 95 files in `tools/`**, plus 5,053 lines elsewhere.

These are largely how the agent checks its own work: it cannot see the panel or probe the
bus, so algorithms are proven offline against captured data before the equivalent C is
written for the MCU.

| Tool | What it does | Lines | Files |
| --- | --- | ---: | ---: |
| [`marvin-perf`](../tools/marvin-perf/) | ingests marvin's perf-log stream; latency/timing analysis | 7,574 | 30 |
| [`gameplay`](../tools/gameplay/docs/journal.md) | offline GH3 game-state observer/controller prototype | 5,355 | 36 |
| [`fret-tuner`](../tools/fret-tuner/SPEC.md) | fretboard detector threshold/gain tuning against captures | 3,231 | 10 |
| [`edge-ai`](../tools/edge-ai/docs/journal.md) | distils marvin's play into a network that fits 64 KB of flash | 2,233 | 15 |
| [`gh3-cover-art`](../tools/gh3-cover-art/) | song cover-art asset pipeline | 581 | 2 |
| [`node-photos`](../tools/node-photos/) | node photography → Legato image assets | 177 | 1 |
| [`qr-verify`](../tools/qr-verify/) | decode-checks QR literals read out of the firmware source | 164 | 1 |
| **Subtotal, `tools/`** | | **19,315** | **95** |
| MGS design skill — **broken out in [§4](#4-the-mgs-design-skill--tooling-the-agent-built-for-itself)** | scripted editing of the Legato design database | 2,675 | 17 |
| Subproject-local scripts | `firmware/marvin/docs/design_*.py` + `mockup_typography.py` (1,319), beatbox puppet GUI (685), marvin NAND/QSPI consoles (252), fretboard `ds_monitor.py` (122) | 2,378 | 12 |
| **Total** | | **24,368** | **124** |

Note that `qr-verify` and `edge-ai`'s host tests are the pattern in miniature: the agent wrote
the checker as well as the thing being checked.

---

## 4. The MGS design skill — tooling the agent built for itself

Worth separating from §3, because it is a different *kind* of authorship. The other Python is
tooling for the project. This is tooling the agent wrote **for its own use**, to make an
authoring-tool database tractable at all — packaged as a reusable Claude Code skill at
[`.claude/skills/mgs-legato-design/`](../.claude/skills/mgs-legato-design/), then extended
every time it learned something new.

### The problem it exists to solve

marvin's UI is built with **MGS (MPLAB Graphics Suite) / Legato**. The design lives in
`default_design.zip` — a zip of large JSON members — and MGS's *Generate* turns it into the
`le_gen_*.c/.h` sources the firmware compiles. Two facts make this hostile to an agent:

1. **The zip is the only editable surface.** Per [`CLAUDE.md`](../CLAUDE.md), generated
   `le_gen_*` output is off-limits, so schemes, strings, fonts, images and widget trees can
   only be changed in the database.
2. **The agent cannot click.** MGS Composer is a GUI. Bulk work the UI makes tedious — rename
   40 schemes, merge duplicates, standardize a palette to Tailwind, retarget a font binding —
   is only reachable by transforming the JSON directly.

So the agent wrote the missing CLI. The workflow it settled on is a deliberately split
round-trip: **the agent edits the zip; the human opens MGS → Generate → build.** Neither half
can verify the other, which is why so much of the skill is auditing.

### What it consists of

**3,396 lines total: 2,675 lines of Python across 17 scripts, plus 721 lines of prose.**

| Group | Scripts | Lines |
| --- | --- | ---: |
| Core round-trip | `mgs_zip.py` — load member, repack with backup + a `drop` set, verify zip integrity | 173 |
| Audit (read-only) | `audit_refs`, `audit_schemes`, `audit_strings_fonts`, `audit_widget_strings`, `audit_glyph_coverage` | 800 |
| Add assets | `add_scheme`, `add_image`, `add_layer`, `add_string`, `add_font_range` | 870 |
| Mutate / prune | `set_scheme_color`, `set_image_source`, `rename_images`, `prune_unused_images`, `strip_subtree` | 664 |
| Export | `export_assets` | 168 |
| **Python total** | **17 scripts** | **2,675** |
| Prose | [`SKILL.md`](../.claude/skills/mgs-legato-design/SKILL.md) 236 + [`REFERENCE.md`](../.claude/skills/mgs-legato-design/REFERENCE.md) 485 | **721** |
| **Total** | | **3,396** |

Note the shape: **800 lines of read-only auditing against 1,534 lines that actually change
anything.** `audit_refs.py` is designed to run *after* a transform and exit non-zero if any
widget asset-uuid, manifest entry or string binding no longer resolves — the failure mode
where the design still opens in Composer but Generate emits a reference to something gone.

### It accreted in three days

The growth curve is the point. Every step is a commit that shipped real design work and left
behind what it learned:

| Date | Commit | `SKILL.md` | `REFERENCE.md` | Scripts |
| --- | --- | ---: | ---: | ---: |
| 2026-08-04 | `fe635ca` | 60 | 117 | 2 |
| 2026-08-05 | `37fcc0d` | 87 | 206 | 5 |
| 2026-08-05 | `fbd9ea4` | 102 | 286 | 6 |
| 2026-08-05 | `81f510a` | 117 | 320 | 9 |
| 2026-08-05 | `8463d62` | 124 | 348 | 9 |
| 2026-08-05 | `453d852` | 137 | 419 | 13 |
| 2026-08-05 | `d0ccd37` | 154 | 433 | 14 |
| 2026-08-05 | `96a0c46` | 161 | 433 | 14 |
| 2026-08-06 | `60a0825` | 176 | 453 | 15 |
| 2026-08-06 | `0d7b6af` | 216 | 485 | 16 |
| 2026-08-06 | `c01786b` | 236 | 485 | 17 |

**15 commits over three days**, 2 scripts → 17, 177 lines of prose → 721. This is the clearest
single example in the repo of the journal mechanism working on tooling rather than firmware:
`CLAUDE.md` instructs the agent to update the skill whenever it hits a new gotcha, so each
debugging session ends by writing down what would otherwise have to be rediscovered.

### What is actually written down in it

The prose is not API documentation — it is a list of failure modes that cost real time, each
one now cheap. A representative sample, all of which are lessons rather than reference:

- **Assets are referenced by uuid, not name.** Renaming is therefore free; deleting or merging
  requires repointing every referencing uuid to a survivor.
- **Generated C symbol names come from the asset `name`,** so renaming an asset that hand
  source uses (`&SCHEME_FOO`) breaks the build while the design stays valid. The grep for real
  dependencies must *exclude* the generated tree, which declares everything.
- **Splice the member text; never round-trip the JSON.** MGS's serializer is not Python's — no
  `indent`/`separators`/`sort_keys` combination reproduces it (`json.dumps(indent=2)` of
  marvin's `schemes.json` is 595 KB against the original's 810 KB). Re-dumping buries a
  four-value change in a whole-file diff and destroys the ability to prove nothing else moved.
- **Which colour slot a widget reads depends on its type *and* state.** A panel fills from
  `base`, but the classic button skin fills from `background` while pressed — and Legato's
  default `background` is white. A scheme authored for panels looks perfect until a button
  lands on it, then flashes white on every touch, with nothing wrong in the design and nothing
  visible in a static screenshot. (This is the "button flashing white" bug the old System Info
  story card used to name.)
- **Remember RGB565.** `#E4002B` reads back as `#E60029` in `le_gen_scheme.c` — the correct
  nearest value in 5/6/5, not a mis-set slot.
- **Glyphs: design-time is automatic, runtime is not.** MGS auto-adds glyphs for *bound design
  strings* on Generate, but text produced at runtime (a C literal, a CSV/QSPI data file) is
  invisible to it and renders blank with no build error — and any glyph that happens to work is
  on loan from an unrelated caption until that caption is retargeted. `add_font_range.py` makes
  the glyph a property of the font instead.
- **Deleting widgets can delete a widget *type*.** `legato_config.h`'s
  `LE_<TYPE>_WIDGET_ENABLED` flags derive from what the design instantiates, so removing the
  last design widget of a type compiles that type out — `leXWidget` becomes an unknown type
  name at the next Generate, in code that built minutes earlier.
- **The split round-trip is not atomic, and every failure is silent.** A Generate that lands
  between two edits produces a half-applied design that builds clean and looks almost right;
  MGS holding the design open will clobber a tool edit on its next save; a zip newer than
  `le_gen_*` means the Generate predates the edit.
- **The backup is not an undo.** `mgs_zip.repack(backup=True)` only writes `<zip>.bak` if one
  does not already exist, so on every edit after the first it silently makes none. For a
  tracked zip the real safety net is `git checkout -- <zip>`.

Each script also has a **dry-run default**, and the audit scripts are non-destructive by
construction — a shape the agent converged on after the round-trip's silent failure modes
became clear, rather than something specified up front.

---

## 5. Documentation, specs and journals

**22,989 lines across 85 Markdown files** (excluding `.specstory/`).

| Category | Lines |
| --- | ---: |
| Working journals (10 files) | 5,437 |
| Specs (`SPEC.md`, `spec.md`, subproject specs) | ~2,400 |
| Everything else — design docs, protocol specs, doc maps, skill docs, READMEs | remainder |
| **Total** | **22,989** |

Journal breakdown, since the journals are the agent's working memory across sessions and
compactions — the mechanism that makes the rest of this possible:

| Journal | Lines |
| --- | ---: |
| [`firmware/marvin/docs/journal.md`](../firmware/marvin/docs/journal.md) | 2,306 |
| [`tools/gameplay/docs/journal.md`](../tools/gameplay/docs/journal.md) | 896 |
| [`firmware/beatbox/docs/journal.md`](../firmware/beatbox/docs/journal.md) | 627 |
| [`firmware/fauxmote/docs/journal.md`](../firmware/fauxmote/docs/journal.md) | 536 |
| [`firmware/fretboard/docs/journal.md`](../firmware/fretboard/docs/journal.md) | 322 |
| [`firmware/lemmy/docs/journal.md`](../firmware/lemmy/docs/journal.md) | 232 |
| [`tools/edge-ai/docs/journal.md`](../tools/edge-ai/docs/journal.md) | 220 |
| [`firmware/lightshow/docs/journal.md`](../firmware/lightshow/docs/journal.md) | 150 |
| [`firmware/guitar/docs/journal.md`](../firmware/guitar/docs/journal.md) | 148 |
| **Total** | **5,437** |

---

## 6. Process — commits and sessions

| Measure | Value |
| --- | --- |
| Commits | **626** |
| Span | 2026-02-04 → 2026-08-07 (**six months**) |
| Distinct git authors | 1 (Greg Mrozek) |
| Commits carrying `Co-Authored-By: Claude` | 464 |
| Recorded agent sessions (`.specstory/history/`) | **110** |
| Transcript volume | 1,116,135 lines / ~6.9 M words |
| Tracked files in repo | 2,401 |

**Both process counts are floors, not measurements — read them that way:**

- **The 464 co-author trailers undercount.** The trailer first appears in **May 2026**; the
  four months of commits before that carry none regardless of how they were produced. Of the
  576 commits from May onward, 464 (80.5 %) carry it.
- **The 110 sessions undercount.** `.specstory` coverage runs Jan–Jul 2026; August sessions
  had not been flushed at snapshot time. Per-month files: Jan 2, Feb 8, Mar 9, Apr 4, May 11,
  Jun 46, Jul 30.
- **The ~6.9 M transcript word count is inflated** and is deliberately *not* quoted on the
  System Info card. `wc -w` over the transcripts counts echoed file contents and tool output
  alongside actual conversation, so it measures log volume, not dialogue.

Git authorship cannot settle the human-versus-agent question on its own: there is one
committer, and the agent's contribution is a trailer rather than an author field. The
defensible evidence is the *combination* — 626 commits and 110 recorded sessions producing
~88,000 lines of authored source and prose in six months, with the session transcripts and
journals showing the work being done in dialogue.

---

## 7. Reproducing this audit

Run from the repo root. Substitute a different ref for `HEAD` to audit a past snapshot.

```bash
REF=HEAD
EX='/config/default/|/packs/|/third_party/|/mcc_generated_files/|/le_gen_|/CMSIS/|third_party/|/FreeRTOS/'

# line count for a list of paths at $REF, read from stdin
lc() { local t=0; while read -r f; do t=$((t + $(git show "$REF:$f" | wc -l))); done; echo "$t"; }
at()  { git ls-tree -r --name-only "$REF"; }

# §2 authored C, and the vendor share it is carved out of
at | grep -E '\.(c|h)$' | grep -vE "$EX" | lc      # authored
at | grep -E '\.(c|h)$'                    | lc      # everything

# §3 Python
at | grep -E '^tools/.*\.py$'              | lc
at | grep -E '\.py$' | grep -v '^tools/'   | lc

# §4 the MGS skill — code, prose, and its growth curve
S=.claude/skills/mgs-legato-design
at | grep -E "^$S/scripts/.*\.py$" | lc
at | grep -E "^$S/.*\.md$"         | lc
git log --oneline "$REF" -- "$S/" | wc -l
for h in $(git log --format='%h' --reverse "$REF" -- "$S/SKILL.md"); do
  printf '%s %s SKILL=%s REF=%s scripts=%s\n' \
    "$(git log -1 --format='%ad' --date=short $h)" "$h" \
    "$(git show "$h:$S/SKILL.md"     | wc -l)" \
    "$(git show "$h:$S/REFERENCE.md" | wc -l)" \
    "$(git ls-tree -r --name-only $h -- "$S/scripts/" | grep -c '\.py$')"
done

# §5 Markdown, excluding chat transcripts
at | grep -E '\.md$' | grep -v '^.specstory/' | lc
at | grep -E 'journal\.md$'                   | lc

# §6 process
git rev-list --count "$REF"
git log "$REF" --format='%H%n%b' | grep -ci 'Co-Authored-By: Claude'
at | grep -c '^.specstory/history/.*\.md$'
```

Per-board and per-tool tables are the same pipelines narrowed with a path prefix
(`grep '^firmware/marvin/'`, `grep '^tools/gameplay/'`, …).

Two things to preserve if you re-run it:

1. **Keep the exclusion list identical**, or the numbers are not comparable across snapshots.
   If a new vendor tree lands under a path the list misses, add it here and re-run *every*
   row rather than patching one.
2. **Anchor to a commit, not the working tree.** Uncommitted edits — including edits to the
   System Info story card that this audit feeds — shift the counts by a line or two and make
   the snapshot unreproducible.

---

## 8. Snapshot history

| Date | Commit | Authored C | Python | Markdown | MGS skill (py/md/scripts) | Commits | Sessions |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2026-08-07 | `6da9caf` | 41,210 | 24,368 | 22,989 | 2,675 / 721 / 17 | 626 | 110 |

---

## See also

- [`SPEC.md`](../SPEC.md) — cross-subproject system overview
- [`CLAUDE.md`](../CLAUDE.md) — the agent's standing instructions for this repo: journal
  discipline, the "do not edit MCC output" rule, static-allocation-only, comment policy
- [`firmware/marvin/docs/journal.md`](../firmware/marvin/docs/journal.md) — the session-log
  entry for this audit, and the largest worked example of the journal mechanism
