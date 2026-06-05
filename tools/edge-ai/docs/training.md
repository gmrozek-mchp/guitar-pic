# training

The offline pipeline: how marvin self-play recordings become training data, and the model that consumes them.

## 1. Data flow

Recap (full diagram in [architecture.md](architecture.md)):

```
perf-log capture (.bin, schema v4: fretboard frame carries sample_seq + applied_mask)
    │
    ▼
marvin-perf  export-ml --labels=actuator-fb capture.bin out.csv
    │   (labels from the in-frame applied_mask — paired with the ADC scan
    │    atomically on-device, no cross-stream join)
    ▼
CSV (per-row: timestamp, fb_seq, 5 ADC, 6 labels)
    │
    ▼
host training (PyTorch)
```

## 2. New label source in the SensiML exporter

[tools/marvin-perf/marvin_perf/exporters/sensiml_csv.py](../../marvin-perf/marvin_perf/exporters/sensiml_csv.py) has a `--labels=` switch (the original `pressed_mask` path stays as `--labels=detector` for back-compat). Two modes matter:

- **`actuator` (original, cross-stream).** Forward-fills `Actuator.intended_mask` onto each `FretboardRaw` row, emitting frets at bits 0..4 verbatim and a **collapsed strum bit** = `((intended_mask >> 5) | (intended_mask >> 6)) & 1` (logical OR of strum-down and strum-up). The label (send path) and feature (receive path) cross two USB-CDC directions, so the join has an unknown skew (see [review.md](review.md) / journal).
- **`actuator-fb` (deployed, atomic — schema v4).** The fretboard frame itself carries `sample_seq` + the `applied_mask` it was driving during the scan, paired with the ADC sample on-device in one 240 Hz tick. The exporter sources labels from `applied_mask` — **no cross-stream join, zero skew** — and emits an `fb_seq` column. This is the corpus the shipped model trains on; it is also the most faithful supervision (exactly the function the deployed model replaces). `detector-fb` is the analogous atomic detector probe.

The strum collapse is a property of *these* exporter modes, not the raw capture. `PERF_REC_ACTUATOR` records both strum bits as marvin emitted them; a future human-trainer corpus (where alternating up/down is meaningful) keeps both bits intact on disk for a future direction-aware exporter.

## 3. CSV schema (`actuator-fb`)

```
timestamp, fb_seq,
ph_green, ph_red, ph_yellow, ph_blue, ph_orange,
fret_green, fret_red, fret_yellow, fret_blue, fret_orange,
strum
```

One row per `FretboardRaw` ≈ 240 Hz. `fb_seq` is the fretboard's monotonic sample counter; the loader windows **within contiguous `fb_seq` runs** so a dropped frame can't stitch two non-adjacent samples ([`data.contiguous_runs`](../edge_ai/data.py)). (The original `actuator` mode omits `fb_seq`.)

## 4. Capture protocol

A training corpus is a set of marvin self-play perf-log captures **at a single difficulty** across song variety. **v1 targets `hard`** (the shipped corpus is 5 hard songs — hitme/rockroll/slowride/story/talkdirty); Expert is the eventual follow-on (see the journal — hard was picked first for slower scroll + all 5 frets). The corpus is also pinned to a single **timing regime** — `TP_STRUM_DELAY_MS=300 / TP_STRUM_PULSE_MS=40` for the current corpus — because the model *is* the photo→command timing function; mixing timing regimes is the same ill-posed averaging as mixing difficulties (see journal decision log). Each capture is exported `--labels=actuator-fb` (in-frame `applied_mask`, no cross-stream join). For deploy, all songs train with `--no-holdout`; for generalisation checks one song is held out and reused across iterations so metrics are comparable.

> **Why single-difficulty.** If the game highway's scroll speed varies with difficulty, the photo-dip→strum delay is `(pixel gap photo-row→CV-row)/scroll_speed + delay` — inversely dependent on scroll speed. With 5 sensors at one screen row and no difficulty input, mixing difficulties makes identical inputs map to different correct delays: an *ill-posed* regression, not merely an under-sampled one. So v1 fixes a single difficulty. If scroll speed turns out constant across difficulties, difficulties can be merged later at no cost. Eventual multi-difficulty support is one-model-per-difficulty (difficulty selected at runtime from marvin's game-state controller), or a dedicated fret-line scroll-speed sensor — see journal open questions.

