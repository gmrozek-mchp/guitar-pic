# gameplay — GH3 game-state engine (offline prototype)

Host-side prototype for marvin's game-state observer/controller (spec §4.8, milestone
M9/M10). Algorithms are proven here against the real-screen corpus, then the proven,
simple logic ports to a firmware `gameplay_engine` module. See
[`docs/journal.md`](docs/journal.md) and `firmware/marvin/docs/gh3_navigation.md`.

So far:
- **Screen classifier** — which GH3 screen is this (`main_menu`, `song_select`, `in_song`,
  …) or `UNKNOWN`.
- **Static-list selection reader** — within a recognized static-list screen, which menu
  item is highlighted.
- **song_select reader** — which song is in the highlight slot (and which setlist).
- **Navigator (M10)** — plan a high-level verb (practice-run a song on a difficulty) and
  execute it closed-loop along the menu graph, verified against a simulated menu and the real
  observer.

## Screen classifier

Coarse **fixed-region colour fingerprint** + **nearest-centroid** classifier with an
**UNKNOWN reject** rule (Q10: fixed-region matching, not general CV). A frame is split
into a `cols×rows` grid; each region collapses to its mean BGR; the concatenated vector
is compared by integer L1 distance to one centroid per screen class. The design is shaped
to port to cheap integer C and is **memory-bound, not compute-bound** on the SAM9X75.

Two knobs (chosen empirically by `gameplay eval`, defaults baked into `FingerprintConfig`):

- **sample density** — dense (every pixel; ~7–20 ms on-device) vs a sparse N×N lattice per
  region (sub-millisecond). Default: 5×5 lattice.
- **normalization** — per-frame value standardization, which cancels the **gain/offset
  value-slop** of the Wii's analog-component → HDMI path. Default: on (it is what lifts
  slop-robustness to ~99.8%).

Default config (12×8, 5×5 samples, normalized): 96% leave-one-out accuracy (= 100% of all
multi-sample classes; the 4 single-sample classes can't be LOO-matched), ~99.8% robustness
across the synthetic analog-slop envelope, ~0.1–0.4 ms estimated per classification.

## Static-list selection reader

For the 8 fixed-count static-list screens, per-screen menu metadata (ordered items +
menu-band geometry, in `metadata.py`) divides the band into one cell per item. The selected
cell is the one that deviates most from its **unselected baseline** (learned per screen from
the corpus) — GH3 marks selection by *changing* a row (colour/bar), not by making it
brightest, so "what changed" is the robust signal. Cell colours are normalized per frame to
cancel gain/offset slop. Result: 100% clean / ~99.4% slop on the 33 labelled frames.

## song_select reader

The selected song sits in a fixed highlight slot (except each setlist's first song, which
sits one row lower). We identify it by matching the slot's low-res grayscale bitmap against
the 64 per-song templates — across **both** setlists, so main/bonus falls out of the match.
A small read-time offset search re-aligns the slot, which makes the fine grid tolerant of
positional slop. Result: 64/64 clean, ~99.1% under slop. The active setlist is also read
independently from the page background colour (main = yellow, bonus = white; warmth R−B):
100% clean and under slop.

Char-level OCR is deliberately out of scope: menu items and song titles are a closed set we
already have reference bitmaps for (match, don't decode); digit OCR comes only with score
reading. `section_select` (variable list) and reading the scrolling neighbour list are deferred.

## Navigator (M10)

`navgraph.py` encodes the menu graph (`firmware/marvin/docs/gh3_navigation.md`) as data;
`navigator.py` plans a verb (`plan_practice_run`, `plan_goto`) and runs it closed-loop through
an `Observer`/`Actuator` seam. The controller is observation-driven — each step runs against
the screen actually observed — which yields normal progress, the `part_select` skip, and RED
recovery from one rule. Offline it's driven by `simgame.py` (the test oracle); on the device
the same seam takes the CV observer + fretboard link. Selections strum the signed delta from
the observed cursor (sticky-safe); FULL SONG/FULL SPEED strum up until the selection stops
moving (no blind count).

## Usage

```sh
uv sync --group dev

uv run gameplay eval                 # classifier + selection reader: accuracy, robustness, thresholds, HW time
uv run gameplay eval --no-sweep      # skip the parameter sweep
uv run gameplay classify path/to/frame.png   # screen id (+ selection / song)
uv run gameplay rows path/to/frame.png       # debug per-cell selection scores
uv run gameplay navigate --song 19 --difficulty hard   # plan + run a practice run vs the sim
uv run gameplay export-c --out gameplay_metadata.h     # freeze recognizer metadata to a C header

uv run pytest
```

## Firmware port (marvin `gameplay_engine`, spec §4.8)

The proven algorithms are being ported to marvin firmware in phases (see `docs/journal.md`).
Phase 0 (done) is `gameplay export-c`: it emits all recognizer data — classifier
centroids/thresholds, static-list menu geometry + baselines, song templates/ROIs — as a single
generated C header (`gameplay_metadata.h`, plain PODs + flat arrays) that the firmware compiles
in. Floats are only the per-frame-normalized baselines/song vectors (soft-float on the ARM926).
The generated header is verified to compile under `cc -std=c11 -Wall -Wextra`.

The corpus is read from `firmware/marvin/docs/gh3_screens/` (override with
`$GAMEPLAY_CORPUS_DIR`). Labels come from each filename's prefix.
