# gameplay — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the GH3
game-state engine prototype. Newest entries at the top. This subproject is the **offline
host-side prototype** for marvin's game-state observer/controller (spec §4.8, M9/M10): we
prove the detection algorithms in Python against the real-screen corpus, then port the
proven, simple GH3-specific logic to a firmware `gameplay_engine` module.

Orienting docs (read alongside this journal):
- `firmware/marvin/docs/spec.md` §4.8 — what the game-state subsystem is.
- `firmware/marvin/docs/gh3_navigation.md` — screen catalog + menu graph (canonical screen names).
- `firmware/marvin/docs/gh3_screens/` — the labelled 720×480 PNG corpus (101 screens, 13 classes).

---

## Current focus

**Slice 3 — song_select reader (done, host-only).** Identify the selected song by matching
the highlight-slot bitmap against the 64 per-song templates (closed-set, not OCR).
- **64/64 songs correct clean, ~99.1% under analog-slop.** Match is against **all 64**
  templates, so the setlist (main/bonus) falls out of the winning song — no dependence on
  the tab read. A small read-time offset search makes the fine slot grid tolerant of the
  positional slop (without it, translate robustness was ~70%).
- The per-setlist first song (Slow Ride / Avalancha) uses a lower ROI; all others the fixed slot.
- Setlist (main/bonus) read from the **page background colour** (yellow vs white) — 100% clean
  and 100% under slop; the song match also yields the setlist, so the two agree.

**Slice 2 — static-list highlight reader (done, host-only).** Within an already-classified
static-list screen, read which menu item is selected. Per-screen menu metadata (ordered
items + menu-band geometry) + a per-cell "deviation from unselected baseline" reader.
Proven against the corpus; not yet ported to firmware.

Result on the 8 fixed-count static-list screens (33 labelled frames):
- **100% clean selection accuracy** (every labelled frame read correctly).
- **~99.4% robustness** across the analog-slop envelope.
- Trivially cheap (per-cell mean colour over a handful of cells + a distance).

**Slice 1 — screen classifier (done, host-only).** Recognize which GH3 screen a captured
frame shows, or report `UNKNOWN`. Coarse fixed-region colour fingerprint + nearest-centroid
+ UNKNOWN reject. Proven against the corpus; not yet ported to firmware.

Result at the chosen default (12×8 grid, 5×5 samples/region, normalized):
- **96.0% leave-one-out accuracy** = 100% of every multi-sample class. The only 4 misses
  are the single-sample classes (`loading`, `in_song`, `section_select`, `tutorials_menu`),
  which by definition have no centroid under LOO — and all four correctly reject as UNKNOWN.
- **~99.8% robustness** across the synthetic analog-slop envelope.
- **~0.1–0.4 ms** estimated per classification on the SAM9X75 (see HW note below).
- Impostor leak (genuinely-unseen screen accepted as a known one): 9/101 — a reported
  limitation, handled in production by the navigator's closed-loop + RED recovery.

---

## Plan (phases)

1. ✅ **Screen classifier** — fingerprint + nearest-centroid + UNKNOWN, eval harness.
2. ✅ **Static-list highlight reader** — read which menu row is selected on the 8
   fixed-count static-list screens. (Deferred within slice 2: `song_select` fixed-slot
   reader, `section_select` variable list.)
3. ✅ **`song_select` reader** — fixed-slot bitmap match of the highlight slot against the 64
   per-song templates + offset search; setlist falls out of the match and is also read
   independently from the page background colour. (Deferred: reading the scrolling neighbour list.)
4. ⬜ **Number/score region readers** — score, multiplier, etc. This is where char/digit
   glyph recognition (open-ended values, no template) actually belongs.
5. ⬜ **Navigator** — plan + execute button sequences along the menu graph, closed-loop on
   the observer (M10).
