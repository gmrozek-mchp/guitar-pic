# GH3 (Wii) — Navigation Model

Working model of Guitar Hero III: Legends of Rock (Wii) screens and the menu
graph between them. This is the source artifact for the §4.8 game-state work:
the **observer** (M9) classifies the current screen and reads the highlighted
selection; the **controller** (M10) plans button sequences along the graph
edges below. See [`spec.md`](spec.md) §4.8.3 (game catalog & menu metadata).

Screen text is transcribed from operator narration; the referenced screens live
as a renamed corpus in [`gh3_screens/`](gh3_screens/) (see its README for the
name ↔ original-snapshot mapping). Raw `marvin-perf snapshot` captures land in
the gitignored `tools/marvin-perf/snapshots/`. Native capture is 720×480.

**Scope now:** the navigation *structure* — screens, their items, and the edges
between them. The **vision** side is now built: the observer classifier lives in
`game/gameplay_classify.c` (`gp_classify()`), and the controller
(`game/game_controller.c`) uses it closed-loop (see Navigator notes below). This
doc remains the source artifact for the screen/edge model those consume; screen
snapshots stay recorded here as the corpus.

Status: **in progress** — this navigation map is populated screen-by-screen
(`TBD` = not yet captured); the observer + controller that consume it are built.

---

## Conventions

### Inputs
- **Strum up / strum down** — move the selection within a list.
- **GREEN** — confirm / enter the selected item.
- **RED** — back up one level.

(Bit names on the wire are the gameplay fret/strum masks; the controller emits
the same bits for menu navigation — the fretboard MCU doesn't distinguish menu
from gameplay. §4.8.4.) Some menus also use the `+` button (pause) and possibly
others beyond fret/strum; the full controller is actuatable in principle —
fauxmote emulates the complete guitar extension incl. `+`/`−` — the actuator
command protocol just needs to carry those inputs.

### Highlight paradigms
How "selected" is presented drives what the observer reads. Three kinds so far:

- **static-list** — the list is painted at fixed screen positions; a highlight
  moves between rows. Observer reads *which row is lit*. (e.g. `main_menu`)
- **fixed-slot** — the selected item sits in a fixed, highlighted slot and the
  list scrolls underneath it. Observer reads *what occupies the highlight slot*.
  (e.g. `song_select`, `venue_select`)
- **per-side** — a 2-player screen split into a left (P1) and right (P2) half,
  each with its **own cursor and own commit state**, advancing *independently*.
  Observer reads each half separately. (e.g. `player_ready_2p`,
  `guitar_select_2p`)

  A whole-frame fingerprint is the wrong mechanism for these: the two halves can
  sit in different sub-states at the same time (snapshot-81552 shows P1 still on
  the character strip while P2 is already on the ready panel), so a single
  centroid built from one capture drifts as soon as the halves are staged
  differently. This is the same failure mode that made `in_song_2p` drop out
  intermittently, fixed by keying on static chrome at fixed coordinates instead
  (see the gameplay journal, 2026-07-15) — per-side probes are the mechanism to
  reuse when these readers get built.

### Edge notation
An edge is `from → to : [inputs]`. `STRUM_DN×n` means n strum-downs. Moving to
list index `i` from the top (index 0) on a static list is `STRUM_DN×i, GREEN`.

Note the controller does not actually issue blind counts — per the closed-loop
rule below it strums the *signed delta from the observed cursor*. `STRUM_DN×i` in
this doc names the **target index**, not a fixed input burst.

**act-then-wait edges.** Most edges are "marvin acts → the screen changes". The
2-player setup screens are not: marvin confirms **its own side only** (it drives
P1/left — the human plays P2), and the screen advances only once the *human*
confirms theirs. Marked `[wait: P2]` in the graph below.

These need a wait state distinct from the normal post-actuation poll: the wait is
**unbounded**, because it ends on a human deciding. Timing out into the generic
RED recovery would be actively wrong — it would back marvin out of the setup flow
while the player is still choosing. Observe until the expected next screen
appears, and surface "waiting for player 2" to the operator instead.

---

## Screen catalog

### main_menu
- **paradigm:** static-list (cursor moves; list fixed)
- **items (top → bottom, index; snapshot = that item selected):**
  0. CAREER — `gh3_screens/main_menu__career.png`
  1. CO-OP CAREER — `gh3_screens/main_menu__co_op_career.png`
  2. QUICKPLAY — `gh3_screens/main_menu__quickplay.png`
  3. MULTIPLAYER — `gh3_screens/main_menu__multiplayer.png`
  4. TRAINING — `gh3_screens/main_menu__training.png`
  5. OPTIONS — `gh3_screens/main_menu__options.png`
  6. NINTENDO WFC — `gh3_screens/main_menu__nintendo_wfc.png`
- **back (RED):** top level — no parent.
- **leads to:**
  - CAREER → TBD
  - CO-OP CAREER → TBD
  - QUICKPLAY → TBD
  - MULTIPLAYER → `guitar_select_2p`
  - TRAINING → `training_menu`
  - OPTIONS → TBD
  - NINTENDO WFC → TBD

### training_menu
- **reached from:** `main_menu` → TRAINING
- **paradigm:** static-list (assumed — 2-item list; confirm)
- **items (top → bottom, index; snapshot = that item selected):**
  0. TUTORIALS — `gh3_screens/training_menu__tutorials.png`  *(out of scope — not modeling this path)*
  1. PRACTICE — `gh3_screens/training_menu__practice.png`
