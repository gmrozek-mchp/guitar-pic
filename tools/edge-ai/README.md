# edge-ai

Distill marvin's emitted gameplay commands into a small causal model over the
fretboard's 5-channel ADC stream. **Design + plan live in [`docs/`](docs/)** —
start with [`docs/SPEC.md`](docs/SPEC.md) and [`docs/journal.md`](docs/journal.md).

This package is the host-side pipeline (PyTorch): train a causal CNN, then int8
post-training quantise it into the `model_weights.h` the fretboard firmware runs.
Input is the actuator-labelled CSV from `marvin-perf export-ml --labels=actuator-fb`.

## Layout

- `edge_ai/data.py` — CSV loader + causal row-index windowing (stdlib).
- `edge_ai/lag.py` — photo-dip → strum lag measurement (stdlib).
- `edge_ai/metrics.py` — per-bit accuracy/F1 + strum-event timing (stdlib).
- `edge_ai/model.py` — causal 1D-CNN (`StrumNet`, torch).
- `edge_ai/train.py` — training loop + holdout evaluation (torch).
- `edge_ai/quantize.py` — int8 post-training quantiser + `model_weights.h` emitter (numpy/torch).
- `edge_ai/viz.py` — torchinfo summary, torchview graph, receptive-field cone plot (`viz` group).
- `edge_ai/cli.py` — `edge-ai {lag,train,eval,quantize,viz}`.

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

# int8 quantise -> firmware header (deploy model: --no-holdout --channels 16 --window 85)
uv run edge-ai quantize --model model.pt --name hard \
    --strum-thresh 0.80 --out ../../firmware/fretboard/model_weights.h song1.csv song2.csv

# visualise the model (summary + RF cone always; graph needs the graphviz binary)
uv sync --group train --group viz   # adds torchinfo, torchview, matplotlib
uv run edge-ai viz --model model.pt --rf-cone rf_cone.png --graph graph.svg

# tests (stdlib-only; needs the dev group for pytest)
uv run --group dev pytest
```

For block diagrams beyond these, export the float model to ONNX and open it in
[Netron](https://netron.app), or redraw in PlotNeuralNet / NN-SVG.

Corpus is Expert-only, one song held out — see [`docs/training.md`](docs/training.md) §4.