6. ⬜ **Firmware port** — emit the recognizer metadata (fingerprint centroids/thresholds,
   menu geometry, per-cell baselines) and reimplement in integer C as `gameplay_engine`.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-15 | **Setlist (main/bonus) is read from the page background colour — warmth = R−B over a large background ROI (median, robust to overlaid text) — not from the setlist tabs.** | 100% clean and 100% under slop. The "setlist"/"bonus" tabs are only on screen when the first song is selected — they scroll off for song 2+ (Greg), so a tab ROI is empty for 63/64 frames (that earlier approach was ~75%). The page itself is always visible and differs by setlist: main = yellow parchment, bonus = whiter. R−B is offset-invariant and gain-preserving, so it survives the analog slop. (Song ID already yields the setlist via match-all; this is an independent, now-reliable confirmation.) |
| 2026-06-15 | **song_select reader = low-res grayscale grid over the highlight-slot ROI, matched against all 64 per-song templates with a small read-time offset search; setlist derived from the winning song, not the tab.** Grid 32×6, offsets dx∈{-6,-3,0,3,6}/dy∈{-3,0,3}. | 64/64 clean, ~99.1% slop. Matching all 64 (vs filtering by a setlist read first) means main/bonus separation is automatic and removes any dependence on reading the setlist first. The slot grid must be fine to separate ~40 short titles by ink pattern, but a fine grid over a tight ROI is shift-sensitive (translate robustness ~70%); a small offset search re-aligns per frame and recovers it to ~100% (overall slop 90.5%→99.1%) at modest cost (the ROI is small). Per-frame luma normalization handles gain/offset. The per-setlist first song (Slow Ride/Avalancha) sits one row lower (list can't scroll up past it), so its template is built from a second, lower ROI; both ROIs are fingerprinted at read time and each template scores against the one it came from. |
| 2026-06-15 | **"Reading" menu items / song titles = closed-set bitmap matching, NOT char-level OCR; char/digit OCR is reserved for open-ended values (scores).** | We have a reference snapshot per menu item and per song, so the selected item/song is recognized by matching its fixed region against the known templates (the same fingerprint idea as the screen classifier). Per-character decoding is only needed where there's no template — i.e. scores/multipliers/note counts — which is a later, separate slice. Keeps slice 2 free of OCR. |
| 2026-06-15 | **Static-list highlight reader = per-screen menu geometry (band + ordered items + axis) + per-cell "deviation from unselected baseline" with per-frame colour normalization; argmax over cells.** Scope = the 8 fixed-count static lists; `section_select` (variable list) and `song_select` (fixed-slot) deferred. | GH3 marks selection by *changing* a row (colour shift / highlight bar), not always by making it brightest — a luma/white argmax collided on most screens (flat scores). Comparing each cell to its own learned unselected appearance ("what changed") handles every highlight style and baselines out background art (main_menu / practice_end sit over collage). Baseline uses only *unselected* exemplars per cell so it's non-degenerate at K=2 (a median-over-all collapses to the midpoint and can't separate two items — that bug showed as exactly 1/2 on the three two-item screens). Per-frame normalization (subtract mean cell colour, scale by spread) cancels the analog gain/offset slop, lifting slop robustness ~90%→~99%. Result: 100% clean / 99.4% slop on 33 labelled frames. Baselines are learned from the corpus (a `SelectionCalibration`, parallel to the classifier's centroids); the firmware port bakes them. Band geometry was placed by auto-locating each selection's highlight from the labelled frames, not by eyeballing. |
| 2026-06-15 | **`items` are stored in on-screen order (top→bottom / left→right). Confirmed by visual inspection + the validated reader that all three 2-item screens match the `gh3_navigation.md` index order: `quit_confirm` = cancel(top)/quit(bottom), `part_select` = lead(top)/rhythm(bottom), `training_menu` = tutorials(left)/practice(right).** | An earlier crude white-min-channel *residual* diagnostic (used only to locate rows) was fooled by the light highlight bar and reported inverted rows for the 2-item screens, which raised a false flag. The actual reader (per-cell deviation from unselected baseline) reads each frame's true selection (2/2 per screen) — and since the highlighted cell is the one that deviates, a correct read proves `items` order = physical order. Full-res frames confirm cancel/lead/tutorials are the top/left items, matching the nav-doc indices. No discrepancy; nothing for M10 to reconcile here. |
| 2026-06-15 | **Prototype lives in its own `tools/gameplay/` subproject (own uv env + this journal); slice 1 is host-only (no firmware export yet).** | The observer+navigator are a distinct workstream that will grow (highlight reader, navigator, firmware module), so a dedicated subproject with its own journal (CLAUDE.md rule 4) is cleaner than folding into `marvin-perf` (a perf-log decoder). Host-only keeps the first slice focused on proving the algorithm; the firmware-consumable template export is deferred to the actual M9 port. |
| 2026-06-15 | **Screen classifier = coarse fixed-region mean-BGR fingerprint + nearest-centroid (per-class) + two-gate UNKNOWN reject (`d_best ≤ t_abs` AND `margin ≥ t_margin`).** Default config 12×8 grid, 5×5 samples/region, per-frame normalization on. | Matches Q10 (fixed-region/colour, not general CV) and the static nature of GH3 screens. Coarse region averaging buys positional-slop tolerance; the design is integer-L1 over uint8 vectors so the firmware port is mechanical. Default chosen empirically from the eval sweep — see normalization decision. |
| 2026-06-15 | **Per-frame normalization is on by default; it is required for value-slop robustness.** | The Wii feed is analog component → HDMI, so it carries gain/offset value slop. With raw means, brightening a frame moves its fingerprint far from its centroid in absolute L1 → false UNKNOWN; robustness to gain/offset was 65/202 and 22/202. Standardizing each frame's fingerprint to a fixed mean/std cancels affine gain/offset and lifted slop-robustness to ~99.8% (offset 155→202/202) at negligible cost (normalization is over the ~288-element vector, microseconds). |
| 2026-06-15 | **Threshold recommendation accepts every legitimate in-class match (`t_abs = 1.2 × worst in-class LOO distance`), with a soft margin gate (`0.5 × smallest in-class margin`); unseen-screen rejection is a *reported* secondary metric, not something we trade real-screen accuracy for.** | First-pass logic tuned `t_abs`/`t_margin` to separate held-out impostors, which over-rejected real screens (every LOO "failure" was a false UNKNOWN, not a misclassification). The observer's primary job is correctly identifying the 13 known screens; some genuinely-novel screen leaking through is acceptable and handled by the navigator's closed-loop + RED-recovery. Impostor leak is logged (9/101 at the default config) for honesty. |
| 2026-06-15 | **Sample density is a first-class fingerprint knob (dense vs N×N lattice per region); default subsampled (5×5).** | The on-device cost is dominated by reading the frame out of the uncached `.region_nocache` framebuffer (marvin journal 2026-05-20), so it's memory-bound. Dense = full-frame touch ≈ 7–20 ms; a sparse lattice mirrors cv_marvin_v1's existing 5×5 patch sampling and is sub-millisecond. The coarse average absorbs the extra sampling noise — eval shows subsampled matches dense on accuracy/robustness — so the cheap path is the default. |

---

## Hardware cost note (SAM9X75 port target)

ARM926EJ-S, ARMv5TE, single core, up to 800 MHz, no SIMD. The classifier is **memory-bound,
not compute-bound** — the dominant cost is reading frame pixels from the uncached DDR
framebuffer (~50–150 MB/s sequential; ~50–150 ns per strided single access). Compute
(per-region integer sums + an L1 distance over 13 templates ≈ a couple thousand byte ops) is
microseconds.

- Dense (every pixel, 720×480×3 ≈ 1.04 MB): **~7–20 ms**.
- Subsampled lattice (default 12×8 × 5×5 = 2400 reads): **~0.1–0.4 ms**.

Either is far from "seconds"; the observer needs only a few Hz for menu navigation, so the
subsampled path costs <1% CPU at 5–10 Hz.

---

## Open questions

- **section_select reading — deferred to the navigator (M10), by design.** Variable,
  song-dependent list, so no fixed-row-index reader. The screen is recognized now (constant
  chrome) and FULL SONG is always the top row, but the corpus has only the FULL-SONG-selected
  frame — no "other section selected" negative — so a standalone FULL-SONG-vs-other detector
  can't be built/validated yet (would need ~2 captures: a non-top section selected, on 2
  different songs). Decided approach instead: the navigator strums **UP until the highlight
  stops moving** (frame-difference saturation detection — *not* a blind fixed strum count),
  then confirms against the FULL SONG top-slot template. Needs no new data; revisit a
  standalone detector only if the saturation approach proves insufficient at M10.
