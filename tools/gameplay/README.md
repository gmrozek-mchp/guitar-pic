# gameplay — GH3 game-state engine (offline prototype)

Host-side prototype for marvin's game-state observer/controller (spec §4.8, milestone
M9/M10). Algorithms are proven here against the real-screen corpus, then the proven,
simple logic ports to a firmware `gameplay_engine` module. See
[`docs/journal.md`](docs/journal.md) and `firmware/marvin/docs/gh3_navigation.md`.

Two capabilities so far:
- **Screen classifier** — which GH3 screen is this (`main_menu`, `song_select`, `in_song`,
  …) or `UNKNOWN`.
- **Static-list selection reader** — within a recognized static-list screen, which menu
  item is highlighted.

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

Char-level OCR is deliberately out of scope: menu items and song titles are a closed set we
already have reference bitmaps for (match, don't decode); digit OCR comes only with score
reading. `song_select` (fixed-slot) and `section_select` (variable list) are deferred.

## Usage

```sh
uv sync --group dev

uv run gameplay eval                 # classifier + selection reader: accuracy, robustness, thresholds, HW time
uv run gameplay eval --no-sweep      # skip the parameter sweep
uv run gameplay classify path/to/frame.png   # screen id (+ selection, if a static list)
uv run gameplay rows path/to/frame.png       # debug per-cell selection scores

uv run pytest
```

The corpus is read from `firmware/marvin/docs/gh3_screens/` (override with
`$GAMEPLAY_CORPUS_DIR`). Labels come from each filename's prefix.