- **back (RED):** `main_menu`
- **leads to:**
  - TUTORIALS → `tutorials_menu` _(out of scope)_
  - PRACTICE → `song_select`

### tutorials_menu  *(out of scope)*
- **snapshot:** `gh3_screens/tutorials_menu.png`
- **reached from:** `training_menu` → TUTORIALS
- Not modeling the tutorials path. **Recovery:** if the controller lands here by
  mistake, press RED to return to `training_menu`.

### song_select  *(shared node)*
- **snapshot:** `gh3_screens/song_select__00_slow_ride.png` (first song "Slow Ride" selected)
- **reached from:** `training_menu` → PRACTICE, and `venue_select` on the 2-player
  path. Expected to be the **same screen** reused by other modes (CAREER,
  QUICKPLAY) — confirm as those paths are walked.
- **paradigm:** fixed-slot (selection fixed on screen; song list scrolls under it)
- **items:** see [Song catalog](#song-catalog-main-setlist) below; index 0 = "Slow Ride".
- **select song i:** `STRUM_DN×i, GREEN` (from the top of the list).
- **BLUE fret:** toggle to the **bonus** setlist; **YELLOW** returns to the main setlist.
- **back (RED):** returns to the entering menu (path-dependent — `training_menu`
  on this path).
- **leads to:** `part_select` (may be skipped for some songs — TBD).
- **note:** assumed identical across modes; when walking CAREER, verify the
  *song set* matches (tiers / locked songs may differ even if the layout is shared).
  Same caveat on the 2-player path — the layout is expected to be shared, but the
  available song set there is unconfirmed (Open items).

### part_select  *(variable — song-dependent)*
- **reached from:** `song_select` → GREEN on a song.
- **paradigm:** static-list (cursor moves).
- **items:** **song-dependent** — the offered parts vary by song. Observed:
  - `{ LEAD (0), RHYTHM (1) }` — `gh3_screens/part_select__lead.png` (LEAD sel), `gh3_screens/part_select__rhythm.png` (RHYTHM sel)
  - `{ LEAD, BASS }` reported for other songs — no snapshot yet
  - the screen **may not appear at all** for some songs (TBD which).
- **select:** LEAD = `GREEN`; second item = `STRUM_DN×1, GREEN` (assumes LEAD is
  always index 0 — confirm).
- **back (RED):** `song_select`
- **leads to:** `difficulty_select`.
- **note:** because the item set varies per song *and* the screen may be absent,
  the navigator can't hardcode this step. It needs either per-song "available
  parts" metadata (candidate: a `parts` column on the song catalog) or a live
  screen read (vision, M9).

### difficulty_select
- **reached from:** `part_select` → GREEN on a part, and `song_select` on the
  2-player path. **Shared outright with the 2-player path** — PRO FACE-OFF puts both
  players on the *same* difficulty, so this is one screen with one cursor, not a
  per-side screen; the existing class and reader apply unchanged.
- **paradigm:** static-list (cursor moves).
- **items (top → bottom, index; snapshot = that item selected):**
  0. EASY — `gh3_screens/difficulty__easy.png`
  1. MEDIUM — `gh3_screens/difficulty__medium.png`
  2. HARD — `gh3_screens/difficulty__hard.png`
  3. EXPERT — `gh3_screens/difficulty__expert.png`
- **select:** `STRUM_DN×index, GREEN` (EASY=0 … EXPERT=3).
- **back (RED):** `part_select`
- **leads to:** `section_select`.
- **note:** likely the same difficulty screen used by other modes (CAREER,
  QUICKPLAY) — treat as a candidate shared node when those paths are walked.
  Default selection on entry may be sticky (last-used) rather than always EASY —
  confirm; if so the navigator should read the current highlight before moving.

### section_select  *(practice — variable per song)*
- **reached from:** `difficulty_select` → GREEN.
- **paradigm:** static-list (assumed; confirm).
- **items:** **song-dependent** (intro / verse / solo / etc.), but **index 0 is
  always "FULL SONG"** — `gh3_screens/section_select__full_song.png`. Only FULL SONG is in scope; other
  sections are ignored.
- **select FULL SONG:** `STRUM_UP×N, GREEN` — strum up to the **top** of the
  list (FULL SONG = index 0), then confirm. Don't assume the cursor enters at
  index 0 (may be sticky / land elsewhere); strumming up saturates at the top so
  it lands on FULL SONG regardless of entry position. `N` ≥ the longest section
  list (or "until the highlight stops moving"). **Assumes no wrap-around** — see
  open items; if the list wraps, read the highlight instead.
- **back (RED):** `difficulty_select`.
- **leads to:** `speed_select`.

### speed_select
- **reached from:** `section_select` → FULL SONG.
- **paradigm:** static-list (cursor moves).
- **items (top → bottom, index; snapshot = that item selected):**
  0. FULL SPEED — `gh3_screens/speed__full_speed.png`
  1. SLOW — `gh3_screens/speed__slow.png`
  2. SLOWER — `gh3_screens/speed__slower.png`
  3. SLOWEST — `gh3_screens/speed__slowest.png`
- **select FULL SPEED:** `STRUM_UP×N, GREEN` — same top-saturating approach as
  FULL SONG above (FULL SPEED = index 0); robust to a sticky default.
- **back (RED):** `section_select`.
- **leads to:** `loading`.

### loading  *(transient — no input)*
- **screen:** `gh3_screens/loading.png`
- **reached from:** `speed_select` (final selection), and on RESTART from
  `practice_end_menu`.
- Not a menu — the song is loading. The navigator **waits** here (no inputs);
  it auto-advances to `in_song`. The observer should recognize it so the
  controller holds instead of mis-firing into the next screen.
- **leads to:** `in_song`.

### in_song  *(gameplay)*
- **reached from:** `loading`.
- **screen:** `gh3_screens/in_song__training.png` (training-mode example).
- The song plays; this is the `gameplay` state where note detection (§4.2,
  cv_marvin_v1) takes over. Not a menu. **Background is mode-dependent** — the
  training background is always the same; other modes (career/quickplay) may use
  different backgrounds, which matters for the observer's `gameplay` classifier.
- **pause:** the guitar `+` button opens `pause_menu` mid-song.
- **leads to:** `pause_menu` (via `+`), or `practice_end_menu` (when the song
  finishes).

### pause_menu  *(mid-song overlay)*
- **reached from:** `in_song` → `+` (guitar plus/pause button).
- **paradigm:** static-list (cursor moves; confirm).
- **items (top → bottom, index; snapshot = that item selected):**
  0. RESUME — `gh3_screens/pause_menu__resume.png`
  1. RESTART — `gh3_screens/pause_menu__restart.png`
  2. OPTIONS — `gh3_screens/pause_menu__options.png`
  3. CHANGE SPEED — `gh3_screens/pause_menu__change_speed.png`
  4. CHANGE SECTION — `gh3_screens/pause_menu__change_section.png`
  5. NEW SONG — `gh3_screens/pause_menu__new_song.png`
  6. QUIT — `gh3_screens/pause_menu__quit.png`
- **select:** `STRUM_DN×index, GREEN`.
- **back (RED):** TBD (likely resumes — confirm).
- **leads to:**
  - RESUME → `in_song` (resume gameplay)
  - RESTART → `loading` → `in_song` (replay current song)
  - OPTIONS → options submenu *(out of scope; RED to recover)*
  - CHANGE SPEED → `speed_select`
  - CHANGE SECTION → `section_select`
  - NEW SONG → `song_select`
  - QUIT → `quit_confirm` (confirmation dialog — not straight to main menu)
- **`+` actuation:** opening the pause needs the `+` button (not a fret/strum
  bit). Actuatable in principle — fauxmote's emulated guitar-extension report
  already carries the `+`/`−` buttons ([`firmware/fauxmote`](../../fauxmote/SPEC.md),
  §3). The marvin→actuator command path is fret/strum-focused today (§4.3);
  carrying `+`/`−` and other controller buttons for menu use is a command-protocol
  extension, not a hardware limit.

### quit_confirm  *(confirmation dialog)*
- **reached from:** `pause_menu` → QUIT (and possibly other QUIT actions — see
  `practice_end_menu`).
- **paradigm:** static-list (cursor moves; confirm).
- **items (top → bottom, index; snapshot = that item selected):**
  0. CANCEL — `gh3_screens/quit_confirm__cancel.png`
  1. QUIT — `gh3_screens/quit_confirm__quit.png`
- **select:** CANCEL = `GREEN`; QUIT = `STRUM_DN×1, GREEN`.
- **back (RED):** likely = CANCEL (returns to `pause_menu`) — confirm.
- **leads to:**
  - CANCEL → `pause_menu` (back, no quit)
  - QUIT → `main_menu`
- **note:** safe default is CANCEL (index 0); a "confirm quit" plan must
  explicitly `STRUM_DN×1` to QUIT, not assume the cursor starts there.

### practice_end_menu  *(after a practice song completes)*
- **reached from:** `in_song` (song done).
- **paradigm:** static-list (cursor moves; confirm).
- **items (top → bottom, index; snapshot = that item selected):**
  0. CONTINUE — `gh3_screens/practice_end_menu__continue.png`
  1. RESTART — `gh3_screens/practice_end_menu__restart.png`
  2. CHANGE SPEED — `gh3_screens/practice_end_menu__change_speed.png`
  3. CHANGE SECTION — `gh3_screens/practice_end_menu__change_section.png`
  4. QUIT — `gh3_screens/practice_end_menu__quit.png`
- **select:** `STRUM_DN×index, GREEN`.
- **back (RED):** TBD (unknown for an end-of-song menu).
- **leads to:**
  - CONTINUE → `song_select` (back to training song selection)
  - RESTART → `loading` → `in_song` (replay the current song)
  - CHANGE SPEED → `speed_select`
  - CHANGE SECTION → `section_select`
  - QUIT → `main_menu` (may route through `quit_confirm` like `pause_menu` —
    unverified)

---

## Screen catalog — 2-player path (MULTIPLAYER → PRO FACE-OFF)

The setup path from `main_menu` → MULTIPLAYER through to 2-player gameplay, walked
2026-08-08. **Marvin drives P1 (left); a human plays P2.** Three of these screens
are act-then-wait (see Conventions) — marvin confirms its side, the human confirms
theirs, then the screen advances.

Snapshots are referenced by raw capture number (`snapshot-NNNNN`) in the
gitignored `tools/marvin-perf/snapshots/`; this path is **not yet imported into the
`gh3_screens/` corpus**, so the observer does not classify these screens yet (see
Open items).

### guitar_select_2p
- **reached from:** `main_menu` → MULTIPLAYER
- **screen:** "Select Guitar" — *"Move the desired guitar to your side of the screen."*
- **paradigm:** per-side — **not a list.** A connected-but-unassigned guitar sits in
  the centre column; each player moves theirs to their own side (orange arrow =
  left/P1, purple = right/P2). A **`READY!` badge** over that side's shield is the
  per-side commit indicator.
- **observed states:** left assigned, right empty, neither ready (*snapshot-73726*)
  → left `READY!` (*74942*) → a second guitar appears centre (*77649*) → it lands on
  the right (*78054*).
- **marvin's action — assert, don't act.** Marvin's guitar is expected to be
  **already preselected on the left** when this screen appears. The controller
  asserts that, then GREENs to confirm its own side. It **never moves a guitar**:
  a wrong move could drag the human's guitar to marvin's side and wedge the screen.
  If the assertion fails, abort and surface it to the operator (Open items).
- **back (RED):** `main_menu`
- **leads to:** `multiplayer_menu` — **`[wait: P2]`**, advances once both sides ready.

### multiplayer_menu
- **reached from:** `guitar_select_2p` (both sides ready)
- **screen:** "multiplayer / CHOOSE MODE"
- **paradigm:** static-list (cursor moves; the selected item turns red)
- **items (top → bottom, index; snapshot = that item selected):**
  0. FACE-OFF — *snapshot-78778*
  1. PRO FACE-OFF — *snapshot-79179*  ← **the mode we always play**
  2. BATTLE — *snapshot-79568*
- **select:** `STRUM_DN×index, GREEN`. All three items are captured, so this is a
  complete fixed-count static list.
- **back (RED):** `guitar_select_2p`
- **leads to:** `character_select_2p` (via PRO FACE-OFF). FACE-OFF and BATTLE are
  documented for recognition but never routed to.

### character_select_2p
- **reached from:** `multiplayer_menu` → PRO FACE-OFF
- **screen:** both players' characters on stage, each with a name banner
  ("AXEL STEEL / PLAYER 1") and a vertical portrait strip on their side
  (*snapshot-80984*).
- **paradigm:** per-side portrait strip.
- **marvin's action:** pass straight through with `GREEN` — this brings up that
  side's `player_ready_2p` panel. Character choice is **out of scope for now**
  (marvin keeps whatever character it has); it may become a real selection later.
- **back (RED):** `multiplayer_menu`
- **leads to:** `player_ready_2p` (per side).
- **note:** this and `player_ready_2p` are **two sub-states of one screen, advanced
  per side** — *snapshot-81552* shows the left half still on the portrait strip
  while the right half already shows P2's ready panel. Don't model them as two
  mutually-exclusive full-screen states.

### player_ready_2p
- **reached from:** `character_select_2p` → GREEN (per side)
- **screen:** a 4-item panel in front of each player's character, **two independent
  cursors** (*snapshot-82769* both on PLAY SHOW).
- **paradigm:** per-side static-list. The selected row gets a light highlight bar.
- **items per panel (top → bottom, index; snapshot = P1's selection):**
  0. PLAY SHOW — *snapshot-82769*  ← **always this**
  1. CHANGE CHARACTER — *snapshot-83501*
  2. CHANGE OUTFIT — *snapshot-83959*
  3. CHANGE GUITAR — *snapshot-84400*
- **commit:** the same `READY!` badge appears over PLAY SHOW once that side confirms
  (*snapshot-85396* — P1 ready, P2 still choosing).
- **marvin's action:** select PLAY SHOW (index 0) on the **left** panel and GREEN.
  Items 1–3 are documented for recognition but out of scope.
- **back (RED):** `character_select_2p`
- **leads to:** `venue_select` — **`[wait: P2]`**, advances once both sides PLAY SHOW.
- **note:** the observer's static-list reader returns a *single* selection index, so
  this screen needs a per-side layout to be readable — only the left panel is
  needed for actuation (Open items).

### venue_select
- **reached from:** `player_ready_2p` (both sides PLAY SHOW)
- **paradigm:** fixed-slot — one gig poster in a fixed centre slot over a constant
  sepia flyer-wall background; the list scrolls under it. Structurally identical to
  `song_select`.
- **observed posters** (*observed, likely a partial list*): Lou's Inferno
  (*snapshot-86147*), Kaiju Megadome (*86866*), Desert Rock Tour (*87297*),
  Shanker's Island (*87679*), Ye Olde Royal Odeon (*88120*), Video Shoot / Studio
  999 (*88461*), Mitch's Moose Lounge (*89308*), Backyard Bash / 22 Arcadia Avenue
  (*89695*).
- **marvin's action:** **confirm through with `GREEN`** on whatever poster is
  selected — the venue does not affect gameplay. So **no venue reader is needed**
  and no venue catalog is modelled (the same treatment `section_select` gets). Venue
  choice may be handed to the human later.
- **back (RED):** `player_ready_2p`
- **leads to:** `song_select`.

### in_song_2p  *(2-player gameplay)*
- **reached from:** `loading`, on the 2-player path.
- **screens:** `gh3_screens/in_song_2p__{0200..0203,web0714}.png`
- Two note highways side by side, one per player, and **two amp scoreboards at the
  top of frame** instead of the single-player bottom-left score block. Marvin plays
  the **left** highway.
- This is the class that tells the CV note-detector which highway geometry to read
  (§4.2). It is **classified by scoreboard-chrome presence**, not a whole-frame
  centroid — see the gameplay journal 2026-07-15, and 2026-07-14 for the per-highway
  sense-line calibration.
- **leads to:** the 2-player results/end screens — **TBD, not yet captured.**

_(more screens added as captured)_

---

## Song catalog (main setlist)

Order as shown on the `song_select` screen; `index` = strum-downs from the top.
Toggle to the bonus setlist with BLUE, back with YELLOW. Original capture numbers
are in [`gh3_screens/README.md`](gh3_screens/README.md).
Bonus setlist: see [below](#song-catalog-bonus-setlist). Titles are operator-transcribed;
exact on-screen punctuation/case to be reconciled if/when M9 text-matches.
Artist is from general knowledge (not read off-screen) — correct any that are
wrong; GH3 may credit covers ("as made famous by …"), this lists the original
artist. Room for more metadata (BPM, length, difficulty) later.

| idx | song | artist | screen |
|----:|------|--------|---------:|
| 0 | Slow Ride | Foghat | `gh3_screens/song_select__00_slow_ride.png` |
| 1 | Talk Dirty to Me | Poison | `gh3_screens/song_select__01_talk_dirty_to_me.png` |
| 2 | Hit Me With Your Best Shot | Pat Benatar | `gh3_screens/song_select__02_hit_me_with_your_best_shot.png` |
| 3 | Story of My Life | Social Distortion | `gh3_screens/song_select__03_story_of_my_life.png` |
| 4 | Rock and Roll All Nite | KISS | `gh3_screens/song_select__04_rock_and_roll_all_nite.png` |
| 5 | Sabotage | Beastie Boys | `gh3_screens/song_select__05_sabotage.png` |
| 6 | Mississippi Queen | Mountain | `gh3_screens/song_select__06_mississippi_queen.png` |
| 7 | School's Out | Alice Cooper | `gh3_screens/song_select__07_school_s_out.png` |
| 8 | Sunshine of Your Love | Cream | `gh3_screens/song_select__08_sunshine_of_your_love.png` |
| 9 | Barracuda | Heart | `gh3_screens/song_select__09_barracuda.png` |
| 10 | Bulls on Parade | Rage Against the Machine | `gh3_screens/song_select__10_bulls_on_parade.png` |
| 11 | Reptilia | The Strokes | `gh3_screens/song_select__11_reptilia.png` |
| 12 | When You Were Young | The Killers | `gh3_screens/song_select__12_when_you_were_young.png` |
| 13 | Miss Murder | AFI | `gh3_screens/song_select__13_miss_murder.png` |
| 14 | The Seeker | The Who | `gh3_screens/song_select__14_the_seeker.png` |
| 15 | Lay Down | Priestess | `gh3_screens/song_select__15_lay_down.png` |
| 16 | Paint It Black | The Rolling Stones | `gh3_screens/song_select__16_paint_it_black.png` |
| 17 | Suck My Kiss | Red Hot Chili Peppers | `gh3_screens/song_select__17_suck_my_kiss.png` |
| 18 | Paranoid | Black Sabbath | `gh3_screens/song_select__18_paranoid.png` |
| 19 | Anarchy in the U.K. | The Sex Pistols | `gh3_screens/song_select__19_anarchy_in_the_u_k.png` |
| 20 | Kool Thing | Sonic Youth | `gh3_screens/song_select__20_kool_thing.png` |
| 21 | My Name Is Jonas | Weezer | `gh3_screens/song_select__21_my_name_is_jonas.png` |
| 22 | Even Flow | Pearl Jam | `gh3_screens/song_select__22_even_flow.png` |
| 23 | Cities On Flame with Rock & Roll | Blue Öyster Cult | `gh3_screens/song_select__23_cities_on_flame_with_rock_roll.png` |
| 24 | Holiday In Cambodia | Dead Kennedys | `gh3_screens/song_select__24_holiday_in_cambodia.png` |
| 25 | Rock You Like A Hurricane | Scorpions | `gh3_screens/song_select__25_rock_you_like_a_hurricane.png` |
| 26 | Same Old Song and Dance | Aerosmith | `gh3_screens/song_select__26_same_old_song_and_dance.png` |
| 27 | La Grange | ZZ Top | `gh3_screens/song_select__27_la_grange.png` |
| 28 | Welcome To The Jungle | Guns N' Roses | `gh3_screens/song_select__28_welcome_to_the_jungle.png` |
| 29 | Helicopter | Bloc Party | `gh3_screens/song_select__29_helicopter.png` |
| 30 | Black Magic Woman | Santana | `gh3_screens/song_select__30_black_magic_woman.png` |
| 31 | Cherub Rock | The Smashing Pumpkins | `gh3_screens/song_select__31_cherub_rock.png` |
| 32 | Black Sunshine | White Zombie | `gh3_screens/song_select__32_black_sunshine.png` |
| 33 | The Metal | Tenacious D | `gh3_screens/song_select__33_the_metal.png` |
| 34 | Pride and Joy | Stevie Ray Vaughan | `gh3_screens/song_select__34_pride_and_joy.png` |
| 35 | Monsters | Matchbook Romance | `gh3_screens/song_select__35_monsters.png` |
| 36 | Before I Forget | Slipknot | `gh3_screens/song_select__36_before_i_forget.png` |
| 37 | Stricken | Disturbed | `gh3_screens/song_select__37_stricken.png` |
| 38 | 3's & 7's | Queens of the Stone Age | `gh3_screens/song_select__38_3_s_7_s.png` |
| 39 | Knights of Cydonia | Muse | `gh3_screens/song_select__39_knights_of_cydonia.png` |
| 40 | Cult Of Personality | Living Colour | `gh3_screens/song_select__40_cult_of_personality.png` |
| 41 | Raining Blood | Slayer | `gh3_screens/song_select__41_raining_blood.png` |
| 42 | Cliffs Of Dover | Eric Johnson | `gh3_screens/song_select__42_cliffs_of_dover.png` |
| 43 | The Number of the Beast | Iron Maiden | `gh3_screens/song_select__43_the_number_of_the_beast.png` |
| 44 | One | Metallica | `gh3_screens/song_select__44_one.png` |

### Song catalog (bonus setlist)

Reached from `song_select` via BLUE (YELLOW returns to main). `index` =
strum-downs from the top. No overlap with the main setlist (verified). Bonus-tier
artists are lower-confidence than the main list — `?` = unverified / unknown.

| idx | song | artist | screen | note |
|----:|------|--------|---------:|------|
| 0 | Avalancha | Héroes del Silencio | `gh3_screens/song_select__bonus_00_avalancha.png` |  |
| 1 | In The Belly Of A Shark | Gallows | `gh3_screens/song_select__bonus_01_in_the_belly_of_a_shark.png` |  |
| 2 | Can't Be Saved | Senses Fail | `gh3_screens/song_select__bonus_02_can_t_be_saved.png` |  |
| 3 | Closer | Lacuna Coil | `gh3_screens/song_select__bonus_03_closer.png` |  |
| 4 | Don't Hold Back | The Sleeping | `gh3_screens/song_select__bonus_04_don_t_hold_back.png` |  |
| 5 | Down 'N Dirty | LA Slum Lords | `gh3_screens/song_select__bonus_05_down_n_dirty.png` |  |
| 6 | F.C.P.R.E.M.I.X. | The Fall of Troy | `gh3_screens/song_select__bonus_06_f_c_p_r_e_m_i_x.png` |  |
| 7 | Generation Rock | Revolverheld | `gh3_screens/song_select__bonus_07_generation_rock.png` |  |
| 8 | Go That Far | Bret Michaels Band | `gh3_screens/song_select__bonus_08_go_that_far.png` |  |
| 9 | Hier Kommt Alex | Die Toten Hosen | `gh3_screens/song_select__bonus_09_hier_kommt_alex.png` |  |
| 10 | I'm In The Band | Hellacopters | `gh3_screens/song_select__bonus_10_i_m_in_the_band.png` |  |
| 11 | Impulse | An Endless Sporadic | `gh3_screens/song_select__bonus_11_impulse.png` |  |
| 12 | In Love | Scouts of St. Sebastian | `gh3_screens/song_select__bonus_12_in_love.png` |  |
| 13 | Mauvais Garçon | Naast | `gh3_screens/song_select__bonus_13_mauvais_gar_on.png` |  |
| 14 | Metal Heavy Lady | Lions | `gh3_screens/song_select__bonus_14_metal_heavy_lady.png` |  |
| 15 | Minus Celsius | Backyard Babies | `gh3_screens/song_select__bonus_15_minus_celsius.png` |  |
| 16 | My Curse | Killswitch Engage | `gh3_screens/song_select__bonus_16_my_curse.png` |  |
| 17 | Nothing For Me Here | Dope | `gh3_screens/song_select__bonus_17_nothing_for_me_here.png` |  |
| 18 | Prayer Of The Refugee | Rise Against | `gh3_screens/song_select__bonus_18_prayer_of_the_refugee.png` |  |
| 19 | Radio Song | Superbus | `gh3_screens/song_select__bonus_19_radio_song.png` |  |
| 20 | Ruby | The Kaiser Chiefs | `gh3_screens/song_select__bonus_20_ruby.png` |  |
| 21 | She Bangs The Drums | The Stone Roses | `gh3_screens/song_select__bonus_21_she_bangs_the_drums.png` |  |
| 22 | Take This Life | In Flames | `gh3_screens/song_select__bonus_22_take_this_life.png` |  |
| 23 | The Way It Ends | Prototype | `gh3_screens/song_select__bonus_23_the_way_it_ends.png` |  |
| 24 | Through The Fire And Flames | DragonForce | `gh3_screens/song_select__bonus_24_through_the_fire_and_flames.png` |  |

---

## Menu graph (edges)

| from | to | inputs | notes |
|------|----|--------|-------|
| main_menu | _(per item)_ | `STRUM_DN×index, GREEN` | static list, index per catalog above |
| main_menu | training_menu | `STRUM_DN×4, GREEN` | TRAINING (index 4) |
| main_menu | guitar_select_2p | `STRUM_DN×3, GREEN` | MULTIPLAYER (index 3) |
| guitar_select_2p | multiplayer_menu | `GREEN` **`[wait: P2]`** | assert marvin's guitar preselected left, confirm own side; never moves a guitar |
| multiplayer_menu | character_select_2p | `STRUM_DN×1, GREEN` | PRO FACE-OFF (index 1) — always |
| character_select_2p | player_ready_2p | `GREEN` | per side; pass through (no character change) |
| player_ready_2p | venue_select | `GREEN` **`[wait: P2]`** | PLAY SHOW (index 0) on the left panel |
| venue_select | song_select | `GREEN` | confirm through; venue is irrelevant to gameplay |
| song_select | difficulty_select | `STRUM_DN×i, GREEN` | 2-player path — song set unconfirmed; `part_select` presence unconfirmed |
| difficulty_select | loading | `STRUM_DN×index, GREEN` | 2-player path — shared screen, both players same difficulty |
| loading | in_song_2p | _(none — wait)_ | transient; auto-advances into 2-player gameplay |
| training_menu | song_select | `STRUM_DN×1, GREEN` | PRACTICE (index 1) |
| song_select | part_select | `STRUM_DN×i, GREEN` | song at list index i; shared node. May skip part_select for some songs |
| song_select | bonus list | `BLUE` (back: `YELLOW`) | toggles the song list, not a screen change |
| part_select | difficulty_select | LEAD `GREEN` / 2nd `STRUM_DN×1, GREEN` | items song-dependent |
| difficulty_select | section_select | `STRUM_DN×index, GREEN` | EASY 0 / MEDIUM 1 / HARD 2 / EXPERT 3 |
| section_select | speed_select | `STRUM_UP×N, GREEN` | FULL SONG = top (idx 0); strum up to saturate |
| speed_select | loading | `STRUM_UP×N, GREEN` | FULL SPEED = top (idx 0); strum up to saturate |
| loading | in_song | _(none — wait)_ | transient; auto-advances |
| in_song | pause_menu | `+` (guitar plus) | not a fret/strum bit — see caveat |
| in_song | practice_end_menu | _(none — song ends)_ | gameplay completes |
| pause_menu | in_song | `GREEN` | RESUME (idx 0) |
| pause_menu | loading | `STRUM_DN×1, GREEN` | RESTART (idx 1) → replay |
| pause_menu | speed_select | `STRUM_DN×3, GREEN` | CHANGE SPEED (idx 3) |
| pause_menu | section_select | `STRUM_DN×4, GREEN` | CHANGE SECTION (idx 4) |
| pause_menu | song_select | `STRUM_DN×5, GREEN` | NEW SONG (idx 5) |
| pause_menu | quit_confirm | `STRUM_DN×6, GREEN` | QUIT (idx 6) → confirm dialog |
| quit_confirm | pause_menu | `GREEN` | CANCEL (idx 0) |
| quit_confirm | main_menu | `STRUM_DN×1, GREEN` | QUIT (idx 1) — confirm |
| practice_end_menu | song_select | `GREEN` | CONTINUE (idx 0) |
| practice_end_menu | loading | `STRUM_DN×1, GREEN` | RESTART (idx 1) → replay |
| practice_end_menu | speed_select | `STRUM_DN×2, GREEN` | CHANGE SPEED (idx 2) |
| practice_end_menu | section_select | `STRUM_DN×3, GREEN` | CHANGE SECTION (idx 3) |
| practice_end_menu | main_menu | `STRUM_DN×4, GREEN` | QUIT (idx 4) |
| _any_ | _(parent)_ | `RED` | back up one level |

_(filled in as destinations are confirmed)_

---

## Navigator notes (implementation)

> **Implemented (2026-07-02):** the on-device controller is
> `firmware/marvin/default/src/game/game_controller.c` — a port of the offline
> `tools/gameplay` `NavController`. Triggered by the dashboard START button or the
> console `play` command; reads the committed `Selection`; drives the worked path
> below closed-loop and hands off to the CV detector at `in_song`. v1 covers the
> practice path from a menu anchor (RED→`main_menu`); mid-song `+`/pause recovery is
> not wired (the T1S mask path carries no `+`). See the marvin journal 2026-07-02.

How the controller (M10) uses this map. Unmapped paths don't need modeling —
they're handled by recovery, not enumeration.

- **Closed-loop — verify every screen (not a blind macro).** Before issuing a
  step's inputs, confirm the observer reports the expected screen; after issuing
  them, confirm the expected destination before proceeding. On mismatch, recover
  (see below) and re-plan. Never fire a button sequence open-loop and *assume* we
  landed where we hoped — drift, sticky defaults, variable/absent screens, and
  load times all break that. This is why the observer (M9) gates the controller
  (M10). (spec §4.8.1.)
- **Stubs / unknown destinations.** Edges marked `TBD` simply have no plan; the
  navigator never routes through them. Out-of-scope nodes (`tutorials_menu`) and
  any screen the observer doesn't recognize are treated as "unknown state."
- **Recovery from unknown state.** RED backs up one level from any menu, so the
  generic recovery is *"press RED until a recognized screen appears, then
  re-plan."* This gets us un-stuck from a wrong branch without mapping it.
- **Top-item selection (`STRUM_UP×N`).** For steps where we always want the top
  item (FULL SONG, FULL SPEED), strum up to saturate at the top, then GREEN,
  rather than trusting the entry highlight. (Depends on no wrap-around — open.)

### Worked path — training run (main-setlist song `i`, difficulty `d`, LEAD)
The nominal input plan. Each step runs **gated on a screen check** (verify the
expected screen before, confirm arrival after) per the closed-loop rule above —
it is not executed as one blind burst. From `main_menu` (if not there, recover
with RED first):

1. `STRUM_DN×4, GREEN` → TRAINING → `training_menu`
2. `STRUM_DN×1, GREEN` → PRACTICE → `song_select`
3. `STRUM_DN×i, GREEN` → song at catalog index `i` (Song catalog) → `part_select`
4. select LEAD, then GREEN → `difficulty_select`  *(see caveat)*
5. `STRUM_DN×d, GREEN` → difficulty `d` (EASY 0 … EXPERT 3) → `section_select`
6. `STRUM_UP×N, GREEN` → FULL SONG → `speed_select`
7. `STRUM_UP×N, GREEN` → FULL SPEED → `loading` (wait) → `in_song` (gameplay)

**Caveat (step 4):** `part_select` items vary per song and the screen may be
absent — exactly the case the closed-loop rule handles: after step 3's GREEN,
check which screen actually appeared. If it's `part_select`, pick LEAD (read the
offered items) and GREEN; if it's already `difficulty_select`, skip step 4. A
purely open-loop macro can't do this safely (extra inputs would mis-fire), so
until the observer exists, restrict to songs known to show LEAD-at-top. Per-song
`parts` metadata lets the planner pre-know the layout either way.

### Worked path — 2-player pro face-off run (main-setlist song `i`, difficulty `d`)
Marvin drives **P1 (left)**; a human plays P2. Same closed-loop gating as above.
Steps marked **`[wait: P2]`** end on the *human* acting, so the wait there is
unbounded — observe until the next screen appears; do **not** time out into RED
recovery (Conventions). From `main_menu`:

1. `STRUM_DN×3, GREEN` → MULTIPLAYER → `guitar_select_2p`
2. **assert** marvin's guitar is preselected on the left; `GREEN` to confirm its
   own side. If the assertion fails, **abort to the operator** — do not try to move
   a guitar. Then **`[wait: P2]`** → `multiplayer_menu`
3. `STRUM_DN×1, GREEN` → PRO FACE-OFF → `character_select_2p`
4. `GREEN` → pass through (keep the current character) → `player_ready_2p`
5. PLAY SHOW (index 0) on the **left** panel, `GREEN`, then **`[wait: P2]`** →
   `venue_select`
6. `GREEN` → confirm whatever venue is selected → `song_select`
7. `STRUM_DN×i, GREEN` → song `i` → `difficulty_select` *(a 2-player `part_select`
   may or may not appear — same closed-loop skip as the training path's step 4)*
8. `STRUM_DN×d, GREEN` → difficulty `d`, shared by both players → `loading` (wait)
   → `in_song_2p` (gameplay, marvin reads the **left** highway)

**Unverified in this plan:** the tail from step 6 on is operator-reported, not
captured — see Open items before relying on steps 6–8.

---

## Open items
- ✅ Practice path mapped end-to-end (`main_menu → … → in_song`), incl. the
  `practice_end_menu` loop-back (continue / restart / change speed|section / quit).
- ✅ Mid-song `pause_menu` mapped (resume / restart / options / change speed|
  section / new song / quit).
- Extend the actuator command path to carry non-fret/strum buttons (`+`/`−`,
  etc.) for menu use — fauxmote already emulates them (`firmware/fauxmote`, §3);
  the marvin→actuator protocol (§4.3) is fret/strum today.
- `pause_menu` OPTIONS submenu — out of scope; map only if needed.
- Per-song `part_select`: which parts each song offers (lead/rhythm vs lead/bass),
  and which songs skip the screen entirely. Likely a `parts` column on the song
  catalog, or read live (vision). Need snapshots of the lead/bass variant + a
  skip case.
- Other modes: walk QUICKPLAY and CAREER; confirm they reuse `song_select` /
  `difficulty_select` (shared-node assumption).
- ✅ 2-player setup path mapped (`main_menu → MULTIPLAYER → … → in_song_2p`), 2026-08-08.
  Its remaining gaps:
  - **Not yet recognized.** The 5 new screens are documented but **not imported into
    the `gh3_screens/` corpus**, so the observer classifies none of them (they'll read
    UNKNOWN). Deferred phase: import the 22 captures, add the ids to `screens.py`
    (`GP_N_SCREENS` 14 → 19), re-export `gameplay_metadata.h`, bump the dimension
    assert in `tools/gameplay/tests/test_export_c.py`.
  - **Per-side readers.** `player_ready_2p` has two independent cursors, but the
    static-list reader returns a single index and menu layouts are keyed by screen id
    — a per-side layout needs keying by a *layout* id instead. Only the left panel is
    needed (marvin drives P1). For the per-side `READY!` commit state on
    `guitar_select_2p` / `player_ready_2p`, reuse the masked-SAD probe pattern from the
    scoreboard-presence work (gameplay journal 2026-07-15) rather than inventing a new
    mechanism.
  - **Confirm the tail** (steps 6–8 of the worked path): the exact
    `venue_select → song_select → difficulty_select → loading` ordering is
    operator-reported from memory, not captured. Also unconfirmed: whether a
    `part_select` appears on this path, and the available *song set*. One capture pass
    settles all three. (Difficulty is **not** per-player — resolved.)
  - **Guitar-preselect assertion.** Documented default if it fails is abort-to-operator.
    If the left side turns out not to be reliably preselected in practice, this needs a
    real plan plus captures of the unassigned state.
  - **Unbounded act-then-wait.** The controller needs a wait state distinct from its
    timeout→RED-recover path, plus operator-visible "waiting for player 2" status
    (`game_controller.c`).
  - **2-player end-of-song screens** are unmapped — `in_song_2p` leads to TBD.
- **Wrap-around behavior** (now load-bearing): does the cursor wrap past the
  ends? The FULL SONG / FULL SPEED selection relies on strum-up **saturating** at
  the top item. If lists wrap, that breaks and the navigator must read the
  highlight instead. Confirm for static lists.
  (Affects shortest-path planning in the controller.)
- ✅ per-screen vision classifier + highlight readout — built in
  `game/gameplay_classify.c` (`gp_classify()`); the controller reads the classified
  `{screen, selection}` via the gameplay engine (`game_controller.c` `observe()`).
