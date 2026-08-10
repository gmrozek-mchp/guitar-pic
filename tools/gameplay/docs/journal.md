# gameplay — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the GH3
game-state engine prototype. Newest entries at the top. This subproject is the **offline
host-side prototype** for marvin's game-state observer/controller (spec §4.8, M9/M10): we
prove the detection algorithms in Python against the real-screen corpus, then port the
proven, simple GH3-specific logic to a firmware `gameplay_engine` module.

Orienting docs (read alongside this journal):
- `firmware/marvin/docs/spec.md` §4.8 — what the game-state subsystem is.
- `firmware/marvin/docs/gh3_navigation.md` — screen catalog + menu graph (canonical screen names).
- `firmware/marvin/docs/gh3_screens/` — the labelled 720×480 PNG corpus (134 screens, 19 classes).

---

## Current focus

**Two threads.** (a) **2-player readers, host-side** — the amp score digit reader landed
2026-08-09 and is proven on both sides; everything else 2p is data-blocked until Greg can
capture again (the 6-digit layout, a left-side stream, star power, the face-off gauge, the
multiplier — all itemized in Open questions). (b) the firmware port below.

**Porting to marvin firmware (`gameplay_engine`, spec §4.8) — started.** The offline
prototype is complete (phases 1–4); now freezing the proven algorithms into firmware. Phased
because the firmware is built/flashed by Greg in MPLAB (I write + syntax-review C only), and
because M9 (observer) and M10 (controller) are separate milestones:

- **Phase 0 — metadata exporter (done, host-side).** `gameplay export-c` emits the proven
  recognizer data as a single generated C header (`gameplay_metadata.h`): classifier config +
  per-class `uint8` centroids + thresholds + screen-id table; static-list menu geometry +
  per-cell baselines; song config/ROIs + 64 per-song templates + catalog + setlist warmth
  threshold. Plain PODs + flat arrays; floats only for the normalized baselines/song vectors
  (soft-float on the ARM926, tiny vectors). Generated output compiles clean under
  `cc -std=c11 -Wall -Wextra`; the header lands in the firmware tree at Phase 1.
  - **SD-card data split (marvin journal 2026-06-22, spec §4.8.3):** this generated header stays the
    compile-time source of truth for the algorithm-coupled recognizer data — it does **not** move to
    SD. Two host-side follow-ups for when the SD catalog/artwork loaders are built: (1) emit a
    `metadata_version` `#define` here whose value is reused in `/marvin/games/<game>/songs.json` so the
    UI can detect a stale catalog after a template-set regen; (2) a small tool that produces
    target-sized JPEG/PNG album art named `<setlist>-<NN>.{jpg,png}` (the recognizer's `(setlist,index)`
    key) from source covers. Both deferred until that firmware work starts (post-M4).
- **Phase 1 — screen-classifier observer (M9 v0) — done (firmware code-complete, pending Greg's
  MPLAB build).** New marvin `game/` module: `gameplay_metadata.h` (generated), `gameplay_classify.{h,c}`
  (pure FreeRTOS-free classify math), `gameplay_engine.{h,c}` (`game_task` subscribing to the video
  frame queue, classify → publish `game_state_t` on `xGameStateQueue` on screen change). Wired into
  `app.c` + `user.cmake`; no MCC regen. The pure unit compiles under `cc -Wall -Wextra`, and
  `test_firmware_classify.py` proves the C decision matches the Python `classify_image` on real
  corpus frames. Soft-float normalization as decided. See marvin journal 2026-06-15 for detail.
- **Phase 2 — selection + song readers — done (firmware code-complete, builds in MPLAB per
  Greg; no hardware test yet).** `game/gameplay_select.{h,c}` (pure): static-list highlight
  (`gp_read_selection`) + `song_select` (`gp_read_song`, offset search). `gameplay_engine.c`
  reads the selection each (rate-limited ~5 Hz, with a one-shot force trigger for post-actuation
closed-loop reads) frame and logs on screen-*or*-selection change
  (`GAME: <screen> / <item>` etc.); `game_state_t` gained a `selection` field. Host cross-check
  extended to both readers (match the prototype on real frames). Song match densely re-reads its
  ROI across the offset search — rate limit bounds the cost; subsample later if needed.
- **Phase 3 — navigator/controller (M10)** — port `navgraph` + `NavController`; wire as the
  third `FretboardLink` producer behind the `actuator_mode` arbiter (marvin journal 2026-05-21
  anticipated this). `MenuInput`→bitmask: GREEN=bit0, RED=bit1, strums=bits5/6; `PLUS`/pause
  needs the `+/-` protocol extension the nav doc flagged (off the practice-run path).
  **Post-actuation settle:** after sending inputs the controller must let the screen transition
  before observing — GH3 menus animate (cursor slide, fades, `loading`), so a delay + a
  poll-until-expected (with timeout) is required, not a single immediate `RequestObservation`.
  The observer exposes background 5 Hz + a force trigger; the settle/poll policy is the
  controller's. (Mirrors the prototype `NavController`'s re-observe-until-match loop.)

**Phase 4 — navigator (M10, done, host-only).** Plan high-level verbs and execute them
closed-loop along the menu graph. `practice_run(song, difficulty, part)` and a generic
`goto`/recover, driven through an `Observer`/`Actuator` seam, tested against a simulated GH3
menu (`simgame`). Control is observation-driven: each iteration runs whichever plan step
matches the observed screen — one rule that yields normal progress, the `part_select` skip,
and RED recovery. Selections strum the signed delta from the *observed* cursor (sticky-safe);
FULL SONG/FULL SPEED strum up until the selection stops moving (no blind count).
- Reaches `in_song` against the sim across normal / part-absent / sticky-default / misfire
  scenarios. A `CorpusObserver` integration runs the whole practice run on the **real
  observer** (phases 1–3) reading actual corpus frames — 0 fallbacks in the default config.
- Ports to firmware by swapping `SimObserver`/`SimActuator` for the CV observer + fretboard link.

**Phase 3 — song_select reader (done, host-only).** Identify the selected song by matching
the highlight-slot bitmap against the 64 per-song templates (closed-set, not OCR).
- **64/64 songs correct clean, ~99.1% under analog-slop.** Match is against **all 64**
  templates, so the setlist (main/bonus) falls out of the winning song — no dependence on
  the tab read. A small read-time offset search makes the fine slot grid tolerant of the
  positional slop (without it, translate robustness was ~70%).
- The per-setlist first song (Slow Ride / Avalancha) uses a lower ROI; all others the fixed slot.
- Setlist (main/bonus) read from the **page background colour** (yellow vs white) — 100% clean
  and 100% under slop; the song match also yields the setlist, so the two agree.

**Phase 2 — static-list highlight reader (done, host-only).** Within an already-classified
static-list screen, read which menu item is selected. Per-screen menu metadata (ordered
items + menu-band geometry) + a per-cell "deviation from unselected baseline" reader.
Proven against the corpus; not yet ported to firmware.

Result on the 8 fixed-count static-list screens (33 labelled frames):
- **100% clean selection accuracy** (every labelled frame read correctly).
- **~99.4% robustness** across the analog-slop envelope.
- Trivially cheap (per-cell mean colour over a handful of cells + a distance).

**Phase 1 — screen classifier (done, host-only).** Recognize which GH3 screen a captured
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
   fixed-count static-list screens. (Deferred within phase 2: `song_select` fixed-slot
   reader, `section_select` variable list.)
3. ✅ **`song_select` reader** — fixed-slot bitmap match of the highlight slot against the 64
   per-song templates + offset search; setlist falls out of the match and is also read
   independently from the page background colour. (Deferred: reading the scrolling neighbour list.)
4. ✅ **Navigator (M10)** — graph + planner (`practice_run` + `goto`) + closed-loop
   `NavController` over an Observer/Actuator seam, proven against a simulated menu and the
   real observer. (Deferred: non-practice modes; real fret/strum/`+` bit mapping is the port.)