- **song_select sub-modes.** Main vs bonus setlist share one `song_select` class (the
  bonus tab differs visually); the centroid spans both and classifies fine today. The
  song reader distinguishes the active setlist by page background colour (resolved, 100%).
- **Gameplay background variance.** The corpus has only the training-mode `in_song`
  background. Other modes (career/quickplay) use different backgrounds; the gameplay
  classifier may need a discriminative fixed region (the invariant 5-colour note-target row)
  rather than a whole-frame centroid. Recapture `in_song` across modes before trusting it.
- **Impostor leak (9/101).** Unmodeled screens (e.g. options submenus) can be accepted as a
  known peer. Acceptable for v0 (navigator recovers), but slice 2's per-screen regions or an
  explicit "options" template could tighten it if it bites.
- **Single-sample classes.** `loading`, `in_song`, `section_select`, `tutorials_menu` have
  one snapshot each — can't be cross-validated. Capture a few more variants per class to
  measure their in-class spread rather than leaning on visual-distinctiveness + the slop pass.

---

## Session log

### 2026-06-15 — slice 3 built (song_select reader)
Added `songselect.py` (slot bitmap match + setlist readout) and song-catalog metadata
(`SONG_SLOT_ROI`/`SONG_FIRST_ROI`/`SETLIST_BG_ROI`, `song_from_filename`), extended
`evaluate.py` with a song pass and `cli.py` classify to print the song. Learned en route:
the selected song's Y is fixed except the per-setlist first song (Greg), so a two-ROI scheme;
luma/white argmax is irrelevant here (it's a title bitmap match); matching all 64 templates
removes the dependence on reading the setlist first (setlist falls out of the match); and the
fine slot grid needed a small offset search to survive positional slop (translate 70%→100%,
total slop 90.5%→99.1%). Result 64/64 clean / 99.1% slop. First read the setlist from the
top tabs (~75%) before Greg noted they scroll off-screen for song 2+ and the page background
colour differs (main yellow / bonus white) — switched to a bg-warmth (R−B) read: 100% clean
/ 100% slop. 28 tests green. Host-only.

### 2026-06-15 — slice 2 built (static-list highlight reader)
Added `metadata.py` (per-screen menu geometry + ordered items, the seed of the §4.8.3
tables), `highlight.py` (per-cell unselected-baseline reader with per-frame colour
normalization + a `SelectionCalibration` learned from the corpus), extended `evaluate.py`
with a selection-reader pass (clean + slop) and `cli.py` with `rows` (debug) plus a
selection line on `classify`. Authored band geometry by auto-locating each selection's
highlight from the labelled frames rather than eyeballing. Iterated the scoring: luma/white
argmax collided on most screens (GH3 highlights by colour/bar, not brightness); per-cell
deviation-from-baseline got 100% on ≥3-sample screens but flipped a coin on the three
2-sample screens (median baseline degenerate at K=2); switching the baseline to
*unselected-only* exemplars + per-frame normalization landed 100% clean / 99.4% slop on all
8 screens. Dropped the obsolete per-screen `score` field. 23 tests green. Clarified the
title-matching-vs-OCR split (see decision log). Host-only.

### 2026-06-15 — slice 1 built
Stood up the `tools/gameplay/` subproject and built the screen classifier end-to-end:
`screens` (canonical ids + filename→id map, aligned to `gh3_navigation.md`), `corpus`
(load labelled PNGs as BGR), `fingerprint` (fixed-region mean-BGR with sample-density +
normalize knobs), `classifier` (nearest-centroid + UNKNOWN reject), `perturb` (synthetic
analog-slop battery), `evaluate` (LOO CV + confusion matrix + whole-class-holdout +
threshold recommendation + robustness + HW-time estimate + parameter sweep), `cli`
(`classify`/`eval`), and pytest coverage. Iterated on the threshold logic (first pass
over-rejected real screens) and confirmed normalization is the value-slop fix. Landed the
defaults above. 18 tests green; `gameplay eval` reports 96% LOO / 99.8% robust at the
default config. Host-only — no firmware port or template export yet.
