# gameplay — GH3 game-state engine (offline prototype)

Host-side prototype for marvin's game-state observer/controller (spec §4.8, milestone
M9/M10). Algorithms are proven here against the real-screen corpus, then the proven,
simple logic ports to a firmware `gameplay_engine` module. See
[`docs/journal.md`](docs/journal.md) and `firmware/marvin/docs/gh3_navigation.md`.

**Current slice: the screen classifier.** Given a captured frame, recognize which GH3
screen it is (`main_menu`, `song_select`, `in_song`, …) or report `UNKNOWN`.

## Approach

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

## Usage

```sh
uv sync --group dev

uv run gameplay eval                 # LOO CV, confusion matrix, robustness, thresholds, HW time
uv run gameplay eval --no-sweep      # skip the parameter sweep
uv run gameplay classify path/to/frame.png

uv run pytest
```

The corpus is read from `firmware/marvin/docs/gh3_screens/` (override with
`$GAMEPLAY_CORPUS_DIR`). Labels come from each filename's prefix.