5. ✅ **Number/score region readers (host-only; training-mode white font)** — per-digit
   glyph OCR of the open-ended score. The font is **proportional** (not tabular), so digits
   are segmented by their **ink gaps** (right-to-left blobs), each resized to a canonical cell
   of per-cell **ink coverage** and matched (integer L1) against **10 per-digit templates** (one
   averaged coverage mask per glyph). Templates built from a **54-frame capture-derived corpus**;
   chrome-of-the-block registration retained (host-side). Result:
   **54/54 clean exact, 270/270 per-digit, LOO 54/54, A2D slop 99.7%, re-register 100%**, and —
   the decisive check — **0 monotonic violations across all 6549 real capture frames** (a play
   only climbs), covering 3–6-digit scores (250→101720). Supersedes the earlier fixed-pitch
   attempt (only 10/16 held-out — the font isn't monospace). **Ported to marvin firmware**
   (`game/gameplay_score.{h,c}` + `export_c` score block, mode-parameterized, no on-device chrome
   registration in v0; C cross-checks byte-faithful on all 54 frames). The **multiplier** (colour)
   and **streak** (odometer OCR + confident-read tracker) readers followed; career (green segmented)
   score font is the only deferred scoring-block item.
6. 🚧 **Firmware port** — `gameplay_engine` on marvin (spec §4.8). Phase 0 (metadata exporter)
   ✅; screen classifier / selection / song / **score / multiplier / streak** readers ported
   (code-complete, pending Greg's MPLAB build); navigator/controller (M10) ported earlier. See
   Current focus.
7. 🚧 **2-player amp scoreboards (host-only)** — per-side chrome registration ✅ (2026-07-14),
   the **score digit reader** ✅ (2026-08-09), and **re-validated on both sides at the locked
   block origins** ✅ (2026-08-10): fixed 7 px/pitch-9 grid inside the registered block,
   powered-cell digit count, per-cell relative ink coverage matched against a 2-variants-per-digit
   bank, then an `Amp2pTracker` temporal filter. **28 497 gameplay frames across the two 15k
   captures: 0 ordering violations, 0 reads on a frame with no amp, 5-digit scores exercised
   (peak 66 098).** The 6-digit re-layout is now *read* on an extrapolated pitch and flagged
   rather than refused. Deferred (see Open questions): star power, the face-off gauge, a
   6-digit capture to confirm the pitch, and the firmware port.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-08-10 | **Capture sweeps read frames in *frame-number* order, never lexicographic (`evaluate.capture_frames`); and the exported strips are zero-padded to 5 digits.** | `export-region` padded to 4 digits, so on any capture past 9999 frames `…-1001.png` sorts between `…-10009.png` and `…-10010.png`. Both monotonic evals took `sorted(glob("*.png"))`, so they scored a **shuffled** sequence — which is silently self-concealing: the ordering check's own "big drops are song resets" escape hatch absorbed the jumps, and the adjacent-delta list (the check that exists because a misread leading digit reads *higher*, where ordering can't see it) filled with hundreds of impossible values. Sorting on the trailing integer is the real fix and is robust at any width; the wider pad just keeps a plain `ls` or shell glob honest up to 99999. This retro-invalidated two recorded findings — see the 2026-08-10 (correction) session-log entry. |
| 2026-08-10 | ~~**Some captured gameplay frames hold the amp's *idle* composite, and nothing spatial can reject them — so the reader gets a temporal filter (`Amp2pTracker`).**~~ **WITHDRAWN same day — the "idle composite" frames were early-song frames mis-ordered by the 4-digit filename sort (row above).** `Amp2pTracker` is kept as an unexercised safety net (it filters **0** frames on both real captures), not as a fix for an observed artefact. Its rise-fast/fall-slow rule is still the right shape if a real single-frame dropout ever appears. | Roughly one frame in 11 of the left capture (525 frames) shows score 0, no multiplier, no streak odometer, no star-power pills, and the amp shifted a pixel or two — mid-song, on strictly consecutive frame epochs with no dropped or duplicated records, so it is real captured content and not a decode artefact. Every spatial gate passes it: the digits are a crisp `0` (distance 347, margin 2999), and the **chrome probe scores it *better* than a real gameplay frame** (SAD 4.1–5.9 vs 4.1–13.5) because `amp2p_<side>_ref.png` is itself a score-0 crop — so raising or lowering TAU cannot separate them, and there is no threshold to tune. What does separate them is time: 523 of the 525 are exactly one frame long, while a play's score never falls. Confirming only *falls* means the value marvin acts on gains no latency, and a real song reset (the strip sits at 0 for many frames) still lands. Filters all 527 left / 424 right occurrences. |
| 2026-08-10 | **6 digits is now read rather than refused: the container width rules out the measured pitch, so `AMP2P_GRID_6` carries the layouts that physically fit, the ink says a 6th digit is present, and the *bank* picks between candidates. The read is flagged `layout_measured=False`.** | Reversal of the 2026-08-09 "report, don't decode" decision, on a measurement that was not available then. The strip's dark container is **49 px** on both sides (`AMP2P_CONTAINER_W`, block-local 11..60 left / 14..63 right, bright bezel immediately left of it), and six cells at the measured pitch 9 need 52 — so pitch 9 is excluded *by measurement*, and pitch 8 is the widest that fits. That turns the guess from "what does the layout look like" into "which of two layouts", which is small enough to decide per frame. Detection is physical, not heuristic: six digits cannot fit at pitch 9, so a 6-digit strip must ink left of the 5-cell grid, and across **22 400 five-digit frames the leftmost inked column is never left of the grid's own first column**. Between candidates the glyph bank decides, because reading a (6,8) strip through 7 px cells drags a neighbouring column into every glyph. Still not measured, hence the flag and the `layout` field: the first real 6-digit capture confirms or corrects the pitch, and only then does it become an `AMP2P_GRID` row. |
| 2026-08-10 | **The two amps share one algorithm *and* one template bank; only the block origin and `AMP2P_RIGHT_EDGE` differ.** | Worth testing rather than assuming, since the sides were registered independently and their right edges differ by 3 px. Measured on the two captures: per-column ink occupancy lands on 7 px cores at pitch 9 on both sides, and a bank built from the **left** capture alone reads the right capture identically (same 13 739 frames, same range, 0 violations), and vice versa (14 400 of 14 758, the shortfall all gate rejections, no wrong values). The combined bank is best on both. So no per-side banks, no per-side thresholds. |
| 2026-08-10 | **`amp2p-monotonic` gates every read on the chrome probe, and scores a decrease as a violation only when it is small (`PLAY_RESET_DROP` 1000).** | Two corrections the multi-song captures forced. (a) The digit reader only asks what the band says, so on a cutscene frame with no amp it still found lit cells and returned a number — 12 such reads on the left capture before gating, 0 after. Presence is the caller's job and the harness has to model that. (b) A capture spans several songs and the score restarts at zero, so "never decreases" is false across a whole capture; the two cases separate by size, since a misread digit is worth at most ~1000 on a plausible score while a song boundary drops tens of thousands. Also: a misread does **not** always decrease — reading a leading `3` as a `9` reads *higher* — so the distinct-delta list is a co-equal check, and it is now restricted to consecutive frames so it stays diagnostic. |
| 2026-08-09 | **The 2-player amp score is read on a *fixed grid*, not by ink segmentation — and the digit count comes from the display's unpowered leading cells, not from the glyph matcher.** Cells are 7 px wide at pitch 9, band rows 14–23, right-aligned against a fixed edge (`AMP2P_GRID` / `AMP2P_RIGHT_EDGE`, block-local). | The amp strip is a segment display, so unlike the single-player *proportional* font (which forced `score.py`'s ink-run segmentation) the cell positions are constant and can be tabulated. Measured on the reference capture: the pitch and the right edge are unchanged across the 3→4 digit crossing, and per-column ink occupancy lands exactly on 7-px cores with clean 2-px gaps on **both** sides. Deriving `n` from the powered-cell contrast gate instead of the matcher is what makes it robust: unused cells are genuinely unpowered (contrast ~17 vs 100–140 for a lit cell, a ~6× margin), so the count is a physical measurement independent of glyph quality — a faint leading `1` can't shorten the number. |
| 2026-08-09 | **The amp font is template-matched, not 7-segment-decoded.** | It *looks* segment-based, and a segment decoder is far cheaper, so it was tried first: it read 1596/3027 frames with 0 monotonic violations and then failed **every** frame containing a `4`. The font is a stylized LED face, not a true 7-segment one — `1` is a centred bar rather than the right-hand pair, and `4` and `7` are drawn with diagonal strokes. Template matching also reuses the integer coverage core already proven and firmware-mirrored for the 1p readers. |
| 2026-08-09 | **The ink threshold is relative to each *cell*, and each digit keeps two templates instead of one average.** `AMP2P_INK_NUM/DEN` = 1/2 per cell; `AMP2P_VARIANTS` = 2. | The LEDs pulse, so a bright glyph's strokes bloom about a pixel wider than a dim one's. Under a band-wide threshold the brightest digit in the band sets the range, and a dim `9` thins until it matches `5` better than it matches a bloomed `9` — the exact failure seen (48 monotonic violations, all traceable to that pair-family). Thresholding per cell removes the brightness phase, and it moved 114 cells from `5` to `9`, taking the reference capture to 0 violations. Keeping two templates per digit then buys headroom rather than correctness: worst-case distance 1844→1275 and 1st-percentile margin 389→648, with 3 and 4 variants adding templates and moving neither. Same reasoning the streak reader already uses when it thresholds per tumbler. |
| 2026-08-09 | **The grid table stops at 5 digits on purpose, and a wider strip is *reported* (`layout_unknown`) rather than decoded.** | Greg: the score does climb past 99999 and the spacing changes when the 6th digit arrives. That geometry cannot be measured from any frame we have (the reference capture tops out at 4 digits and the screen corpus at 4), so any 6-digit entry would be invented. Leaving `AMP2P_GRID[6]` absent turns the unmeasured case into a detectable event instead of a silent misread, and pinning it later is one table row — no code change. The same guard catches a non-right-aligned lit pattern and ink that does not sit on the grid. |
| 2026-08-09 | **Both sides share one bank and one corpus; only the block origin and right edge differ per side.** | The two amps are the same art at the same scale — the star-power pill column is mirrored, but the amp itself is not (digits are right-aligned on both). So a bank trained on one side must read the other, and that became the primary generalization test rather than an assumption: a bank built from 10 committed **right**-side capture frames reads the **left** amp in all 5 corpus snapshots exactly (a different side, a different ROI origin, and a different image source). |
| 2026-08-08 | **2-player run policy is fixed, not a choice the navigator makes: always PRO FACE-OFF (`multiplayer_menu` idx 1); marvin drives P1/left only; `guitar_select_2p` is assert-don't-act; `player_ready_2p` always PLAY SHOW (idx 0); `venue_select` confirms through whatever is selected.** | Greg. Every degree of freedom on the 2-player setup path that *doesn't* affect gameplay is pinned to one value, so the navigator has nothing to decide and needs no readers for it — the same reasoning that lets `section_select` be GREENed through today. Concretely: PRO FACE-OFF is the only 2p mode in scope, so FACE-OFF/BATTLE are documented for recognition but never routed to; venue doesn't affect gameplay, so no venue catalog or fixed-slot matcher is built (8 posters captured become classifier exemplars only); character choice is out of scope, so `character_select_2p` is a pass-through GREEN. **Assert-don't-act on the guitar screen is a safety call:** marvin's guitar is expected already preselected on the left, and improvising guitar-moving inputs could drag the *human's* guitar to marvin's side and wedge the screen — so the controller verifies and confirms its own side, and aborts to the operator if the assertion fails, rather than trying to fix it. Marvin driving only P1 halves the reader work: P2 state is wait-for-only, never acted on. |
| 2026-08-08 | **The 2-player setup screens introduce a new edge kind — `act-then-wait` — whose wait is *unbounded*; timing out into the generic RED recovery is a bug, not a fallback.** | Every edge modelled until now is "marvin acts → the screen changes", so the controller's post-actuation policy is poll-until-expected *with a timeout*, falling back to RED recovery. On `guitar_select_2p` and `player_ready_2p` marvin can only confirm its own side — the transition is gated on a **second human actor** confirming theirs. A timeout there would back marvin out of the setup flow while the player is still deciding, i.e. the recovery path actively breaks the run. So these edges get "observe until the expected screen appears" plus operator-visible "waiting for player 2" status instead of a deadline. |
| 2026-08-08 | **`difficulty_select` is shared outright with the 2-player path (one cursor, existing reader); `player_ready_2p` is the screen that needs a per-side reader.** | Greg: PRO FACE-OFF puts both players on the *same* difficulty, so the natural worry (a per-player difficulty screen) doesn't exist — no new class or reader there. The genuinely per-side screens are `guitar_select_2p` and `player_ready_2p`, and only the latter has a cursor to read. That makes the deferred reader work small and specific: key menu layouts by a *layout* id rather than by screen id so one screen can carry two panels, and read only the left one. |
| 2026-07-09 | **Score reader = register-once + search-free per-frame read; registration keys on the block *chrome*, not the digits.** The whole scoring block (`SCORE_BLOCK_ROI (114,309,210,414)`, 96×105) is located once by a masked normalized-SAD offset search matching its static chrome (box frame + inner panel texture + medallion ring); the 6 digit cells are then fixed offsets inside it. Each subsequent frame samples those cells and 1-NN-classifies — no per-frame search. Mask is a hand-drawn artifact (`data/scores/score_block_mask.png`, magenta = chrome). | Greg: field position is stable on a rig (won't vary moment-to-moment); slop is position (per-rig) + A2D colour/brightness, **not** scale. So the search belongs at gameplay start, not per frame; per-frame luma normalization handles A2D (100% on gain/offset/noise). Chrome is a better anchor than the digits: **digit-independent** (registers before any valid score, can't alias a cell onto a neighbour) and **mode-independent** — the box chrome is pixel-identical in training and career (verified: all 25 training + 44 career snapshot frames register to (0,0) against a training-built reference; shift recovery exact to ±7 px). Greg flagged that a variance-derived mask wrongly kept the left of the digit row as "chrome" (our samples top out at 5 digits, so it's always blank there) — the hand mask hard-excludes the full digit row so a 6-digit score can't corrupt registration. The block doubles as the marvin-perf capture region. First tried digit-template two-stage registration (wide whole-field + per-cell refine); superseded by chrome the same day. |
| 2026-07-09 | **Score digits read by per-digit glyph OCR: fixed-pitch, right-anchored 6-cell grid + per-cell 1-NN over labelled cell exemplars (not a per-class centroid).** ROI `(139,315,199,333)`, `N_SCORE_DIGITS=6`, blank class for unused leading cells. | The score is open-ended (no whole-field template), so it must be decoded per digit — the case the earlier phases deferred. Measured on the corpus: the training white font is **tabular** (cluster-count == digit-count, units right edge locked at x=197, ~10 px pitch), so a fixed right-anchored grid segments cleanly. 1-NN beats a centroid because the crisp digits blur together when averaged (8/3/0 collide); keeping exemplars lifted clean reads 8/9→9/9 and A2D 16/18→18/18. Score is right-aligned/grows left (Greg), so cells anchor at the right and short scores leave leading cells blank. |
| 2026-07-09 | **Score corpus = ~9 hand-labelled training-white in-song frames from `marvin-perf/snapshots/`, value in the filename (`score__training__NNNNNN__snapNNNN.png`); host-side under `tools/gameplay/data/scores/`.** | Greg: minimal manual labelling. The 9 distinct scores cover all 10 glyphs (multiple exemplars each), enough to build templates + a real eval, without bulk labelling. LOO is only 6/9 — the small-corpus ceiling (a held-out digit sometimes has a single look-alike exemplar left), not an algorithm limit (clean/A2D/registration are all 100%). Enriching via a marvin-perf score-crop capture is the follow-up to lift LOO and harden templates. |
| 2026-06-15 | **Port to marvin firmware is phased (Phase 0 exporter → 1 observer → 2 readers → 3 controller); numerics stay soft-float for v0; metadata ships as a generated C header (`gameplay export-c`).** No MCC regen needed. | The firmware is built/flashed by Greg in MPLAB (I write + syntax-review C), so phases land "code-complete, pending build"; M9 (observer) and M10 (controller) are already separate milestones, so the phase split mirrors them. Soft-float chosen over fixed-point for v0: the only float work is per-frame normalization over small vectors (~hundreds of values), microseconds even soft-float on the ARM926 — re-validating a fixed-point reimplementation isn't worth it until profiling says so. A generated header (not hand-ported constants) keeps the firmware data in lockstep with the proven prototype and re-emittable when the corpus/params change; plain PODs + flat arrays avoid coupling to firmware struct layout. `game_task` is pure compute on the existing video frame queue + `FretboardLink_Send`, so no new peripheral / no re-apply-patch churn. |
| 2026-06-15 | **Navigator is observation-driven: each loop iteration runs whichever plan step's `expected_from` matches the currently observed screen (not a fixed program counter).** Verbs: `practice_run` + generic `goto`. Selections strum the signed delta from the observed cursor; FULL SONG/FULL SPEED strum up until the selection stops moving. | One rule gives the three behaviours the nav doc needs: normal progress (next screen matches the next step), the `part_select` skip (an absent part means the observed `difficulty_select` matches a later step and the part step is simply never run), and recovery (an off-plan screen matches no step → press RED to back up until a known screen reappears, then resume). Delta-from-observed-selection is sticky-default-safe; saturate-until-stable avoids a blind strum count (and is the agreed `section_select` mechanism). Closed-loop verification is implicit: a mis-fire just means the next observation matches no expected step → recover. |
| 2026-06-15 | **Offline navigator is proven against a simulated GH3 menu (`simgame`) behind an `Observer`/`Actuator` seam; the real observer is exercised via a `CorpusObserver`.** | No console/actuator offline, so the sim is the test oracle (with configurable part-absent / sticky / misfire to actually exercise the control logic). The seam is the firmware boundary — swap in the CV observer + fretboard link to port. The `CorpusObserver` runs the whole practice run on real corpus frames through the phase-1–3 vision stack (0 fallbacks in the default config), proving the observer's outputs are exactly what the controller consumes. |
| 2026-06-15 | **Setlist (main/bonus) is read from the page background colour — warmth = R−B over a large background ROI (median, robust to overlaid text) — not from the setlist tabs.** | 100% clean and 100% under slop. The "setlist"/"bonus" tabs are only on screen when the first song is selected — they scroll off for song 2+ (Greg), so a tab ROI is empty for 63/64 frames (that earlier approach was ~75%). The page itself is always visible and differs by setlist: main = yellow parchment, bonus = whiter. R−B is offset-invariant and gain-preserving, so it survives the analog slop. (Song ID already yields the setlist via match-all; this is an independent, now-reliable confirmation.) |
| 2026-06-15 | **song_select reader = low-res grayscale grid over the highlight-slot ROI, matched against all 64 per-song templates with a small read-time offset search; setlist derived from the winning song, not the tab.** Grid 32×6, offsets dx∈{-6,-3,0,3,6}/dy∈{-3,0,3}. | 64/64 clean, ~99.1% slop. Matching all 64 (vs filtering by a setlist read first) means main/bonus separation is automatic and removes any dependence on reading the setlist first. The slot grid must be fine to separate ~40 short titles by ink pattern, but a fine grid over a tight ROI is shift-sensitive (translate robustness ~70%); a small offset search re-aligns per frame and recovers it to ~100% (overall slop 90.5%→99.1%) at modest cost (the ROI is small). Per-frame luma normalization handles gain/offset. The per-setlist first song (Slow Ride/Avalancha) sits one row lower (list can't scroll up past it), so its template is built from a second, lower ROI; both ROIs are fingerprinted at read time and each template scores against the one it came from. |
| 2026-06-15 | **"Reading" menu items / song titles = closed-set bitmap matching, NOT char-level OCR; char/digit OCR is reserved for open-ended values (scores).** | We have a reference snapshot per menu item and per song, so the selected item/song is recognized by matching its fixed region against the known templates (the same fingerprint idea as the screen classifier). Per-character decoding is only needed where there's no template — i.e. scores/multipliers/note counts — which is a later, separate phase. Keeps phase 2 free of OCR. |
| 2026-06-15 | **Static-list highlight reader = per-screen menu geometry (band + ordered items + axis) + per-cell "deviation from unselected baseline" with per-frame colour normalization; argmax over cells.** Scope = the 8 fixed-count static lists; `section_select` (variable list) and `song_select` (fixed-slot) deferred. | GH3 marks selection by *changing* a row (colour shift / highlight bar), not always by making it brightest — a luma/white argmax collided on most screens (flat scores). Comparing each cell to its own learned unselected appearance ("what changed") handles every highlight style and baselines out background art (main_menu / practice_end sit over collage). Baseline uses only *unselected* exemplars per cell so it's non-degenerate at K=2 (a median-over-all collapses to the midpoint and can't separate two items — that bug showed as exactly 1/2 on the three two-item screens). Per-frame normalization (subtract mean cell colour, scale by spread) cancels the analog gain/offset slop, lifting slop robustness ~90%→~99%. Result: 100% clean / 99.4% slop on 33 labelled frames. Baselines are learned from the corpus (a `SelectionCalibration`, parallel to the classifier's centroids); the firmware port bakes them. Band geometry was placed by auto-locating each selection's highlight from the labelled frames, not by eyeballing. |
| 2026-06-15 | **`items` are stored in on-screen order (top→bottom / left→right). Confirmed by visual inspection + the validated reader that all three 2-item screens match the `gh3_navigation.md` index order: `quit_confirm` = cancel(top)/quit(bottom), `part_select` = lead(top)/rhythm(bottom), `training_menu` = tutorials(left)/practice(right).** | An earlier crude white-min-channel *residual* diagnostic (used only to locate rows) was fooled by the light highlight bar and reported inverted rows for the 2-item screens, which raised a false flag. The actual reader (per-cell deviation from unselected baseline) reads each frame's true selection (2/2 per screen) — and since the highlighted cell is the one that deviates, a correct read proves `items` order = physical order. Full-res frames confirm cancel/lead/tutorials are the top/left items, matching the nav-doc indices. No discrepancy; nothing for M10 to reconcile here. |
| 2026-06-15 | **Prototype lives in its own `tools/gameplay/` subproject (own uv env + this journal); phase 1 is host-only (no firmware export yet).** | The observer+navigator are a distinct workstream that will grow (highlight reader, navigator, firmware module), so a dedicated subproject with its own journal (CLAUDE.md rule 4) is cleaner than folding into `marvin-perf` (a perf-log decoder). Host-only keeps the first phase focused on proving the algorithm; the firmware-consumable template export is deferred to the actual M9 port. |
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

- **2-player amp scoreboard follow-ups** (score digits done 2026-08-09, re-validated on both
  sides 2026-08-10). The reader is built so each remaining gap is *visible* rather than guessed.
  1. **Confirm the 6-digit pitch.** Still no capture past 99999, but this is no longer a refusal:
     the container is 49 px so pitch 9 cannot fit six cells, and `AMP2P_GRID_6` holds the two
     layouts that do — read on the widest that the ink and the bank agree with, flagged
     `layout_measured=False` and reporting the fitted `layout`. **Closes with:** any capture whose
     score passes 99999 → check `read-amp2p` prints `layout=(7, 8) EXTRAPOLATED` and the right
     number, then promote it to an `AMP2P_GRID` row and drop the flag. 5 digits is now *measured*
     (11 199 / 11 306 frames per side).
  2. ✅ ~~**Root-cause the idle-composite frames.**~~ **Withdrawn 2026-08-10 (same day): there is no
     artefact.** The frames were early-song frames spliced into mid-sequence by `export-region`'s
     4-digit filename sort; with `evaluate.capture_frames` the tracker filters **0** frames on both
     captures. Nothing to root-cause, and the 1-player readers need no equivalent audit. See the
     correction session-log entry. `Amp2pTracker` is retained as an unexercised safety net.
  3. **Star-power pill count.** The pill column is *mirrored* — left of the left amp, right of the
     right amp. The 76×78 blocks now clip part of it in view (visible at the block edges in both
     captures), so the rects still need widening (host-supplied, so no reflash) before it can be
     read.
  4. **The centre face-off gauge** (the tug-of-war meter, roughly `(305,235)-(405,325)`) — a
     separate ROI and its own region slot, unrelated geometry to the amps.
  5. **Firmware port** — `export_c` amp bank + a `gp_read_amp2p` + the tracker's fall-confirm, as
     a separate signed-off phase.
  6. **Noise robustness.** The per-cell range is a 2-sample statistic (min/max), so one hot pixel
     can set it. Not worth fixing on current evidence (the pixel-locked streams read with 205+ of
     margin), but it is the first thing to revisit if a noisier rig ever appears — robust
     percentiles would cost the bit-exact integer C mirror.
- **section_select FULL SONG reader — still wanted; firmware assumes-and-GREENs for now
  (2026-07-04, Greg).** Variable, song-dependent list, so no fixed-row-index reader; the
  screen is recognized (constant chrome) and FULL SONG is always the top row. The offline
  plan was to strum UP until the highlight stops moving (frame-diff saturation) — but the
  firmware has no frame-diff primitive and `read_selection` returns -1 on `section_select`
  (no layout), which FAIL-looped the closed-loop controller (it could never confirm a top).
  **Interim decision:** since we only ever reach this screen with FULL SONG selected, the
  controller `saturate_top` GREENs straight through it (no strum, no confirm). **To revisit:
  a real FULL SONG top-slot detector** — a highlight match at the top-slot ROI (mirrors the
  `song_select` slot match) returning selected/not, exported into `gameplay_metadata.h`, so
  the controller *confirms* rather than assumes. Still blocked on data: need a "non-top
  section selected" negative (~2 captures on different songs) to build/validate the
  threshold; the lone FULL-SONG-selected frame can't prove an unhighlighted top slot is
  rejected.
- **Score reader follow-ups (phase 5).** (1) **Career (green segmented) font** — a second
  `SCORE_ROI["career"]` + template bank; color (green-dominant ROI) is the natural
  font/mode discriminator. (2) **Richer templates / LOO** — the ~9-frame corpus reads
  clean/A2D/registration at 100% but LOO is 6/9 (sparse exemplars); a small marvin-perf
  score-crop capture would add many glyph exemplars (self-supervised via the proven reader)
  and lift it. **(Built 2026-07-09: `marvin-perf score-capture` streams the 96×105 block to
  PNGs at full rate — host done, firmware pending Greg's MPLAB build; see marvin journal.
  Next: capture a play, auto-label via visual read + monotonic-increase, rebuild templates.)** (3) **6-digit scores** — the reader supports them, but the corpus tops out at
  5 digits, so that path is untested. (4) **Firmware port** — `export_c` digit banks +
  `gp_read_score` (register once when `in_song` goes stable, then search-free read).
- **song_select sub-modes.** Main vs bonus setlist share one `song_select` class (the
  bonus tab differs visually); the centroid spans both and classifies fine today. The
  song reader distinguishes the active setlist by page background colour (resolved, 100%).
- ✅ **Gameplay background variance.** Resolved (2026-07-15): the gameplay screens
  (`in_song`, `in_song_2p`) are now classified by **scoreboard-chrome presence**
  (`present.py` / `gameplay_present.c`), not the whole-frame centroid — see the session log
  below. A whole-frame centroid keys on the dynamic highway/crowd/background, so it drifts
  across modes/songs (the intermittent 2p dropout); the static scoring chrome does not.
  Remaining data gap: still only training-mode `in_song` + one-session 2p frames, so the
  in-class present margin (~0.04) is optimistic — capture career/quickplay 1p and cross-session
  2p to tighten the true out-of-sample margin (the slop proof shows the *mechanism* generalizes).
- **Impostor leak (112/134 = 84%, and the 9/101 figure quoted below is long stale).** Unmodeled
  screens (e.g. options submenus) can be accepted as a known peer. Measured 2026-08-09: the leak
  is **93/112 without** the 2-player setup classes and **112/134 with** them, so it is a
  pre-existing property of the current corpus + threshold rule, *not* something the 2P import
  caused. It drifted from the original 9/101 as the corpus grew (the 70-song `song_select`
  expansion above all): `t_abs = 1.2 × worst in-class LOO distance` is set by the single loosest
  class, and `dist_max` is **7906** either way — one wide class sets the gate for all of them.
  Still acceptable for v0 by the 2026-06-15 decision (the navigator's closed-loop + RED recovery
  is the real defence, and in-class accuracy is what we refuse to trade), but the honest statement
  is now "the absolute-distance gate barely rejects anything; the margin gate is doing the work."
  Worth revisiting as a **per-class** `t_abs` rather than one global threshold, which would stop
  the loosest class from setting everyone's gate.
- **Presence probes have zero positional headroom (measured 2026-08-09).** All three — 1p score
  block, both 2p amp panels, and the new READY-badge probe — exceed TAU 18 at a **1 px** shift.
  The "no offset search; the capture is pixel-locked and the rig is stable" position (2026-07-15)
  is therefore all that stands between these probes and a false negative, for every one of them at
  once. If a per-rig offset ever shows up, the fix is the offset search `amp2p.calibrate`
  prototypes, applied to all probes together — worth doing pre-emptively if the rig is ever
  re-mounted or the converter changed.
- **`guitar_select_2p` "is my guitar preselected on the left?" is data-blocked.** Greg wants the
  controller to assert this before confirming, but all four captures have a guitar in P1's shield,
  so there is no negative to threshold against (P2's empty shield has different art and can't
  stand in). Needs a few snapshots of the screen *before* P1's guitar is assigned; the same capture
  would add badge-absent negatives for `ready.py`, whose absent threshold currently rests on one
  frame.
- **Single-sample classes.** `loading`, `in_song`, `section_select`, `tutorials_menu` have
  one snapshot each — can't be cross-validated. Capture a few more variants per class to
  measure their in-class spread rather than leaning on visual-distinctiveness + the slop pass.
- **`character_select_2p` has 2 exemplars that are really 2 different states** (both-strips vs
  P1-strip-with-P2-panel), so each is a poor stand-in for the other: **LOO 0/2** (both reject as
  UNKNOWN) while **in-sample is 2/2** with margins ~1900–2200. Not an algorithm problem — a
  whole-frame centroid is the wrong model for a screen whose halves advance independently, which
  is exactly what the per-side probe work supersedes. Capture more of both states, or let the
  probe take over this class.

---

## Session log

### 2026-08-10 (correction) — the capture sweeps were reading frames out of order; two findings withdrawn

`export-region` zero-padded frame numbers to 4 digits and both monotonic evals took
`sorted(glob("*.png"))`, so on a >9999-frame capture `…-1001.png` sorts between `…-10009.png`
and `…-10010.png`. Measured on the right capture: **495 backward steps, every displaced frame in
1001–1495** — early-song frames spliced into the middle of the sequence. Fixed by
`evaluate.capture_frames` (sort on the trailing integer) plus a 5-digit pad in `export-region` /
`score-capture`.

The bug was self-concealing, which is why it survived: the ordering check treats a big drop as a
song boundary, so every splice was absorbed as a "reset" rather than reported as a violation.

**Both sweeps re-run correctly, same reader, same committed corpus:**

| | left | right |
|---|---|---|
| frames / amp on screen | 15 455 / 14 939 | 14 941 / 14 093 |
| read | 14 758 | 13 739 |
| ordering violations | 0 | 0 |
| **song resets** | **0** (was "412") | **0** (was "412") |
| **idle-composite frames filtered** | **0** (was 527) | **0** (was 424) |
| false reads | 0 | 0 |
| peak | 58 806 | 66 098 |
| worst dist / min margin | 1621 / 205 | 2386 / 286 |

Deltas are now clean — left `[2,3,4,6,7,8,9,10,12,15,16,18,20,21,24,50,100,150,200,300]`, right the
same plus `[5,28,32,36,40,224]` — where before they carried ~300 impossible values (43251, 44871, …).
**That matters more than the violation count:** a misread leading digit reads *higher*, so ordering
alone cannot catch it and the delta list is the check that does. The pollution had disabled it on
every capture over 9999 frames.

**Two findings from the entry below are withdrawn:**

1. **"About one frame in 11 is the amp's idle composite"** — no. Those were frames 1001–1495,
   spliced in by the sort. Early-song frames legitimately show score 0, no multiplier (1× at song
   start), no streak odometer (only appears ~25+ streak) and no star-power pills — which is exactly
   the description recorded as "idle art". The chrome-probe observation (it scores them *better*
   than mid-song frames, because `amp2p_<side>_ref.png` is itself a score-0 crop) is consistent with
   that and is not evidence of a pipeline fault. With correct ordering the tracker filters **0**
   frames on both captures. The **capture-pipeline open question is withdrawn**, and the 1-player
   readers do *not* need the audit it asked for.
2. **"412 song resets"** — no. Both captures are one continuous play; the resets were the splices.

`Amp2pTracker` stays, but honestly labelled: it is an **unexercised safety net**, not a fix for an
observed artefact. Its tests are synthetic (they build the reads directly) and still pass, and
rise-fast/fall-slow is still the right shape if a real single-frame dropout ever turns up — it just
has no evidence behind it today. Removing it would be defensible; keeping it costs nothing.

Everything else in the entry below stands: the geometry, the corpus and its self-check, the
per-side interchangeability, the 6-digit container bound, and the per-frame read numbers (peaks,
false reads, cell-count histograms and dist/margin are all order-independent).

### 2026-08-10 (later) — the amp digit reader re-cut on both sides; 6 digits becomes readable; and one frame in 11 is lying

Greg's two gameplay captures at the locked rects — `web-20260809-143135` (**left**, 15 455
frames) and `web-20260809-144032` (**right**, 14 941) — are the re-cut the re-registration entry
was waiting on. They closed the empty-corpus block, exercised 5 digits for the first time, bounded
the 6-digit layout, and turned up a capture artefact that no spatial gate can see.

**Geometry: the compensated constants were right.** Measured from scratch per side rather than
trusted. Green-LED ink (`G>90, G−R>25, G−B>40`) isolates the strip cleanly: band rows **6..15**
both sides, per-frame column runs at starts 17/26/35/44/53 (left) and 20/29/38/47/56 (right) —
**pitch 9, 7 px cores, right edges 60 / 63**, exactly `AMP2P_BAND_Y0` / `AMP2P_RIGHT_EDGE` as
compensated at the re-registration. A first pass at the aggregate column profile looked like ink
in two of the gaps; that was the **green 3× multiplier glyph** in the medallion, which sits inside
the grid's column span but 20 rows below the band. Which also answers an open question: **2-player
does show a multiplier** (4× purple, 3× green, 2× gold over the portrait), and the streak odometer
is inside the block too.

**Corpus: 36 labelled frames, 18 per side, read by eye off montages**
(`score2p__<side>__…__c14…f….png`). One label was wrong — a `3` read as a `9` at left frame
12261 — and it poisoned digit 9's templates badly enough that every real `3` then read as `9` (the
bank had only 5 nine-exemplars, so the bogus 3-shaped "9" out-matched the blurred 3 means). Caught
by dumping the glyph as ASCII and checking the upper-left stroke, then pinned by a **self-check
that reads the corpus back through the bank it built** — 0 mismatches now. Worth remembering: a
single bad exemplar in a small class does not degrade gracefully, it inverts a digit.

