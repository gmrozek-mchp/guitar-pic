# edge-ai

Distill marvin's emitted gameplay commands into a small causal model over the
fretboard's 5-channel ADC stream. **Design + plan live in [`docs/`](docs/)** —
start with [`docs/SPEC.md`](docs/SPEC.md) and [`docs/journal.md`](docs/journal.md).

This package is the Phase-2 host-side training pipeline (PyTorch). Input is the
actuator-labelled CSV produced by `marvin-perf export-ml --labels=actuator`.

## Layout

- `edge_ai/data.py` — CSV loader + causal row-index windowing (stdlib).
- `edge_ai/lag.py` — photo-dip → strum lag measurement (stdlib).
- `edge_ai/metrics.py` — per-bit accuracy/F1 + strum-event timing (stdlib).
- `edge_ai/model.py` — baseline causal 1D-CNN (`StrumNet`, torch).
- `edge_ai/train.py` — training loop + holdout evaluation (torch).
- `edge_ai/cli.py` — `edge-ai {lag,train,eval}`.

The stdlib pieces (`data`, `lag`, `metrics`) run without numpy/torch; only
`model`/`train`/`eval` need the `train` dependency group.

## Use

```sh
export UV_CACHE_DIR="$TMPDIR/uv-cache"   # only needed inside a restricted sandbox

# size the causal window from real data (no torch needed)
uv run edge-ai lag path/to/song-expert.csv --curve

# install the heavy deps and train
uv sync --group train --group dev
uv run edge-ai train --holdout heldout.csv song1.csv song2.csv song3.csv \
    --window 60 --out model.pt
uv run edge-ai eval --model model.pt heldout.csv

# tests (stdlib-only; needs the dev group for pytest)
uv run --group dev pytest
```

Corpus is Expert-only, one song held out — see [`docs/training.md`](docs/training.md) §4.
