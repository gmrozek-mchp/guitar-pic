# gameplay — GH3 game-state engine (offline prototype)

Host-side prototype for marvin's game-state observer/controller (spec §4.8, milestones
M9/M10). Algorithms are proven here against the real-screen corpus, then ported to marvin's
firmware `game/` module (`firmware/marvin/default/src/game/`). The journal
([`docs/journal.md`](docs/journal.md)) holds the current state and every measurement; the menu
graph and screen names are in `firmware/marvin/docs/gh3_navigation.md`.

## What's here

| Module | Reads | Notes |
|---|---|---|
| `fingerprint.py`, `classifier.py` | which GH3 screen (`main_menu`, `song_select`, …) or `UNKNOWN` | fixed-region colour fingerprint + nearest-centroid + reject |
| `highlight.py`, `metadata.py` | highlighted item on the static-list screens | deviation from a learned unselected baseline |
| `songselect.py` | selected song + setlist on `song_select` | closed-set match against 64 per-song templates; setlist also from background colour |
| `present.py` | is this a 1p / 2p gameplay screen | masked SAD on the static scoreboard chrome |
| `score.py` | 1p score, streak, multiplier | per-digit glyph match (proportional training font) |
| `amp2p.py` | 2p amp scoreboard score (both sides, up to 6 digits) | fixed LED grid, per-cell ink threshold, template bank keyed by (digit, font) |
| `ready.py` | P1 READY! badge on `guitar_select_2p` | did marvin's own confirm land |
| `endprobe.py`, `endlayout.py` | results screen up / practice vs face-off end layout | the frame-rate probe is disarmed on device (`GC_END_PROBE_ARMED 0`, journal 2026-08-11) |
| `navgraph.py`, `navigator.py`, `simgame.py` | plan + run a menu verb closed-loop | `simgame` is the offline oracle for the `Observer`/`Actuator` seam |
| `export_c.py` | — | freezes recognizer data into the generated `gameplay_metadata.h` |

Everything is integer / cheap-to-port by design; char-level OCR of menus and titles is out
of scope (closed sets are matched, not decoded).

Current `gameplay eval --no-sweep` on the 137-frame, 20-class corpus: classifier 130/137
leave-one-out (misses are single-sample classes and the 2p setup screens), 99.7% stable under
the synthetic analog-slop envelope, ~0.1–0.4 ms estimated per classification on the SAM9X75;
static-list selection 43/43; song_select 70/70; 1p score reader 54/54.

## Usage

```sh
uv sync

uv run gameplay eval                 # all readers: accuracy, robustness, thresholds, HW time
uv run gameplay eval --no-sweep      # skip the parameter sweep
uv run gameplay classify path/to/frame.png   # screen id (+ selection / song)
uv run gameplay rows path/to/frame.png       # debug per-cell selection scores
uv run gameplay read-score path/to/frame.png              # 1p score reader on one frame
uv run gameplay read-amp2p path/to/frame.png --side left  # 2p amp reader on one frame

# capture sweeps (frames come from marvin-perf):
#   cd ../marvin-perf && uv run marvin-perf export-region <cap> --kind score-2p-left --out /tmp/L
uv run gameplay score-monotonic /tmp/S                  # label-free 1p sweep (monotonic violations)
uv run gameplay amp2p-monotonic /tmp/L --side left      # label-free 2p sweep
uv run gameplay amp2p-grow /tmp/L --side left           # propose new corpus frames (add --commit)

uv run gameplay navigate --song 19 --difficulty hard    # plan + run a practice run vs the sim

# regenerate the firmware header after any recognizer/corpus change
uv run gameplay export-c --out ../../firmware/marvin/default/src/game/gameplay_metadata.h

uv run pytest
```

One-off scripts: `render_cv_points.py` (draws marvin's `cv_marvin_v1.c` sample points over a
frame) and `scan_scoreboard_presence.py` (offline proof for `present.py`).

The screen corpus is read from `firmware/marvin/docs/gh3_screens/` (override with
`$GAMEPLAY_CORPUS_DIR`); labels come from each filename's prefix. Score/amp corpora and
held-out regression frames live under `data/`.
