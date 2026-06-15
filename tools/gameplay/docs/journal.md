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
3. ⬜ **`song_select` reader** — fixed-slot: nearest-bitmap match the highlight slot against
   the 64 known per-song title templates (closed-set matching, **not** char-level OCR).
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

- **section_select reading.** Variable, song-dependent item list — doesn't fit the
  fixed-row-index model. The navigator only ever wants the top "FULL SONG", so a
  "is the top item highlighted?" check may be all that's needed; revisit when M10 lands.
- **song_select sub-modes.** Main vs bonus setlist share one `song_select` class (the
  bonus tab differs visually); the centroid spans both and classifies fine today. The
  fixed-slot song reader (slice plan #3) will also need to know which setlist is active —
  a within-screen readout (which tab is lit), not a separate class.
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