**Validation, both captures, through the real committed corpus and the presence gate:**

| | left | right |
|---|---|---|
| frames / amp on screen | 15 455 / 14 939 | 14 941 / 14 093 |
| read | 14 758 (98.8%) | 13 739 (97.5%) |
| ordering violations | **0** | **0** |  <!-- measured out of order; re-measured 0/0 in the correction entry -->
| reads on a frame with no amp | **0** | **0** |
| peak score | 58 806 | 66 098 |
| cell-count hist | {1:1274, 2:30, 3:524, 4:1912, 5:11199} | {1:245, 2:46, 3:350, 4:2129, 5:11306} |
| worst dist / min margin | 1621 / 205 | 2386 / 286 |

Unread frames are digit transitions, in runs of 1–4. Adjacent-frame deltas are
`2,3,4,6,7,8,9,10,12,15,16,18,20,21,24,50,100,150,200,300` (left) and the same plus
`5,28,32,36,40,76,224` (right) — sustain ticks and note awards scaled by the multiplier, i.e. real
GH3 scoring, which is the independent check that the numbers are not merely self-consistent.

**The sides need no separate algorithm** — tested, not assumed, since Greg asked for it
explicitly. A bank built from the left capture alone reads the right capture *identically*, and
vice versa with only extra gate rejections and no wrong values. Only the block origin and the
3 px right-edge difference are per-side.

**6 digits went from refused to read**, on a measurement that was missing before: the dark
container is **49 px** on both sides, and six cells at pitch 9 need 52, so pitch 9 is excluded and
pitch 8 is the widest that fits. Detection is physical — a 6-digit strip must ink left of the
5-cell grid, and across 22 400 five-digit frames the leftmost inked column is never left of the
grid's first column. Validated on synthetics built by re-laying real glyphs at pitch 8 (reads
exactly, `dist` 858/255 against a gate of 2600); the alternative (6, 8) candidate is *selected*
correctly and then gated, because chopping 7 px glyphs to 6 px is not a real 6 px font. Flagged
`layout_measured=False` until a real capture confirms the pitch.

**[WITHDRAWN 2026-08-10 — see the correction entry above: these were early-song frames
mis-ordered by the 4-digit filename sort, and with correct ordering the tracker filters 0 frames.]**
~~The find that matters most: about one frame in 11 of the left capture is the amp's *idle*
composite mid-song~~ — score 0, no multiplier, no streak odometer, no star-power pills, amp
shifted a pixel or two. Frame epochs are strictly consecutive with no drops or duplicates and all
15 455 frames are distinct, so it is real captured content. It defeats every spatial gate: the `0`
matches at distance 347 with 2999 of margin, and the **chrome probe likes it *better* than a real
gameplay frame** (SAD 4.1–5.9 vs 4.1–13.5) because the committed reference is itself a score-0
crop — there is no TAU that separates them. 523 of 525 are single frames, so `Amp2pTracker`
(accept a rise at once, confirm a fall 3×) removes all 527 left / 424 right occurrences at zero
latency for a climbing score. Root-causing why marvin's capture presents that frame is a
**capture-pipeline** item now in Open questions, and the 1-player readers should be checked for
the same exposure.

**`amp2p-monotonic` was reworked** to be meaningful on a real capture: it gates every read on the
chrome probe (the reader has no presence notion of its own — now stated in `read_amp2p_score`'s
docstring), treats only *small* decreases as violations, counts song resets separately, restricts
the delta list to consecutive frames, and reports the tracker's filtered count.

Also `amp2p.calibrate_on_corpus`: the corpus now holds both sides, and `calibrate` was being
handed `samples[:4]` regardless of side, which registered `right` against left-side crops and slid
the grid off the digits (it then read every right frame as a bogus 6-digit strip). Latent since the
corpus was right-only; the helper filters by side.

Suite: **111 passed** in the non-firmware tests, up from 87 passed + 27 skipped — the 27 skipped
digit tests now run, plus 11 new ones (6-digit candidates / fit / flagging, the trigger's silence
on every measured layout, and the tracker). The 4 `test_export_c` / `test_firmware_classify`
failures in the tree are from the in-flight `faceoff_end_menu` screen (GP_N_SCREENS 19→20 shifted
the classifier indices), not from this work.

### 2026-08-10 (later still) — `faceoff_end_menu`: the 2-player results screen, and two bugs the hardware run exposed

Greg got a full 2-player game through, which surfaced the end of the flow.

- **New screen class `faceoff_end_menu`** (3 corpus frames): the results newspaper spread after a
  pro face-off. Static list, items CONTINUE (0) / RETRY SONG (1) / MORE STATS (2). Band
  **(375, 87, 495, 148)**, signed off against a ruler overlay — item centres y ≈97/117/138 at
  ~20.3 px pitch. **The selected row is a dark bar with light text**, the inverse of most GH3
  highlights, which is why a brightness-argmax probe found newsprint instead of the bar; the
  deviation-from-unselected-baseline reader doesn't care. Corpus 134 → **137 samples, 20 classes**.
  - LOO **3/3**, and `practice_end_menu` stays 5/5 — no confusion between the two end screens.
    Selection reader **43/43** clean, slop 99.8%, dy budget **±7**.
  - `GP_N_SCREENS` 19 → 20, `GP_N_MENUS` 10 → 11.