## 5. Model

For the network itself — layer-by-layer shape, receptive field, and the design rationale — see [model.md](model.md). This section records the training-pipeline choices.

### 5.1 The model (as built)

- **Input:** 5 ADC channels × `window` causal samples @ 240 Hz, windowed **by row index** (each CSV row is one true 240 Hz sample; the `timestamp` column is bursty — see journal), per-channel standardised then int8 at deploy. The deploy model uses `window=85` (= the receptive field, required by the streaming inference path — see [runtime.md](runtime.md)).
- **Output:** 6 sigmoids during training (independent BCE per bit) — 5 frets + 1 collapsed strum; thresholded to binary at deploy. The runtime stage maps the strum bit into the wire byte's bit 5 (strum-down); bit 6 stays 0.
- **Architecture (primary):** tiny causal 1D-CNN — **three** dilated causal conv layers, kernel 5, dilations **`(1, 4, 16)`** (receptive field 85, to cover the photo→strum lag — see [model.md §4](model.md)), → linear head on the last timestep → 6 sigmoids. Training default 8 channels; deploy uses **16** (capacity for the sparse strum class — [model.md §8](model.md)). Quantise to int8. This is the primary path (see journal decision log): the strum-timing problem is a sequence/event problem the auto-pipeline's windowed-feature + tree/ensemble shape handles poorly.
- **MPLAB ML / SensiML (later demonstration track):** windowed feature extractor (MAV, std, slope, peak, simple FIR taps) feeding a gradient-boosted ensemble or tiny dense head. SensiML targets exactly this MCU class, so once a known-good model shape exists it's a strong "here's the Microchip-tooling path" comparison — but it is **not** the v1 critical path.

### 5.2 Sizing the window: receptive field, then the lag

The window must be **≥ the receptive field** (85 samples for `(1,4,16)`), and the receptive field must **cover the photo→strum lag** — that's the binding constraint, established by the overfit A/B in [model.md §4](model.md), not by a blind window sweep. The lag itself is a constant at fixed difficulty + timing, measurable with the `edge-ai lag` utility (cross-correlating photo dips against strum events); `TP_STRUM_DELAY_MS` is only the *fixed* part (the rest is scroll-time from the photo row to marvin's CV row). The deploy window is pinned at 85 (= RF) so the streaming deploy path is bit-exact — see [review.md Q3](review.md#q3-look-ahead-vs-now-cast).

### 5.3 Quantisation (built — int8, post-training)

Per-tensor symmetric **int8 weights, int32 accumulators, int8 activations** (int64 fixed-point requant between layers); int8 output via an integer threshold compare on the head accumulator (no on-device sigmoid). The target M0+ has no FPU, so float inference is a non-starter. PTQ is essentially lossless (frets ±0.001 vs float, strum recall unchanged), so QAT is not needed. Standardisation folds into an integer input affine. Implemented in [`quantize.py`](../edge_ai/quantize.py); host↔device bit-exactness is gated by tests. Full scheme in [model.md §7](model.md) / [runtime.md](runtime.md).

## 6. Loss

Independent BCE per output bit, weighted to compensate for class imbalance:

- Fret bits are dense (often 1 for many ticks per chord); near-1:1 weighting works.
- The strum bit is sparse — **~7–13 %** of samples across the hard corpus; weight positively (`--strum-weight`, auto = inverse class frequency ≈ 10–17×) to avoid the trivial "always 0" minimum. The deploy model uses `--strum-weight 5` (below auto) to trim the recall bias at the source, then sets the precision/recall operating point with the strum threshold + monostable at quantise time (header-only, no retrain). Focal loss is a sane alternative if simple weighting plateaus.

If per-bit independence produces poor strum timing, switch to a multi-class head over common chord shapes — see [review.md §alternatives](review.md#12-alternatives-considered).