- **This screen has no BACK at all.** Its legend offers only SELECT and UP/DOWN, so RED does
  nothing — `nav_to_main_menu`'s RED default would have spun until the exit budget ran out. It now
  special-cases CONTINUE (item 0), which lands on `song_select`, which RED *does* back out of.
- **Marvin deliberately doesn't act here at song end** (Greg's requirement): the results stay up
  until the next run starts, and it is that run's anchor that presses CONTINUE.
- **The class also fixes a bug the debounce created.** While `faceoff_end_menu` was unclassified it
  read UNKNOWN — and `play_until_done`'s shake debounce *holds* on UNKNOWN by design — so the play
  loop would never have recognised the song as over. Adding the class is what closes that; the two
  changes were only safe together.

**Two controller bugs from the same run, both worth recording as classes of mistake:**

- **RED on UNKNOWN was undoing marvin's own progress.** On `character_select_2p`, marvin's GREEN
  moves only *its* half to the ready panel, so the next frame is "P1 panel + P2 strip" — a state
  with no corpus exemplar, which reads UNKNOWN. `step_for(UNKNOWN)` returned NULL, which fell into
  the recover arm, which pressed **RED** — backing P1 out to the strip. Log showed the exact loop:
  `character` → `unknown` → `recover (RED)` → `character_select_2p` → repeat until FAILED. UNKNOWN
  is *not* evidence of being off-plan; it is no reading. It now holds and re-observes (bounded by
  `GC_MAX_UNKNOWN` 24, then terminal `NO SCREEN`), and only a decisive unexpected screen recovers.
  Notably this is the **same distinction** the shake debounce already made in `play_until_done` —
  right in one place, wrong in the other.
- **The character hop is human-gated and wasn't marked as such.** That mixed frame persists until
  the *human* advances their own half (both panels up is what makes the frame classify as
  `player_ready_2p`), so the step needed `await_player`, not a short transition wait. Consequence
  worth knowing: the human reaches their panel *before* marvin picks PLAY SHOW. Fine — both sides
  simply have to confirm, order is irrelevant.
- Also from Greg's note that confirming the mode takes a few seconds to bring up character select:
  the post-step transition budget went **4 s → 10 s** (`GC_TRANSITION_MS`). The cost of being too
  short is not a delay but a *repeated input* — the wait lapses, the loop re-observes the same
  screen, and the step re-fires a strum + GREEN into an already-committed menu.

**Digit corpus re-cut (Greg):** `data/scores/score2p__*.png` is back at **36 frames — 18 left, 18
right — all 76×78 at the locked origin**, replacing the 10 old-origin crops deleted earlier. That
clears the 27 pending skips: `test_amp2p` runs and passes 36/36, so the 2p amp digit reader is
validated at the new geometry on **both** sides. Full suite **124 passed, 0 skipped**.

### 2026-08-10 (later) — P1 READY!-badge probe ported; the 2p guitar-select step verifies its own confirm

The badge probe built 2026-08-09 (host-only) is now on marvin, and it closes the ambiguity that
made the 2-player guitar step untrustworthy: Select Guitar advances only once **both** sides
confirm, so an unchanged screen could not distinguish "marvin's GREEN never registered" from "the
human hasn't confirmed yet" — and a failed GREEN would sit out the whole 120 s P2 wait before
reporting `NO PLAYER 2`, which named the wrong cause.

- **Left side only, by design and by Greg's call.** P2's banner is a different ROI and is *not*
  needed: the screen advancing **is** P2 confirming, so the existing `await_player` wait already
  covers that side. Recorded in `ready.py`'s docstring from the start; Greg reached the same
  conclusion independently.
- **The port reuses the presence mechanism unchanged.** `export_c` emits `gp_ready_p1` as a plain
  `gp_probe_t` (`{145,265,262,305}`, 117×40, npix 4680) plus `GP_READY_TAU 18`, and the C reader
  `gp_ready_p1_present` calls the existing `gp_probe_l1` — same masked SAD, same mean128/std48
  space, no new math. The mask is **all-ones** because the whole ROI is the fiducial (badge-present
  it is the banner; badge-absent it is the guitar body on the shield). `GP_PROBE_MAX_LEN` now spans
  the badge too.
- **Plumbed the idiomatic way:** `game_state_t.ready_p1` (1/0/−1), read by the engine only on
  `GP_SCREEN_guitar_select_2p`, mirroring how selection/score/streak are read per screen.
- **Controller: `ACT_CONFIRM_READY`.** GREEN, then poll ≤ `GC_READY_TIMEOUT_MS` (2500) for the
  badge. Badge → proceed into the P2 wait; no badge → return false so the step **retries** instead
  of blocking on a GREEN that never landed. "Screen already advanced" counts as success — if both
  players confirm before we sample there is no badge left to see, and that is a pass.
- **Numbers:** ready 0.00 / 0.53 / 0.54, not-ready **51.69**, against TAU 18 — ~96× on this corpus.
  C↔Python cross-check (new `ready` driver mode) agrees on verdict *and* SAD for all four frames.
  Suite **88 passed, 27 skipped**. Caveat unchanged: **one** badge-absent frame, so the absent side
  of the threshold rests on a single exemplar plus its value-slop variants.
- **Confirmed on hardware** (Greg, same day). The 2P run logged: `main_menu / career` →
  `GC: MULTIPLAYER` → `main_menu / multiplayer` → `guitar_select_2p` → `GC: guitar (await P2)` →
  **`GC: P1 READY confirmed`** → `GC: WAITING FOR P2` → `multiplayer_menu / face_off`. So the badge
  verify, the act-then-wait edge, and the `multiplayer_menu` selection reader all work live.
- **Still not built:** the *assert* Greg originally wanted — "is P1's guitar already on the left"
  before confirming. Still data-blocked: all four corpus frames have a guitar in P1's shield, so
  there is no negative to threshold, and P2's empty shield has different art. Needs a few Select
  Guitar snapshots taken before P1's guitar is assigned. The badge verifies the *confirm*, not the
  *precondition*.

### 2026-08-10 — both amp blocks re-registered by eye; capture rects follow, and it invalidated every origin-bound artefact

Greg reviewed the amp block placement on a real frame and re-registered both — the old boxes
were up-and-left of the amps. **Final, signed off against a pixel ruler:**

| | old (x0,y0,x1,y1) | new | delta |
|---|---|---|---|
| 2pL | (128,164,196,242) | **(131,172,199,250)** | +3, +8 |
| 2pR | (515,164,583,242) | **(513,172,581,250)** | −2, +8 |

Both stay 68×78 (the code relies on the two sides sharing a block size).

- **Final geometry, iterated with Greg against a pixel ruler and locked:**

| | first pass | grown | final |
|---|---|---|---|
| 2pL | (131,172,199,250) 68×78 | +8 px left | **(123,172,199,250) 76×78** |
| 2pR | (513,172,581,250) 68×78 | +8 px right | **(513,172,589,250) 76×78** |

  Both sides stay the same size (76×78) — the code relies on that. Getting here took several
  review rounds; the lesson recorded as a standing rule is that **these regions are settled with
  Greg from an overlay before any processing work**, because a wrong region does not fail loudly
  (`song_select` once scored 64/64 while sampling the wrong row).
- **Capture needed no firmware work at all.** The `marvin-perf` region rects are *host*-supplied
  (`perf_log_records.h`: "the rect is host-selected so it can be repointed without a firmware
  rebuild"), so it is `records.py` `REGION_SLOTS` → `(123,172,76,78)` / `(513,172,76,78)` plus three
  pinned copies in the marvin-perf tests. 174 tests pass, no reflash.
- **The digit cells are *compensated*, not moved.** `AMP2P_BAND_Y0` and `AMP2P_RIGHT_EDGE` are
  **block-local**, so every block move drags the digit grid with it — off digits that were already
  validated. They move by the negation of each delta; across all the iterations that landed at
  `BAND_Y0` 14 → **6** and `RIGHT_EDGE` left 55 → **60**, right 61 → **63**. The invariants to
  preserve are the *absolute* anchors: **right edge x=183 (left) / x=576 (right), band top y=178** —
  verified by overlay at every step. Recorded in the constants' comment, since nothing previously
  said they depended on the block origin.
- **The masks are Greg's, drawn per side at the final origin, then trimmed by one column.** A
  mirror was tried for the right side and rejected from the overlay: the right amp sits ~11 px
  further right inside its block (the same offset `RIGHT_EDGE` 60-vs-63 encodes), so a mirrored bar
  lands on the medallion ring. Greg then marked one column to drop — the leftmost masked column,
  which was picking up the neighbouring scenery at the amp's edge. Final: **1382 px (left) /
  1370 px (right)** of 5928.
- **Staleness is checkable rather than silent.** `amp2p.MASK_PAINTED_AT` records the origin each
  mask was painted against and `mask_origin_mismatch()` returns a reason when it stops matching
  `AMP2P_BLOCK`; `test_amp2p`, `test_present` and the C presence cross-check skip on it (and on an
  empty digit corpus) with that reason rather than failing. This is the mechanism that made the
  several re-registration rounds safe to do: a stale mask otherwise degrades registration *quietly*.
- **Presence separation is unaffected by all of it: worst present 0.22, best absent 0.62 → 2.8×
  around TAU 0.37**, with **0/187 corpus frames misclassified** (it was 0.22/0.62 → 2.9× before the
  first move). That invariance is the point: presence keys on masked static chrome, and the mask
  followed the chrome every time. `translate`/`scale` still cross (1.15/1.17 vs 0.85/0.88),
  consistent with the standing zero-shift-headroom finding — excluded as unrealistic on a
  pixel-locked capture, not passed.
- `gameplay_metadata.h` re-exported: `gp_probes[]` carries `{123,172,199,250}` npix 1382 and
  `{513,172,589,250}` npix 1370. C↔Python presence cross-check passes; all six marvin `game/` TUs
  syntax-check clean. Suite **87 passed, 27 skipped**.
- **Stale artefacts removed** now the geometry is locked: the paint-free `amp2p_*_ref_6x.png`
  templates (regenerated on demand whenever a re-paint is needed) and the 10 old-origin
  `score2p__*.png` digit crops, which were cut at a superseded origin and are unusable against the
  current block. Both remain in git history at `d180151`.

**Still pending (data, not code):** the 21 skipped `test_amp2p` digit tests. Their guard now also
covers the empty corpus, naming the re-cut needed — a capture at the new `marvin-perf` rects, which
is the capture Greg is taking next. Until then the digit reader is untested against the new origin,
though its cell geometry was compensated and visually verified to land on the same pixels.

### 2026-08-09 — P1 READY!-badge probe (host-only), and every presence probe turns out to have zero shift headroom

Two results, one of them systemic and worth more than the feature.

**The badge probe (`gameplay/ready.py`, `READY_BADGE_ROI = (145,265,262,305)`).** The Select
Guitar screen advances only when *both* sides confirm, so "the screen didn't change" is ambiguous
between "my GREEN failed" and "the human hasn't confirmed" — the badge disambiguates it. Same
mechanism as the `present.py` chrome probes (masked per-frame-normalized SAD at nominal coords,
shared TAU 18), with two differences that fall out of the region: the whole ROI is the fiducial so
there's no hand-painted mask (badge-present it's the banner, badge-absent it's the guitar body on
the shield — almost nothing shared), and the reference is cropped from the corpus frame
`guitar_select_2p__p1_ready` at load time rather than committed as a second asset that could drift.
- **Clean 0.0–0.5 vs absent 51.7** (~100×), and across the gain/offset/noise envelope worst-present
  stays 0.5 while best-absent is 51.2 — 2.8× on the absent side. That is a *better* value-slop
  margin than the shipped chrome probes, which sit at ~10–11 against the same TAU 18.
- **Getting there needed measurement, not eyeballing.** This screen's art is *animated* (flames):
  a whole-frame ready-vs-unready diff lights up x 58..645, and coarse/colour formulations I tried
  in order to be shift-tolerant all collapsed to ~1.1× margins. What makes the fine ROI work is
  that the animation does not reach inside it — the three badge-present frames differ by only ~0.5
  there, which is also the premise for one reference frame standing in for all of them (asserted
  in `test_ready.py`).
- **Data caveat: one badge-absent frame.** The absent side of the threshold rests on a single
  exemplar plus its value-slop variants. Reasonable given the ROI is static, but a few more unready
  captures would retire it.

**The systemic finding: no presence probe has any shift headroom, including the two already
shipped.** Measured by shifting a known-present frame until the probe exceeds TAU: the 2p amp
probes exceed at **±1 px** (SAD 31.3), the 1p score block at **±1 px** (25.5), and the badge at
**±1 px** (27.1). So ±1 px sensitivity is a property of masked-SAD-at-nominal-offset, not of any
one ROI choice — the badge is not the fragile outlier it first looked like. This makes the standing
"positional slop is a one-time registration concern, deferred" (2026-07-15) more load-bearing than
it reads: TAU 18 leaves **zero** shift margin, not "some". If a per-rig offset ever appears, all
three probes need the offset search `amp2p.calibrate` already prototypes, and they will all break
together rather than one at a time.
- All four sampled regions were **verified visually** by drawing the exact boxes the code reads
  (and, for the menu bands, the exact cells `_cell_bounds` produces) — the practice that caught the
  `song_select` mis-registration on 2026-07-07. All correct: the menu cells hold one label each
  with boundaries in the gaps, the amp blocks bound their panels, the 1p block bounds the scoring
  block, and the badge ROI is centred on the banner.

`tests/test_ready.py` (+7): clean decisions, wide separation, ROI-static premise, value-slop
envelope. Full suite **114 passed**. Host-only — no export_c/firmware wiring yet, so the
controller's `ACT_CONFIRM` on guitar-select still presses GREEN without verifying.

**Not done — the assert Greg actually asked for is data-blocked.** "Marvin's guitar is already
preselected on the left" needs a frame where it *isn't*, and all four captures have a guitar in the
left shield (the only empty shield in the corpus is P2's, whose art differs, so it can't stand in).
Same shape of gap as the `section_select` FULL SONG detector. **Closing capture: a few
`guitar_select_2p` snapshots before P1's guitar is assigned / before confirming** — which would
also supply the extra badge-absent negatives above. Until then the badge probe verifies the
*confirm*, not the *precondition*.

### 2026-08-09 — selection layouts for the two readable 2-player screens (`multiplayer_menu`, `player_ready_2p`)

Prerequisite for the firmware's 2P plan (marvin journal, same date): `select_and_confirm` returns
false on an unreadable cursor, so without a `MENU_LAYOUTS` entry the plan could only press GREEN
blind on these two screens — and picking PRO FACE-OFF / PLAY SHOW specifically is the whole point.

- **`multiplayer_menu`** — 3 items (face_off / pro_face_off / battle), band `(195,313,262,402)`.
  **The band is deliberately narrow in x (65 px)**, which is new for this table: this poster's
  labels are drawn on a *tilt*, rising ~20 px from the left end of a label to its right, so a
  full-width band puts one item's glyphs into a neighbouring cell. Measured over x 195..260 the
  item centres are y 328/358/387 with even ~29.5 px pitch; over the full label width the apparent
  pitch skews to 30/32 and the per-item runs overlap. This is why the geometry was located by
  measurement rather than by eye — the tilt is invisible in a whole-frame glance.
- **`player_ready_2p`** — the **LEFT panel only** (marvin plays P1, so it reads the side it acts
  on), 4 items, band `(190,284,312,400)`, centres y 298/327/356/385 at an exact 29 px pitch. The
  band sits below the character art (which varies frame to frame) and well left of the P2 panel at
  x≈380. P2's cursor is independent and this single-index model can't express it — by design, since
  nothing needs it. The `p1_ready` frame is excluded from calibration automatically: its suffix
  isn't in `items`, so `build_selection_calibration` skips it.
- **Validation.** Selection reader **40/40 clean** (was 33/33): `multiplayer_menu` 3/3,
  `player_ready_2p` 4/4; slop robustness **99.8%** (up from 99.7%). Positional budget by uniform-
  shift sweep — the check that caught the `difficulty_select` mis-registration on 2026-07-07 —
  is **dy ±12 / dx ±24** for `multiplayer_menu` and **dy ±11 / dx ±24** for `player_ready_2p`,
  i.e. healthier than `main_menu`'s ±5 and in the same band as the well-registered screens.
- `gameplay_metadata.h` re-exported (`GP_N_MENUS` 8 → **10**, per-cell baselines generated);
  `tests/test_export_c.py` assert bumped. Full suite **107 passed**.
- Still unread on this path (unchanged): the `READY!` badge — needed for the guitar-select
  *assert* Greg asked for, and the natural shape is a masked-SAD probe like `present.py`'s.
  `guitar_select_2p`, `character_select_2p` and `venue_select` have no layout and don't need one
  (the controller's new `ACT_CONFIRM` accepts what they already offer).

### 2026-08-09 — the 5 2-player setup screens are now recognized (corpus + classifier + header)

Closes the "Phase B" deferral from the 2026-08-08 nav-doc work: the setup path was *documented*
but every screen on it classified as UNKNOWN, so the firmware controller's `step_for()` had
nothing to dispatch on and would RED-recover straight back out of the flow. This is the unblock
for implementing the 2P run.

- **Corpus +22 frames, 5 new classes** (`firmware/marvin/docs/gh3_screens/`, README section added):
  `guitar_select_2p` (4), `multiplayer_menu` (3), `character_select_2p` (2), `player_ready_2p` (5),
  `venue_select` (8). Corpus 112 → **134 samples, 14 → 19 classes**. Every label was verified
  against the image before naming rather than taken from the earlier walk-through notes.
- **Suffix convention for the per-side screens: the suffix names P1's state**, because marvin
  plays P1 (left) and that is the side the controller reads and acts on. `guitar_select_2p` and
  `character_select_2p` suffixes are descriptive state labels, not menu rows — neither screen is a
  static list, so `MENU_LAYOUTS` has no entry and the selection reader skips them.
- **Results: 4 of the 5 new classes are clean.** LOO `guitar_select_2p` 4/4, `multiplayer_menu`
  3/3, `player_ready_2p` 5/5, `venue_select` 8/8; **all 22 frames classify correctly in-sample**
  (margins 1922–10889 against `t_margin` 645). No 1p↔2p leakage. Whole-corpus LOO 128/134 (95.5%),
  robustness 99.7%, and the selection / song / score readers are untouched (33/33, 70/70, 54/54).
  `venue_select` has the tightest margins of the new classes (2547–5129) — the poster art varies
  wildly while the page background is constant, which is also why it separates at all.
- **`character_select_2p` is the one soft spot: LOO 0/2, in-sample 2/2.** Its two frames are two
  genuinely different states (both-strips vs P1-strip-with-P2-panel), so holding either out leaves
  a centroid that rejects the other. Kept both anyway — at runtime the centroid is built from both
  and recognizes both with healthy margins; LOO on a 2-sample class of dissimilar states is the
  wrong measure. Logged as an open question; the per-side probe is the real answer.
- **Found on the way: `GP_SCREEN_*` values are assigned alphabetically, not in `SCREEN_IDS` order.**
  `classifier.build_templates` sorts the ids, so adding these five **renumbered every existing
  screen** (`GP_SCREEN_in_song_2p` 2 → 4). Safe — every firmware use is symbolic (`GP_SCREEN_*`
  constants, `gp_menus[]` lookups by symbolic value, `gp_screen_ids[]` for logging) and **no
  numeric screen id is persisted or put on a wire** (checked perf-log records and the host decoder:
  neither carries one). `screens.py` now says so explicitly, because an initial comment there
  claimed the opposite and that assumption would make the next class addition a schema change.
- **Also corrected a stale documented metric:** the "impostor leak 9/101" in Open questions was
  measured before the 70-song corpus expansion. Actual today: **93/112 without** these classes,
  **112/134 with** — so ~83% either way, pre-existing and not caused by this import. See the
  rewritten open question for why one wide class sets the global `t_abs` for all of them.
- `gameplay_metadata.h` re-exported (`GP_N_SCREENS` 14 → **19**, centroids + screen-id table
  regenerated, 306 KB); `tests/test_export_c.py` dimension assert bumped. Full suite **107 passed**.
  All six marvin `game/` TUs syntax-check clean against the new header (`xc32-gcc -fsyntax-only
  -Wall -Wextra`; the one `game_engine.c` unused-variable warning is pre-existing). **Pending
  Greg's MPLAB build.**
- **Not done here** (the rest of the 2P flow): the controller's act-then-wait edges, a left-panel
  `MENU_LAYOUTS` entry for `player_ready_2p`, the READY-badge probe for the guitar assert, and a
  capture pass to confirm the tail order past venue.

### 2026-08-09 — 2-player amp score digit reader (host-only, both sides, 0 violations)

Finished the thread opened 2026-07-14, which had the per-side *location* (chrome registration)
done and left the digit reader as "next, needs sign-off, blocked on data". The data arrived:
`tools/marvin-perf/captures/web-20260808-155311` is a 51 s / **3027-frame** stream of the
**right** amp (`score_2p_right`, 68×78 blocks, score 443 → 3153). Scope agreed with Greg for
this pass: **score digits only**, **host-only**. Star power / face-off gauge / multiplier come
later.

**Registration needed no changes** — `amp2p.calibrate` returns (0,0) for frames sampled across
the whole new capture and for both sides of all 5 corpus snapshots. What it did need was to
accept a bare region-strip crop, so `_ensure_full_frame(image, side)` now embeds a block into a
full frame (mirroring `score.py`); registration and reading then share one path.

**Geometry, measured not assumed.** Band rows **14–23** (ink is exactly zero at rows 13 and 24,
both sides). Cells **7 px wide at pitch 9** with 2-px gaps, right-aligned against a fixed edge
(`right` 61, `left` 55 — the sides' block origins were picked independently in July, hence the
constant 6 px offset). Per-column ink occupancy over the capture lands dead-on those cores, and
the same check on the snapshots confirms the left side. Verified across the 3→4 digit crossing
at frame ~760: the pitch and the right edge do not move.

**Two findings drove the design** (both in the decision log): the font is not segment-decodable
(a segment decoder failed every `4`), and the LED **pulse** makes coverage brightness-dependent —
which under a band-wide ink threshold turned dim `9`s into `5`s. Per-cell thresholding plus two
templates per digit fixed it.

**Labels were bootstrapped, not hand-annotated.** All 11392 non-blank cells were clustered
(k=14 over the coverage vectors), the centroids read off as ASCII, then an active-learning loop
grew a corpus from the weakest-margin frames and a backward pass pruned it. Result: **10
committed frames** (`data/scores/score2p__right__*.png`) → 17 templates, with *better* margins
than the 40-frame set it came from (510 vs 255). `gameplay amp2p-grow` is that loop as a verb,
so the next capture is one dry-run plus a glance.

**Validation.**
- **Reference capture: 3027/3027 frames read, 0 monotonic violations, 0 unreadable, 0
  layout-unknown**; range 443..3153; cell-count histogram {3: 716, 4: 2311}; worst per-cell
  distance 1275, minimum runner-up margin 510. Distinct frame-to-frame deltas
  `[2, 4, 5, 6, 9, 50, 100]` — small sustain steps plus 50/100 note awards, i.e. real GH3
  scoring, which is the independent check that the values are not merely self-consistent.
- **Cross-side / cross-source:** the right-side bank reads the **left** amp in all 5 snapshots
  exactly (6906, 0, 486, 2386, 6906) — and the right amp too (0, 4376, 4400, 4400, 0).
- **Value slop:** gain 0.85/1.15 and offset +20 leave every read intact. Across the *whole*
  envelope no perturbation ever produces a **wrong** number — the gates degrade to "unreadable"
  instead. Two axes do hit that path on the lower-contrast snapshot frames: `offset-neg20`
  (which clips ~49% of the digit band to black) and added noise (a single hot pixel can set a
  cell's range). Both sit outside a pixel-locked capture; the raw stream has 510 of margin.
- **Registration:** synthetic ±3 px shifts recover exactly and the read is unchanged.
- 27 new tests in `tests/test_amp2p.py`; suite 80 → **107 passed**.

Also extracted `gameplay/covcore.py` — the integer coverage primitives (`luma_i`, `edge`,
`cov_grid`, `ink_mask`) that `score.py` had private and `present.py` had already duplicated a
constant from. `score.py` keeps its local names via aliases, so the move is behaviour-neutral
(verified: suite green before adding anything new). `ink_mask` gained a `range_from` argument
for callers whose band is wider than the region the threshold should come from.

**New CLI:** `read-amp2p <img> --side {left,right}`, `amp2p-monotonic <dir> --side`,
`amp2p-grow <dir> --side [--commit]`.

Open items are in "Open questions" below: the 6-digit layout, a left-side stream capture, and
the deferred star-power / gauge / multiplier readers. No firmware work — host-first, per the
pattern the 1p readers used.

### 2026-08-08 — the 2-player setup path is mapped (nav doc); 5 new screens, not yet recognized

Walked Greg's 22-snapshot capture of `main_menu → MULTIPLAYER → … → 2-player gameplay`
(`snapshot-73726` … `snapshot-89695`, 720×480, all copied into the gitignored
`tools/marvin-perf/snapshots/` so the deferred corpus import has source data). This closes
the `main_menu → MULTIPLAYER` edge, which had been `TBD` since the nav model was started.
**Doc-only by decision** — `firmware/marvin/docs/gh3_navigation.md` plus this journal; no
`screens.py` ids, no corpus import, no `gameplay_metadata.h` re-export, no readers.

Five new screens, in path order: **`guitar_select_2p`** (assign a guitar to your side;
per-side `READY!` badge), **`multiplayer_menu`** (FACE-OFF / PRO FACE-OFF / BATTLE — all 3
items captured), **`character_select_2p`**, **`player_ready_2p`** (PLAY SHOW / CHANGE
CHARACTER / OUTFIT / GUITAR), **`venue_select`** (8 posters observed). Also added the missing
**`in_song_2p`** catalog entry — it has been a recognized *class* since 2026-07-14 but the nav
doc never described it, and the 2-player worked path terminates there.

Three findings that changed the model rather than just adding rows:

- **A third highlight paradigm: `per-side`.** The 2p screens split into a left (P1) and right
  (P2) half, each with its own cursor *and its own commit state*, advancing **independently** —
  *snapshot-81552* catches the left half still on the character strip while the right half is
  already on the ready panel. So `character_select_2p` and `player_ready_2p` are sub-states of
  one screen, not two mutually-exclusive full-screen states. This is the **same failure mode as
  the `in_song_2p` centroid dropout** (2026-07-15): a whole-frame fingerprint over a screen
  whose halves stage independently keys on a configuration that varies. Per-side probes at
  fixed coords are the mechanism to reuse, and the doc now says so.
- **`act-then-wait` edges** (see decision log) — the first edges in the model that don't
  advance when marvin acts, and the first place where the existing timeout→RED-recover policy
  is *wrong* rather than merely suboptimal.
- **Every screen on this path paints an input legend** — `SELECT` (green fret icon) / `BACK`
  (red fret) / `UP/DOWN` (strum bar). That's independent confirmation of the doc's
  GREEN/RED/strum conventions on screens we had no other evidence for, and a candidate cheap
  "this is a menu" cue if the impostor-leak open item ever bites.

Written into the nav doc: the `per-side` paradigm + `act-then-wait` notation under
Conventions, the 6 new catalog entries, the closed `MULTIPLAYER` edge, 10 new menu-graph rows,
a **"Worked path — 2-player pro face-off run"** mirroring the training-run path, and the
open items below.

**Deferred / unconfirmed** (all in the nav doc's Open items): the 5 screens are documented but
**not recognized** — the corpus import + `GP_N_SCREENS` 14 → 19 re-export is the next phase;
`player_ready_2p` needs a per-side layout (menu layouts are keyed by screen id today, and
`read_selection` returns one index); the tail past `venue_select` (song → difficulty →
loading ordering, whether a 2p `part_select` exists, the available song set) is
operator-reported from memory and needs one capture pass; 2-player end-of-song screens are
unmapped.

### 2026-08-08 — a 2-player corpus can now be captured off the device (marvin-perf capture types)

The data gap this journal has carried since 2026-07-15 — "only one session's worth of `in_song_2p`
frames" — is now a capture away rather than a tooling task. `marvin-perf` grew capture types for
2-player gameplay (see the marvin journal, same date):

- **The two amp scoreboards stream independently**, as strip kinds `score_2p_left` / `score_2p_right`
  on their own device region slots. Default rects are `AMP2P_BLOCK` verbatim
  (`(128,164,68,78)` / `(515,164,68,78)`), so what lands on disk is exactly what `amp2p.py` reads —
  and each side comes out as its own PNG series (`score-2pL-NNNN.png`), which is the shape the
  per-side reference/mask build wants. Host-supplied rects, so widening a block for the deferred
  2p digit reader needs no reflash.
- **The note bands are tagged per highway** (`sensing_2p` / `strike_2p` vs the 1p `sensing` /
  `strike`), so a 2-player capture's left-highway strips are self-identifying instead of
  indistinguishable from 1p ones.

Recipes: `marvin-perf score-capture --slot score-2p-left`, or Record in the viewer with the
`2P SC L` / `2P SC R` toggles on and then
`marvin-perf export-region <cap> --kind score-2p-left --out scores-2pL/`. Note the bands and the
scoreboards should be captured in **separate sessions** — there is no per-stream rate control and
both at 60 fps over-subscribes the wire.

### 2026-07-15 — gameplay screens classified by scoreboard-chrome presence (not whole-frame centroid)

Fixes the intermittent 2-player `in_song_2p` classification dropout. Root cause: every screen
(menu *and* gameplay) was classified by one whole-frame 12×8 centroid. Menus are ~static so that
works; a gameplay frame is mostly dynamic (two scrolling highways, animated crowd, changing
digits), and the `in_song_2p` centroid was built from 5 frames of one session — so a different
song/stage/character drifts many cells, the L1 crosses `t_abs` / the margin collapses, and the
frame reads UNKNOWN. **The scoring chrome is the one static element, and its layout is the 1p/2p
signature.**

New approach (host source of truth `gameplay/present.py`, ported to `gameplay_present.c`): three
masked, per-frame-normalized SAD probes at **fixed nominal coords** — 1p bottom-left score block
(`SCORE_BLOCK_ROI` + `score_block_ref/_mask`), 2p left+right amp panels (`AMP2P_BLOCK` +
`amp2p_{side}_ref/_mask`). Each block's luma is standardized to mean128/std48 over its masked
(static-chrome) pixels (the same quantize `gp_fingerprint` uses → integer L1, exact C↔Python),
then integer L1 vs the baked reference over the mask, / npix. Decision: `1p iff p1≤TAU & p1≤max(pL,pR)`;
`2p iff max(pL,pR)≤TAU`; else fall through to the centroid classifier (which keeps the static
screens). **TAU = 18** (L1-per-masked-pixel).

**Proven offline** (`scan_scoreboard_presence.py`, `tests/test_present.py`): 0/187 corpus frames
misclassified; value-slop envelope (gain/offset/noise) worst present **10.1** vs best absent **28.8**
→ **2.9× margin** around TAU 18; 1p/2p mutually exclusive; pause-overlay partial chrome reads ~0.62
(unit-std), safely absent. **No registration/offset search:** marvin's capture is pixel-locked
native BGR888 (`capture_pipeline.md`), the real 2p snapshots sit at nominal offset, and the
component→HDMI converter re-samples to a fixed raster — so positional slop is a one-time concern
(deferred with the 2p digit reader `amp2p.calibrate`), not per-frame. Per-frame cost is one masked
SAD per block at the existing ~3 Hz classify cadence.

- Host: `gameplay/present.py` (+ `tests/test_present.py`); `export_c.py` bakes `gp_probes[]`
  (block coords, bw/bh, npix, uint8 mask + uint8 mean128/std48 reference) + `GP_PRESENT_TAU` into
  `gameplay_metadata.h`. `test_firmware_classify.py` adds a `present` mode: C `gp_present` == Python
  on every corpus frame (decision exact, SAD within tol). Full suite **80 passed**.
- Firmware (marvin, pending Greg's MPLAB build): `game/gameplay_present.{c,h}` (pure, `-Wall
  -Wextra` clean); `gameplay_engine.c` observe path is **probe-first** — `gp_present` for the
  gameplay screens, `gp_classify` for everything else. `in_song`/`in_song_2p` centroids stay as a
  harmless fallback (probe is authoritative). See marvin journal, same date.
- Data follow-up (unchanged from the open item): capture career/quickplay 1p + cross-session 2p to
  tighten the true out-of-sample margin; the `clean` present ~0.04 is in-sample (ref is a crop of a
  2p frame).

### 2026-07-14 — `in_song_2p` screen class (so the detector knows which highway to read)

Added a **2-player in_song** class to the screen classifier so the system can tell a 1-player
gameplay screen from a 2-player one *from the video* — that's the trigger marvin uses to point the CV
note-detector at the 1p vs 2p-left highway geometry (see the marvin journal, same date, for the
firmware config-select side). Host-only recognizer change:

- `screens.py`: `in_song_2p` added to `SCREEN_IDS` (between `in_song` and `pause_menu`).
- Corpus: the 5 available 2P `in_song` frames (`tools/marvin-perf/snapshots/snapshot-0200..0203` +
  `web-20260714-112721`, all confirmed 720×480 gameplay) copied into
  `firmware/marvin/docs/gh3_screens/` as `in_song_2p__{0200..0203,web0714}.png`; README table updated.
- Re-exported `firmware/marvin/default/src/game/gameplay_metadata.h` (`gameplay export-c`):
  `GP_N_SCREENS` 13→14, `GP_SCREEN_in_song_2p` added, centroid + screen-id table regenerated.
- `tests/test_export_c.py` dimension assert bumped 13→14.

Validation: all 5 2P frames classify `in_song_2p` (margins 2934–5690, well clear of `t_margin`
664); the 1p `in_song` frame still reads `in_song` (dist 0). **`in_song_2p` LOO 5/5** (now a
multi-sample class, unlike the single-sample `in_song`/`loading`/etc.). No 1p→2p or 2p→1p leakage.
Full suite **76 passed**. The 2p screens are visually very distinct (two highways, different stage),
so the whole-frame centroid separates them cleanly.

Caveat: only 5 labelled 2P frames, and they're from one capture session — capture more across
songs/stages to measure real in-class spread (mirrors the existing single-sample-class open item).

### 2026-07-14 — 2-player CV note-detector sense-line calibration (host-only, geometry)

Placed the **note-detector** (`cv_marvin_v1`, spec §4.2) sample points for the two 2-player
highways, mirroring how the single-player sensors sit. This is the note/fret detector, **not** the
gameplay/score readers (that's the separate 2P amp-scoreboard thread also dated today). Worked off
`snapshot-0200.png` (the reference 2P `in_song` frame); overlays rendered host-side, no firmware
change yet.

**What the single-player detector does (established by overlaying its coords on a real rig frame,
`snapshot-0100`):** the sense row sits **mid-highway at y≈311, ~100 px above the target rings**
(strike line y≈410), not on the rings — it reads gems partway down the highway for actuator lead
time. Per fret it samples two 5×5 patches: a **hold** (brightness) point and an **edge** (colour)
point offset ~13 px inward. 1P coords (from `cv_marvin_v1.c`): hold x = {G280,R317,Y355,B393,O430},
edge x = {293,330,368,380,417}, all y=311.

**Model used for the 2P placement.** Each highway is a perspective fan: all 5 lanes converge to one
vanishing point. Greg hand-tuned the **strike-line ring anchors** (bottom of each lane) by eye; I fit
each highway's **vanishing point** from the rails, then projected each ring up its lane to y=311.
Sanity check: t=0.645 up the lane from V to the ring reproduces the 1P hand-tuned coords, so the same
construction transfers. Edge offset = ±0.347·(lane pitch) inward, same sign pattern as 1P (G/R/Y →+,
B/O →−). Lane pitch at the sense row ≈28 px (vs 37.5 px on 1P — 0.71× playfield scale + deeper
foreshortening at this height). Vanishing y fixed to 131 for both; P2's x nudged with Greg to line
the fan onto the neck.

**Locked 2P calibration** (720×480 capture coords; strike y=410, sense y=311):

| | V (x,y) | ring x (strike, G→O) |
|---|---|---|
| P1 (left)  | (223,131) | 135, 179, 223, 267, 310 |
| P2 (right) | (488,131) | 399, 443, 487, 531, 574 |

| | G | R | Y | B | O |
|--|--|--|--|--|--|
| P1 hold (y=311) | 166 | 195 | 223 | 251 | 279 |
| P1 edge (y=311) | 176 | 204 | 233 | 242 | 269 |
| P2 hold (y=311) | 431 | 459 | 487 | 516 | 543 |
| P2 edge (y=311) | 440 | 469 | 497 | 506 | 534 |

**Open / next:**
- **Lead-time caveat.** Same y=311 as 1P was Greg's choice. Because the 2P highways are shorter, y=311
  is proportionally higher up the (shorter) neck than on 1P, so the *time* a gem takes from sense row
  to strike may differ from 1P. May need to drop the row after watching real 2P note-scroll; revisit
  against 0201–0203 + the web 2P capture.
- **Firmware wiring (design, not yet done).** `cv_marvin_v1.c` has a single hard-coded
  `s_sensor_coords[FRET_COUNT]` and one detector state set. 2P needs a second coord set (P2) + doubled
  per-fret state + a 1P/2P mode the detector currently has no notion of. Plan this before editing the
  detector. SENSING/STRIKE strip ROIs + the overlay ring painter are also 1P-only today.
- **Validate the anchors across the other 2P frames** before treating them as rig-final.

### 2026-07-14 — 2-player scoreboard: started with the per-side location (chrome) mask

Kicked off reading the **2-player** amp scoreboards (`snapshot-0200.png` is the reference 2P
in_song frame; 0201–0203 + `web-20260714-112721.png` are the other 4 available 2P frames).
Established geometry vs the single-player block:

- **Two amps, top of frame, ~0.71× the single-player amp's linear size.** Gold-trim boxes measured
  off a labelled grid overlay: **P1 (left) ≈ (132,169)–(189,236)** (~57×67), **P2 (right) ≈
  (514,168)–(573,237)** (~59×69). Same overall amp art/layout as single-player (digit strip on top,
  medallion below), just smaller and relocated. P2's score appears right-aligned to its box like P1
  (the lone "0" hugs the right edge), so the layout may not be mirrored — but each side registers
  independently regardless.
- **Block ROIs chosen (same 68×78 size, per-side origin):** `left (128,164,196,242)`,
  `right (515,164,583,242)`. Both fully contain their amp with a few px margin (right block
  re-centered off an initial too-far-left crop that pulled in the neighbour portrait; true right
  box is (522,170)–(577,235)).

**Approach = per-side chrome registration, mirroring the single-player score-block design** (decision
log 2026-07-09). The amp is static on screen, so **one clean frame** builds each side's chrome
reference; the location mask marks the digit-independent static chrome (box frame + panel texture +
medallion ring, excluding the green digit strip and medallion interior). Registration then absorbs
only minor per-rig offset — validated by synthetic ±N px shifts (as the single-player re-register
check did), **not** by a multi-frame corpus (Greg: the amp position doesn't change).

Paint templates written for Greg to mask (6× upscale, like `score_block_ref_6x.png`):
`data/scores/amp2p_{left,right}_ref_6x.png` → he returns `amp2p_{left,right}_mask.png`.

**Location mask DONE + validated (host-only).** Greg painted both masks (magenta over the static
dark panel, excluding the digit strip + medallion interior) directly onto the 6× templates → renamed
`data/scores/amp2p_{left,right}_mask.png` (1338/5304 px each). Clean per-side references committed
as `amp2p_{left,right}_ref.png` (68×78 crops of `snapshot-0200`). New `gameplay/amp2p.py` (self-
contained; mirrors the single-player chrome design rather than refactoring `score.py`):
`AMP2P_BLOCK` in `metadata.py`, `load_amp2p_mask` / `build_reference` / `calibrate` (masked,
per-frame-normalized SAD offset search). Validation:
- **Cross-frame lock:** all 5 available 2P frames register to (0,0) both sides — position is
  identical everywhere, ROIs are dead-on.
- **Synthetic-shift recovery:** **289/289** over the full ±8 px grid, both sides.
- **A2D robustness:** **135/135** across gain 0.7–1.3× × offset ±30 × shifts, both sides
  (normalization absorbs hardware colour variation).
The panel-only mask localizes perfectly despite being a low-contrast region (the panel pixels
adjacent to the carved-out digit-strip/medallion boundaries carry the position signal).

**Next (needs sign-off; separate phases):** (1) 2P **digit** reader — the green LED font at ~0.71×
scale, right-aligned in each registered block; needs its own digit band offsets + green-font
templates (the single-player white-font banks won't transfer). (2) 2P **multiplier**/**streak** if
present in 2-player. (3) A committed pytest fixture for the registration (crops of the 5 frames) and,
later, the firmware export/port. No firmware work yet — this is host-only, matching the phase-5
host-first pattern.

*(Item 1 done 2026-08-09 — see that session-log entry; the registration fixture came with it, as
`tests/test_amp2p.py`. Item 2 turned out to be two separate things: there is no multiplier digit
in the 2p amp at all, and no streak odometer either — both now tracked in Open questions.)*

### 2026-07-10 — streak: audited/cleaned the training set + units-wrap tens carry (fixes the carry lag)

Two linked changes after Greg reviewed the corpus by eye:

1. **Training-set audit.** The auto-selected units exemplars had let **mid-roll** samples in — a
   rolling wheel momentarily reads as a digit but the glyph is half-transitioned, and averaging those
   smears the template. Greg reviewed every crop; removed the mid-roll ones and relabelled full-label
   crops whose ones was rolling to exclude just the units (`08x`, `11x`, `16x` — keeping their settled
   tens/hundreds). Corpus 76→65. **Future policy (Greg): a digit is usable only if it reads identically
   across neighbouring frames** — any frame-to-frame change = not settled. (To implement in the
   auto-growth tooling.) After cleanup some units digits are thin (2:3, 3:2, 4:3) — re-grow with the
   stability gate if they under-detect.

2. **Units-wrap tens carry.** Cleaner data made units-9 detect better, which *exposed* the tens carry
   lag: at a ten-crossing the units rolls to 0 immediately but the debounced tens lagged, so the
   display dipped 49→40→50. Fixed by stepping the tens the instant the units wraps — a confident units
   read that dropped by ≥`STREAK_WRAP_MIN`(5) bumps the *current* (debounced) tens by one, cascading to
   the hundreds. **Safe against the earlier lock**: it works off the debounced digits and fires only on
   a genuine wrap, so it never re-rounds a stale value — if a wrong-high tens was corrected downward,
   the units isn't wrapping, no carry fires, and the correction stands. Verified by a regression test
   (a 2-frame tens misread commits ~141 then recovers to 11x, *not* stuck) and over the capture:
   carry-lag dips 30→0, 0 implausible jumps, anchors exact, 4 transient recoveries, 76 tests green.

### 2026-07-10 — integer-math rewrite of the score + streak readers (bit-exact C↔Python; fixes a coverage OOB bug)

Rewrote the shared coverage pipeline (luma → ink threshold → grid resize → per-cell coverage) to
**pure integer** so the firmware reproduces the host prototype bit-for-bit. Why it mattered for
*accuracy*, not just speed: the firmware extracted coverage in float32 while the templates were built
and validated in Python float64, with different rounding (`np.round` half-to-even vs C half-up,
`np.linspace().round()` vs `lround`) — so the device's reads diverged from the host-validated reads
(more hardware misreads than the host showed). Now:
- luma = `B*29+G*150+R*77` (no `/256` — cancels in the relative threshold);
- ink threshold = a rational `NUM/DEN` of the cell's min..max range (score 3/5, streak 1/2), tested as
  `DEN*(luma-lo) ≷ NUM*(hi-lo)`;
- grid edges = `(2*i*extent+n)/(2*n)` (round-half-up); per-cell coverage = `(count*255 + npx/2)/npx`.
No float remains in the readers (the ARM926 has no FPU). The screen classifier / song matcher (a
separate float-normalization path) are untouched — out of scope.

**Found + fixed a coverage out-of-bounds bug** doing this: when a glyph's ink bbox is smaller than the
grid (e.g. a 6-row bbox stretched to 16 grid rows), the last grid cell's block runs *past the bbox*.
numpy silently clamps the slice to empty (→ coverage 0), but the C loop read one row/col beyond, into
the **unused tail of the scratch buffer** (stale data) → wrong coverage on short glyphs. Fixed by
clamping `yb→rh`, `xb→rw` in both C loops (host made explicit to match). This was present in the float
version too — it's why the streak cross-check kept a `known`-flag mismatch on cap4247, and it means
the device had real misreads on short glyphs (now gone).

Cross-check is now **strict bit-exact** (C == Python on presence, every digit, every confidence flag,
all frames). Re-validated: score LOO 54/54, streak in-sample 154/154, capture anchors exact, units
detection intact (0/6/9 = 434/335/256). Digit banks in `gameplay_metadata.h` rebuilt with the integer
coverage. Remaining edge: a 2-frame song-end slide-out can re-seed on misaligned digits (capture max
384) — a brief-dropout-reseed case, separate follow-up.

### 2026-07-10 — streak dashboard label: show "0" at reset (fix stale value not clearing)

The dashboard streak wasn't clearing at song start or after a miss — it kept showing the pre-reset
count. `ScreenDashboard_ApplyStreak` set an **empty** string for streak 0, and `lestring_set_utf8("")`
doesn't repaint/clear the previously-rendered glyphs (the score/status labels never hit this — they
always render a non-empty string). Now it always renders the number, so 0 shows "0" and repaints. Both
reset paths already posted 0 (song start: `run()` posts `PostStreak(0)`; miss: the tracker → 0 once the
odometer isn't locked) — the label just wasn't clearing. Firmware-only, `screen_dashboard.c`.

### 2026-07-10 — streak corpus growth: fix units-9/0/6 detection (units-only exemplars mined from the capture)

The units wheel (dark-on-light, rolling) has a fat distance tail, so a correct digit from an
under-sampled template can exceed the confidence gate and be dropped. Most visible on **units-9** (4
exemplars → real 9s matched their own template at dist ~12300 > gate 9500, so nearly all confident 9s
were discarded — "rarely detects 9"). Fixed by mining the 6549-frame capture (unused until now):
select frames by units-cell `argmin==d` spread across the run, hand-verify via montage (argmin was
reliable — every candidate was truly that digit), and add **30 units-only crops** (10 each of the
three thinnest: 9/0/6).

Labelled `streak__xx<u>__cap…` (`x` = a place not used for labelling) so they feed **only** the
dark-on-light units bank. First attempt used full 3-digit labels, but these frames were picked for
units clarity — their tens/hundreds were often mid-roll, and feeding those into the white-on-dark
bank pulled a clean 7 and 1 toward 9 (2 in-sample cross-confusions). Units-only labels keep the
white-on-dark bank the clean original 46; `streak_from_filename` already maps `x`→None so those places
are skipped. (Reusable pattern for future wheel-specific growth.)

Result: units-9 median distance **12300 → ~5700** (clears the gate); capture confident reads units-9
**0 → 256**, units-0 435, units-6 335; in-sample **154/154**; tracker unchanged (0 big jumps, max 304,
anchors exact); 74 tests. Corpus 46 → 76. Remaining thin units digits (2/3/4 at 3-4 exemplars) read
fine and were left; grow them the same way if a gap shows up.

### 2026-07-10 — streak tracker: per-place debounce + plausibility clamp (fixes lock, 0↔8 flicker, carry blip)

Reworked the streak tracker to fix a reported **lock** and two failures it surfaced. The prior
monotonic forward window `[last, last+30]` couldn't represent a value *below* `last`, so a tens
misread that nudged `last` up locked in and every units roll then forced the tens higher (11x read as
14x, 12x→15x…). Replacing it with plain "confident read trumps" fixed the lock but unmasked two
pre-existing misreads the window had hidden: the **tens 0↔8 flicker** (the slashed zero's top loop
closes under A2D aliasing → a tens "0" matches the 8-template better and reads 8 *confidently*,
alternating frame-to-frame, 100↔180) and a **carry-roll blip** (at 99→100 the mid-roll hundreds wheel
reads 9 confidently for ~2 frames → a 900 flash).

Fix = two temporal/physical guards, both symmetric so they correct downward and **never lock**:
- **Per-place debounce** (`GP_STREAK_DEBOUNCE {2,2,1}`): a *changed* digit commits only after N
  consecutive confident reads. Slow wheels (hundreds/tens) N=2 → single-frame flips (0↔8) never
  stick; units N=1 → stays responsive (debouncing the fast wheel would freeze it). A committed change
  carries (unreadable lower places → 0); an unreadable place holds.
- **Plausibility clamp** (`GP_STREAK_MAX_STEP 50`): reject a committed per-frame value change > 50 —
  a streak can't gain that much per ~3 Hz poll, so it's a misread that cleared debounce (the 2-frame
  carry hundreds read). The seed bypasses it (a real reappearance jumps straight in).

Over the 6549-frame capture: **0 implausible jumps (94 → 15 with debounce alone → 0 with the clamp),
max back to 304, anchors exact.** `test_streak.py` gains flicker/debounce/clamp cases (74 pass).
**Still open — units-9 detection:** the units wheel (dark-on-light) has a fat distance tail, so a
correct 9 (dist ~12300) exceeds the `UNK_DIST` gate and is dropped; needs more units exemplars — a
corpus-growth pass mining the capture is next.

### 2026-07-10 — note-streak counter (odometer OCR + confident-read tracker) → dashboard label

Added the last scoring-block value: the **note streak**, a 3-tumbler mechanical odometer (note icon
+ hundreds/tens/units) right of the multiplier. Two halves, mirroring the firmware split:

- **Stateless reader** (`read_streak`, `score.py`). **Presence = the odometer is *locked at its
  final position*, detected from the note icon — NOT the digits** (Greg): the whole counter *slides
  in from the bottom, overshoots, and bounces down* to settle (≈20 frames; `score-0335.png` is the
  reference locked frame), and mid-slide the digit cells are misaligned → garbage. The fixed gold
  note-icon glyph is a position-sensitive fiducial: its coverage matches the locked template (L1 <
  `NOTE_MAX_SAD`=2000) only when settled — cleanly separates locked (≤~1300) from slide/overshoot
  (~3200–10000) and absent (~17000+). Three fixed digit cells; polarity differs — **units is
  dark-on-light** (highlighted), **tens/hundreds white-on-dark** (like the score). Per cell: ink by
  polarity → 16×10 coverage grid → integer-L1 argmin over a **per-polarity** 0-9 bank; a cell is
  **unreadable** when best-L1/margin fail the gate (mid-roll). *All three wheels roll* (Greg), so no
  place is trusted per-frame. ROIs are consistent 8×15 digit cells + a 10×15 note cell (Greg's box
  refinement → 100 % in-sample confident-digit accuracy). All positions are **absolute** in the
  720×480 frame (fixed rig; the score reader's chrome registration exists but is unused in v0).
- **Stateful tracker** (`StreakTracker`). **Presence is binary: not present ⇒ the streak reset ⇒ 0**
  (immediate — the counter only vanishes on a miss/below-threshold; the sole reset path). While
  present, **a confident wheel read is authoritative for its place** (Greg: "a clear read of a digit
  trumps the automatic rollover logic") — it always wins, even if the value drops, so a misread is a
  self-correcting one-frame blip, not a permanent lock. An unreadable (rolling) wheel is *filled*:
  it holds its last digit, or resets to 0 when a higher place changed this frame (a carry). ≥2
  confident wheels seed a first appearance. This **replaced an earlier monotonic constrained-search**
  (`[last, last+30]`, ties→smallest): that forward-only window could never represent a value *below*
  `last`, so a single tens misread that nudged `last` up got locked in — every subsequent units roll
  then forced the tens upward (11x read as 14x, 12x→15x…). Confident-trumps fixes that.

**No filename ground truth** (unlike the score), so a **hand-labelled odometer corpus** (`data/streak/`,
46 block crops read from capture montages, `x` = mid-roll wheel) builds the banks. Validation over the
full **6549-frame capture**: hand-read anchors exact, **0 non-reset decreases**, correct slide-gating
(0 through the slide, reads only once locked), max 304; **100 % in-sample confident-digit accuracy**
with the refined 8×15 cells.

**Known follow-up — the tens 0↔8 confusion (deferred, to fix with a per-place debounce).** Dropping
the monotonic window unmasked a *pre-existing* systematic misread the old window had accidentally
hidden: the odometer's zero has a center slash, and A2D aliasing closes its top loop on ~half the
frames, so a tens "0" wheel genuinely matches the **8** template better (dist 4590 vs 6885) and reads
8 *confidently* — alternating 0,8,0,8 frame-to-frame (100↔180, 200↔280, 300↔380; a hundreds variant
flashes 900). It is a real per-frame ambiguity, not a template/threshold bug, so it needs **temporal**
handling: require a *changed* confident digit to persist ≥2 consecutive reads before committing
(debounces the alternation; a sustained real change still commits with ~1-frame lag; cannot lock since
it follows reads up *or* down). Committing the lock fix first; debounce is the next change.

`metadata` (note + cell ROIs/thresholds) + `export_c` `GP_STREAK_*` block (note-icon template + two
uint8 digit banks + geometry + tracker params); pure-C `gp_read_streak` (stateless, cross-checked) +
`gp_streak_track`/`_reset` (state owned by the caller) in `gameplay_score.c`. Cross-check
(`test_firmware_classify` `streak`): C == Python on **presence** (exact) + every **confident** digit,
all 46 frames. Caveat: the per-wheel `known` flag can differ on a wheel sitting on the confidence
threshold — Python rounds the coverage grid half-to-even (`np.round`), the C port half-up, so a 1-LSB
coverage delta flips a borderline gate; benign (the tracker fills an unknown wheel anyway). **Pending
fix: make the coverage integer end-to-end** (midpoint threshold since ink_frac=0.5, integer grid
edges) — removes all FP from the reader (the ARM926 has no FPU) *and* the rounding-mode gap, giving
bit-exact parity; deferred so the working version can be tested/committed first. `test_streak.py`:
corpus reader sanity + synthetic tracker cases. Engine: `game_state_t.streak`, tracker advanced on
in_song / reset when leaving gameplay (log adds `streak<s>`). Dashboard: feed applies `DASH_EVT_STREAK`
→ `ScreenDashboard_ApplyStreak` → `Marvin_LABEL_DASHBOARD_ROBOT_Streak` (shows "0" at reset — see
2026-07-10 label-clear fix). **Reset
behaviour (w/ Greg):** score/mult/streak clear the instant a run is *requested* (top of `run()`, not
on reaching gameplay); a broken streak mid-song blanks immediately (odometer not locked → 0);
**everything persists after the song ends** (next run's request is the only reset). `play_until_done`
tracks **peak streak** + **final score** over the run (logged for the eventual `Results_Append`).
Full suite **71 passed**. **Pending Greg's MPLAB build.** Deferred: integer-coverage rewrite;
career-font score mode.

### 2026-07-09 — score multiplier reader (colour-count classifier) + dashboard buttons

Added the in-song **multiplier** reader (1x/2x/3x/4x). The medallion glyph has a fixed colour per
value (2x gold, 3x green, 4x purple; 1x = no digit / dim portrait), so it's read by **colour, not
shape** (Greg): count bright, saturated purple/green/yellow pixels in a **small 16×24 patch** over
the digit (`SCORE_MULT_ROI`, ~6× smaller than the glyph), argmax → 4/3/2, floor → 1x. Validated on
the 6549-frame capture: sensible distribution (mostly 4x, 1/2/3 at starts/resets) and the value
only climbs by 1 / resets to 1 (**1 anomalous transition in 6548**). Mode-independent (colours
identical training/career) — one classifier, no per-mode data, no segmentation.

Host `read_multiplier` (`score.py`) + `metadata` ROI/thresholds; `export_c` emits a tiny `GP_MULT_*`
block; pure-C `gp_read_multiplier` (`gameplay_score.c`) with independent integer colour counts
(matches the Python's non-exclusive masks). Cross-check (`test_firmware_classify` `mult` mode):
`gp_read_multiplier` == `read_multiplier` on all 54 corpus frames. Engine: `game_state_t.multiplier`
read on in_song (log `score N x<m>`). Dashboard: `game_controller` posts it (reset 1x at song
start), the feed consumer applies `DASH_EVT_MULTIPLIER` → `ScreenDashboard_ApplyMultiplier` which
highlights the active `Marvin_BUTTON_DASHBOARD_ROBOT_{1,2,3,4}X` (clears the others; the ApplyFret
latch pattern). Full suite **61 passed**; `gameplay_score.c` clean. **Pending Greg's MPLAB build.**
Streak counter (`DASH_EVT_STREAK` stub) + career-font score mode remain deferred.

### 2026-07-09 — score reader ported to marvin firmware (M9 Phase 3, code-complete pending build)

Froze the proven proportional score reader into firmware, mirroring the classifier/selection/song
port pattern. `export_c.py` gained a score block (pulls `build_score_catalog` + `SCORE_DIGIT_BAND`):
`GP_SCORE_*` geometry/seg-params, a per-mode `gp_score_<mode>_tmpl[10][140]` **uint8** template
array (row d = digit d's coverage mask), and a `gp_score_modes[]` table carrying the digit band +
templates — **mode-parameterized** (`GP_SCORE_MODE_TRAINING`; career appends later, no API change).
New pure-C `game/gameplay_score.{h,c}`: `gp_read_score(frame,w,h,mode,*out)` — band ink mask
(relative threshold), gap-based column segmentation, per-cell ink-coverage glyph, **integer L1
argmin over the 10 templates** — a faithful port of `score.py`. **No chrome registration in v0**
(fixed band; the rig is stable and host registration always resolved to (0,0)). Wired into
`gameplay_engine` (new `game_state_t.score`, read on `GP_SCREEN_in_song`, `GAME: in_song / score N`
log) + `user.cmake`.

Matcher note (Greg): first ported a heavy 270-exemplar float 1-NN + per-glyph normalization (554 KB
header, soft-float) — Greg pushed back ("10 fixed glyphs shouldn't need this"). Diagnosed on the
6549-frame capture: a plain binary mask misread only 8→5/3 and 0→3 (thin closed-loop strokes lost to
1-bit). Switched to per-cell **ink coverage** (0-255) with **10 averaged per-digit templates** +
integer L1 — same accuracy (0 monotonic violations over 6549), integer-only, header 554 KB → 157 KB.

Cross-check: extended `test_firmware_classify.py` with a `score` driver mode that compiles
`gameplay_score.c` and asserts `gp_read_score` == `read_score` on all 54 corpus frames — **passes**
(the C is byte-faithful). Full suite **60 passed**;
generated header compiles clean under `cc -std=c11 -Wall -Wextra`; `gameplay_score.c` clean with
`-Wall -Wextra`. **Pending Greg's MPLAB build + on-hardware check** (GAME log shows the score during
training in_song). Follow-ups: career (green) font mode, continuous in-song score tracking +
dashboard/results wiring, optional on-device chrome registration.

### 2026-07-09 — score reader reworked: proportional (gap-based) segmentation + 6549-frame capture corpus

Greg captured the scoring block at full rate via the new `marvin-perf score-capture`/REGION
stream (`captures/web-20260709-142721`, 6549 frames climbing 250→101720) and flagged that the
training font is **not monospace** — right-aligned, but digit x-positions shift with the value
(a `1` is narrower than an `8`). Confirmed on the capture: fixed-pitch cells can't sit on the
digits, but the digits are cleanly **gap-separated** (no touching). So the fixed-pitch grid +
blank-class was replaced with **gap-based segmentation**: threshold the digit band (relative
`min+0.6·range`, gain/offset-robust), split the column ink profile into digit blobs (right-to-
left), resize each blob's bbox to a canonical grid, 1-NN-match. Digit count = blob count (no
fixed N, no blank class). Chrome-of-the-block registration retained; block crops are embedded
into a full frame (`_ensure_full_frame`) so the absolute ROIs / registration work unchanged.

Corpus: extracted the 6549 REGION frames (`export-region`), read **54** spanning 3–6 digits off
montages (values cross-checked by the monotonic ordering), committed as
`score__training__NNNNNN__capNNNN.png` (replacing the 9-frame set; the capture itself is
gitignored). `metadata.SCORE_DIGIT_BAND` (block-relative digit band) replaces the old fixed
`SCORE_ROI`; `score.py` reworked (`_segment_digits`/`_glyph_vec`, `read_score`/`build_score_catalog`);
`evaluate.py` gained the gap-based `score_eval` + `score_monotonic_eval`; `cli.py` a
`score-monotonic <dir>` verb. Results: `score_eval` **54/54 exact, 270/270 per-digit, LOO 54/54,
A2D 99.7%, re-register 100%**; **`score-monotonic` over all 6549 = 0 violations** (100% clean).
Full suite **59 passed**. The earlier fixed-pitch reader + its held-out 10/16 finding is
superseded (root cause: proportional font). Career font / firmware port still deferred.

### 2026-07-09 — phase 5 built (score reader, host-only, training-mode white font)

Stood up the open-ended **score** reader (gameplay phase 5 / marvin M9 Phase 3). New
`gameplay/score.py`: `ScoreConfig` / `DigitTemplate` / `ScoreCatalog` / `ScoreCalibration` /
`ScoreResult`; `build_score_catalog` (per-cell exemplars from the labelled corpus),
`calibrate_score` (two-stage register-once), `read_score` (search-free per-frame 1-NN).
`metadata.py` gained `SCORE_ROI["training"] = (139,315,199,333)`, `N_SCORE_DIGITS=6`, and
`score_from_filename`; `corpus.py` gained `load_score_corpus`; `evaluate.py` gained
`score_eval` (clean exact + per-digit + LOO + A2D-slop + re-registration) wired into
`run_report`; `cli.py` gained an `in_song` branch in `classify` + a `read-score` debug verb.
Curated **9 training-white in-song frames** from `marvin-perf/snapshots/` into
`data/scores/`, value in the filename (all 10 glyphs covered).

Design arc this session (see decision log): started with a per-frame per-cell offset search
(clean 9/9) but that models per-frame jitter, which Greg corrected — position is **stable per
rig**, so switched to **register-once + search-free read**. A single global field offset
per frame under-fit (8/9, per-digit sub-pixel variance); a wide *per-cell* registration
**aliased cells onto neighbouring digits**; the fix is two-stage registration (whole-field
wide → per-cell small). Averaged centroids blurred 8/3/0 → switched to **1-NN over
exemplars**. Result on the ~9-frame corpus: **clean 9/9 exact, 54/54 per-digit, A2D
(gain/offset/noise) 100%, position re-registration 100%**; **LOO 6/9** is the small-corpus
ceiling. Scale confirmed a non-concern (Greg). Full suite **56 passed** (+5). Host-only — no
firmware export or career-font work yet.

**Expanded held-out test (same day) — the thin corpus overstates accuracy; margin separates
right from wrong.** Rather than hand-label more, used the fact that Claude reads these frames
reliably (and scores are **monotonically increasing within a play** — an independent
consistency check) to auto-label the whole `0097–0129` training run: two plays (`0097–0108`
1138→17728, then a restart via the `0109–0113` menus, `0114–0129` 9056→12236), 25 frames after
dropping `0123/0126/0128` (screen tearing through the digits — Greg). Reading these with
templates+registration from the **9-frame** corpus only: **total 19/25, held-out (16 new
frames) 10/16.** All 6 failures are low-margin (≤4.7) digit confusions, overwhelmingly **→8**
(6→8 ×3, 9→8, 5→8) plus one dropped leading digit — 1-NN sparsity (≈4 exemplars/digit; a stray
"8" exemplar sits near everything in the low-res cell space). **Every correct read has margin
≥10.1 and every miss ≤4.7**, so a confidence gate (~7) rejects all errors with zero false
rejects — useful for the closed-loop (reject → re-read next frame). Takeaway: the fix is
**more exemplars** (the marvin-perf score-crop capture), not an algorithm change; the clean
9/9 was optimistic. Screen tearing is a real capture artifact to reject downstream.

**Registration reworked to key on the block chrome (same day).** Settled the marvin-perf
**capture region = `SCORE_BLOCK_ROI (114,309,210,414)`, 96×105** (Greg tuned it live to fit both
modes' full scoring block — score + multiplier + streak). Greg's insight: that block's chrome is
a far better positioning anchor than the digits (strong straight edges, static, mode-shared).
Replaced the digit-template two-stage `calibrate_score` with **chrome registration**: median
block luma reference + a **hand-drawn mask** (`data/scores/score_block_mask.png`) marking the
static chrome (panel/ring/border, hard-excluding the full digit row and the medallion interior),
matched by masked normalized-SAD over an offset search. Validated: all 69 real frames (25 train +
44 career) register to (0,0), shift recovery exact to ±7 px, mode-independent. Reading unchanged
(19/25, 10/16 held-out — template-limited, pending more data). Full suite **59 passed**. Also
added `SCORE_BLOCK_ROI`/`SCORE_CHROME_BOX` to metadata and `load_chrome_mask`/`build_chrome_reference`
to `score.py`. Next: the marvin-perf full-rate score-block capture (firmware + host) to enrich
the digit templates.

### 2026-07-07 — prototype: title **length + prefix** song reader (`songselect_prefix.py`) — negative result

Greg flagged the `song_select` reader mis-reading certain songs on real hardware and asked
whether a minimal-OCR "length + prefix" approach could do better. **CSV analysis** of the
64-song catalog (`data/games/gh3-wii/songs.csv`): the full title charset is 54 glyphs, but the
identifying signal is up front — **`(title length, first 2 case-insensitive chars)` uniquely
separates all 64 songs** (7-char case-insensitive prefix alone also does; first-2 alone leaves
15 collision groups, e.g. "The…" ×5, so length is the tie-breaker).

Built a **parallel** reader (`gameplay/songselect_prefix.py`, original `songselect.py`
untouched): reads only the **title sub-band** (top ~30% of the slot ROI; artist/year is below),
and matches on two features — normalized **ink extent (length)** + a low-res normalized luma
grid over the **leading window** of the title — with the same read-time offset search. Distance
= `prefix_L1 + length_weight·|Δlength|`.

**Result — it does not beat the whole-slot match; it's a useful negative:**
- Clean corpus **64/64** (ties baseline). But **worst inter-song margin collapses** (~0.04–0.79
  vs baseline **5.63**), and analog-slop robustness is **~88% at best vs baseline 99.1%**.
- Category breakdown pinpoints why: gain/offset/noise **100%**, but **scale ~58–60%**,
  **translate ~85%**. Rendered title lengths are packed **~1 px apart** (min gap 0 px on a
  330 px ROI), so the length feature that must break the many shared-prefix ties has **no margin
  against overscan/underscan (±3% ≈ ±10 px)** — exactly the analog slop we can't avoid.
- **Root cause / takeaway:** the information-theoretic sufficiency of (length, first-2-chars)
  does **not** survive as *analog bitmap* features. Many titles share leading glyphs (matches
  the CSV collision groups), so the prefix grid barely separates them, and the length tie-breaker
  is the single most scale-fragile feature. The whole-slot match wins because it uses the entire
  title's ink as independent evidence.
- **The real "minimal OCR" path is discrete, not bitmap:** classify the first ~2–3 glyphs to
  actual character *labels* + read an integer length → exact-key lookup, immune to
  analog-distance collapse. That needs a glyph classifier (segmentation + per-glyph templates;
  ~26 glyph classes cover all first-3 prefixes) — a bigger lift with a one-frame-per-song data
  constraint, deferred pending Greg's call. **Caveat:** the corpus is clean digital captures
  (baseline already 100%/99.1% here), so it does not reproduce the on-hardware mis-reads Greg
  sees — the synthetic slop envelope is only a proxy. Files: `gameplay/songselect_prefix.py` (new).

**Follow-up (same day) — the port is faithful; the misreads are an integration gap (active-area
offset not applied by the CV readers).** Greg gave four real on-hardware misreads
(paint_it_black→cult_of_personality, same_old_song_and_dance→my_name_is_jonas,
cult_of_personality→kool_thing, raining_blood→before_i_forget) and noted the corpus is exported
directly from marvin — *the exact data marvin sees*. Ran the port down:
- **On the exact corpus frames, all four read correctly with large margins** (wrong target 15–82
  L1 away; raining_blood→before_i_forget is the tightest at margin 13). So it is **not** value/position
  slop as modelled, not a close-template problem on the settled frame.
- **The C port is byte-faithful.** Built the `gp_read_song` C unit and compared to Python over
  **all 64** songs → **0 mismatches** (the existing cross-check only spot-checks `songs[::8]`, which
  skips all four failing indices 14/22/34/35 — a real test gap). The committed `gameplay_metadata.h`
  is **identical to a fresh export** (not stale). Pixel format matches: the CV path uses a dedicated
  dense **BGR888 3 B/px** ISC capture (`isc_capture.c`), and `GP_BPP`/`GAME_BYTES_PER_PIXEL`=3 —
  not the display BGRX32. Resolution is 720×480 (Wii 480p), matching `GP_CANON_*`.
- **First hypothesis (active-area drift) — rejected by Greg:** the system is stable (same rig
  since start, constant screen size/offset) and the raw frame buffer is never cropped (active-area
  detection only picks a region to scale for *display*; the 720×480 raw data is untouched). So
  corpus == runtime pixels, and drift is not the cause.
- **ACTUAL ROOT CAUSE — the song ROIs are mis-registered; they don't sit on the selected title.**
  Measured the selected (gold) title's y-position across the whole main list: it's **y≈246 for every
  song 1–38** (rock-steady), and **y≈291 for the first song (slow_ride)** — the list can't scroll up
  past the top, so song 0 sits ~45 px lower. But `SONG_SLOT_ROI` is **y=145–192** and `SONG_FIRST_ROI`
  is **y=178–225** — both **~55–100 px too high**. Cropping the exact sampled region confirms it
  visually: on the `paint_it_black` frame the ROI contains *"The Seeker / THE WHO"*, on
  `cult_of_personality` it contains *"3's & 7's"*, etc. **The reader has never matched the selected
  song — it matches whichever unselected neighbour sits ~2 rows above.** That scores 64/64 on the
  corpus only because each song's upstream-neighbour is unique *at the one scroll position the corpus
  captured*; the neighbour is a scroll-state artifact, not a stable property of the selection, so it's
  fragile on hardware (and the confusion targets are just other songs with a similar mis-sampled
  region). This is the "different y for song 0" concern Greg raised: the code *does* special-case
  index 0 with a lower `FIRST_ROI`, but layered on a base ROI that is itself in the wrong place.
- **Fix (validated in the prototype):** move the ROIs down onto the actual selection — SLOT ≈
  **y=238–285**, FIRST ≈ **y=283–330** (the +45 px first-song offset). Re-run over the corpus:
  clean **64/64**, slop **99.1%→100%**, and **worst inter-song margin 5.63→26.7 (≈5×)**. The tiny
  5.63 margin was the fragility. This is a **data fix in the ROI constants** (`metadata.py` →
  re-export `gameplay_metadata.h`), not an algorithm or OCR change — and it explains the misreads
  without any drift. TODO before landing: (1) nail the exact bands data-driven per setlist (verify
  bonus/avalancha positions); (2) confirm the *last* songs' runtime y (list can't scroll down past
  the end either — the corpus shows even `one`(38) at y≈246, which may not be the true end-of-list
  scroll position); (3) widen the C cross-check from `songs[::8]` to all 64; (4) get Greg's sign-off
  (firmware data change). Prototype ROIs still un-committed pending this.

**LANDED (same day, with Greg positioning the bands live).** Confirmed with Greg: only the *first*
row is special (last songs sit at the normal y≈246, verified across both setlists; avalancha/first
at y≈296). Went **title-only** and started the ROI right of the album-art thumbnail. Final,
Greg-approved values (host + re-exported header):
- `SONG_SLOT_ROI = (183, 233, 385, 266)`, `SONG_FIRST_ROI = (183, 282, 385, 315)` — title glyphs
  only, x0=183 (right of art), first song +49 px.
- `SongConfig.dy_search = (-6,-3,0,3,6)` (was ±3). A uniform-shift sweep showed the vertical
  positional budget is set by `dy_search`: ±3 tolerates ≈ −6..+3 px at ≥95%, **±6 tolerates ±6 px
  fully (±9 at 95%)** — chosen for per-rig position headroom. dx unchanged (32-col grid is coarse;
  ±6 already covers it). Cost is fine: the title-only ROI is smaller than the old title+artist one,
  so 25 offsets ≈ the old 15's cost, at a few Hz.
- Results: `song_eval` **64/64 clean, 704/704 slop (100%), worst inter-song margin 5.63→33.68**.
  All four reported misreads now read correctly with large margins (paint_it_black 44.5,
  same_old 51.9, cult_of_personality 85.4, raining_blood 44.5).
- Re-exported `gameplay_metadata.h` (ROIs, `gp_song_dy[5]`, `GP_SONG_NDY 5`, regenerated song
  templates). Widened the C cross-check from `songs[::8]` to **all 64** — full suite **51 passed**,
  C still matches Python on every song. **Pending Greg's MPLAB build + on-hardware confirmation**
  that the four songs (and the rest) now read correctly live. Files: `metadata.py`, `songselect.py`,
  `tests/test_firmware_classify.py`, `firmware/marvin/default/src/game/gameplay_metadata.h`
  (generated). The `songselect_prefix.py` prototype stays as-is (unused; the root cause was
  registration, not the matcher).

**Follow-up — audited all 8 static-list menu ROIs; difficulty_select was significantly
mis-registered, fixed.** After the song fix, checked the highlight-reader bands the same way
(overlay cells on frames + a uniform-shift positional-budget sweep). Seven of eight are well
placed (vertical budget ±7…±24 px, all reads correct); `main_menu` is well-centered but inherently
tight (±5 px — 7 items in 195 px). **`difficulty_select` was significantly off on both axes:**
band `(35,158,245,310)` started ~85 px too far left (sampling the side collage) and its 38 px cells
were shorter than the ~44 px item spacing, so lower rows (HARD/EXPERT) sagged out of their cells —
worst budget ±4 px and 1 slop miss. Measured item centers from the dark glyph rows
(178/222/265/309, even 44 px spacing) and re-registered to **`(120,156,280,331)`** (Greg set the
160 px width). Result: vertical budget **±4→±18 px**, slop 43/44→**44/44**, still 100 % clean.
Re-exported the header (band + regenerated per-cell baselines); full suite **51 passed**. Files:
`metadata.py`, `firmware/marvin/default/src/game/gameplay_metadata.h`. **Pending Greg's build +
on-hardware check** (difficulty_select selection reads correctly live).

### 2026-07-04 — firmware controller made fully closed-loop (marvin)
Fixed a real navigation misfire in the ported `game_controller` (marvin): after the Wii
remote woke, the `practice_end_menu` exit strummed down 1 + GREEN (→ RESTART) instead of
down 4 (→ QUIT). Two root causes, both in the marvin firmware (not the offline prototype):
observation returned stale state (a `RequestObservation`+fixed-delay+`GetLatest` poll of a
retained "latest"), and GREEN was pressed without re-verifying the cursor landed. Made
observation **synchronous** (`GameplayEngine_Observe` blocks for a fresh post-request
frame; deleted the retained-latest/`GetLatest`/`SetObserveEnabled` vestiges) and added
**confirm-before-GREEN with bail-to-recover** (`select_and_confirm`: blind-move the delta,
re-observe, GREEN only when the observed cursor == target on the expected screen; else
recover). Also removed the timing pipeline's `CurrentScreen()` actuation gate (layering
inversion) — the controller owns the actuation window via `TimingPipeline_SetEnabled`. This
mirrors the offline `NavController`'s re-observe-until-match contract; the prototype was
already correct, the port had drifted. Detail in the marvin journal 2026-07-04. Pending
Greg's MPLAB build + hardware validation.

### 2026-06-15 — marvin port Phase 2: selection + song readers
Greg confirmed Phase 1 builds in MPLAB and asked for a log on screen/selection change. Ported
the readers into firmware as `game/gameplay_select.{h,c}` (pure: static-list highlight + song
offset-search match), wired into `gameplay_engine.c` (rate-limited ~10 Hz, logs on screen-or-
selection change, `game_state_t.selection` added). Extended `test_firmware_classify.py` to
cross-check both readers against the prototype on real frames (3/3 firmware tests pass; 51
total). Detail in the marvin journal (2026-06-15).

### 2026-06-15 — marvin port Phase 1: M9 screen-classifier observer
Ported the classifier into marvin firmware as a new `game/` module — `gameplay_classify.{h,c}`
(pure math, host-compilable) + `gameplay_engine.{h,c}` (`game_task` frame consumer →
`xGameStateQueue`), fed by the generated `gameplay_metadata.h`; wired into `app.c` + `user.cmake`,
no MCC regen. Factored the classify math FreeRTOS-free so it cross-validates on a host: a new
`test_firmware_classify.py` compiles `gameplay_classify.c` with `cc` and confirms its screen
decision matches the prototype's `classify_image` on real corpus frames — the port is proven
numerically before MPLAB. 49 tests green here. Firmware build is Greg's. Full detail in the
marvin journal (2026-06-15).

### 2026-06-15 — marvin port Phase 0: metadata exporter
Started the firmware port (decided phasing + soft-float; see decision log). Built
`export_c.py` + a `gameplay export-c [--out]` command that renders all proven recognizer
metadata into one C header: classifier config/centroids/thresholds/screen-ids, static-list
menu geometry + per-cell baselines, and song config/ROIs/64 templates/catalog/warmth
threshold. ~150 KB header; generated output compiles clean under `cc -std=c11 -Wall -Wextra`
(a pytest case writes the header + a stub TU and invokes `cc`). 48 tests green. The header
will be generated into `firmware/marvin/default/src/game/` when Phase 1 (the consumer) lands —
not committed yet (nothing includes it). Firmware C is unwritten; built by Greg in MPLAB.

### 2026-06-15 — phase 4 built (navigator, M10)
Added `navgraph.py` (menu graph as data: `MenuInput`, forward/parent edges, BFS `shortest_path`),
`simgame.py` (the `SimGame` oracle + `SimObserver`/`SimActuator`, with part-absent / sticky /
misfire hooks), and `navigator.py` (`Observer`/`Actuator` protocols, `plan_practice_run` +
`plan_goto`, `NavController` closed-loop). The whole controller collapsed to one rule —
"observe, run the step matching this screen" — which handles progress, the part_select skip,
and RED recovery uniformly. Selections use observed-delta (sticky-safe) and saturate-until-stable
(no blind count). Added a `navigate` CLI (plan + sim trace) and a `CorpusObserver` integration
that drives the full run on real frames through the phase-1–3 observer (0 fallbacks). 43 tests
green. Host-only; the Observer/Actuator seam is the firmware port boundary.

### 2026-06-15 — phase 3 built (song_select reader)
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

### 2026-06-15 — phase 2 built (static-list highlight reader)
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

### 2026-06-15 — phase 1 built
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
