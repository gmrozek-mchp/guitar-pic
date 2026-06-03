# training

The offline pipeline: how marvin self-play recordings become training data, and the model that consumes them.

## 1. Data flow

Recap (full diagram in [architecture.md](architecture.md)):

```
perf-log capture (.bin)
    │  contains: SESSION + DETECTOR + ACTUATOR + FRETBOARD_RAW
    │
    ▼
marvin-perf  export-ml --labels=actuator capture.bin out.csv
    │
    ▼
SensiML CSV (per-row: 5 ADC + 6 labels)
    │
    ▼
host training (PyTorch / MPLAB ML)
```

## 2. New label source in the SensiML exporter

Modify [tools/marvin-perf/marvin_perf/exporters/sensiml_csv.py](../../marvin-perf/marvin_perf/exporters/sensiml_csv.py) to add an `--labels=actuator` mode (the current `pressed_mask` path stays as `--labels=detector` for back-compat):

- Track the most recent `Actuator` record alongside `last_detector` (decoder already exists at [tools/marvin-perf/marvin_perf/records.py:300](../../marvin-perf/marvin_perf/records.py)).
- Forward-fill `Actuator.intended_mask` onto each `FretboardRaw` row, emitting frets at bits 0..4 verbatim and a **collapsed strum bit** = `((intended_mask >> 5) | (intended_mask >> 6)) & 1` (logical OR of strum-down and strum-up).
- Emit a per-export stat `n_strum_events` so we can verify strum-bit density on a captured corpus.

The collapse is a property of *this* exporter mode, not the raw capture. `PERF_REC_ACTUATOR` records both strum bits as marvin emitted them; a future training corpus that includes human-trainer data (where alternating up/down is meaningful as ergonomic ground truth) keeps both bits intact on disk and can either be exported through the same collapse for this model, or through a future direction-aware exporter for a different model variant.

## 3. CSV schema

```
timestamp,
ph_green, ph_red, ph_yellow, ph_blue, ph_orange,
fret_green, fret_red, fret_yellow, fret_blue, fret_orange,
strum
```

Same row cadence as today (one row per `FretboardRaw` ≈ 240 Hz).

## 4. Capture protocol

A training corpus is a set of marvin self-play perf-log captures across difficulty levels (Easy, Medium, Expert) and song variety. Each capture must have all of `SESSION + DETECTOR + ACTUATOR + FRETBOARD_RAW` enabled in the `PERF_CMD_SET_TYPE_MASK`.

A held-out song is reserved per session for offline validation; the same held-out set should be reused across training iterations so per-iteration metrics are comparable.

## 5. Model

### 5.1 Recommended baseline

- **Input:** 5 ADC channels × 60 samples (250 ms causal window) @ 240 Hz, int16 → int8 after normalisation.
- **Output:** 6 sigmoids during training (independent BCE per bit) — 5 frets + 1 collapsed strum; thresholded to binary at deploy. The runtime stage maps the strum bit into the wire byte's bit 5 (strum-down); bit 6 stays 0.
- **Architecture:** MPLAB ML / SensiML auto-pipeline first — windowed feature extractor (MAV, std, slope, peak, simple FIR taps) feeding a gradient-boosted ensemble or tiny dense head. SensiML targets exactly this MCU class.
- **Fallback if auto-pipeline misses strum timing:** tiny causal 1D-CNN, two conv layers ~8 channels, kernel 5–7, dilation `[1, 4]`. Quantise to int8.

### 5.2 Why 250 ms

`TP_STRUM_DELAY_MS = 220` ([timing_pipeline.c:21](../../../firmware/marvin/default/src/actuator/timing_pipeline.c)). 60 samples × 4.17 ms = 250 ms gives ≈30 ms margin around the photo-dip-to-strum-bit relationship. Window length is the most important hyperparameter to sweep — see [review.md Q3](review.md#q3-look-ahead-vs-now-cast).

### 5.3 Quantisation

int8 weights, int16 activations during the conv layers, int8 at the output. The intended deployment target has no FPU; floating-point inference is a non-starter. If the offline phase chooses to revisit the deployment target, the quantisation choice may relax.

## 6. Loss

Independent BCE per output bit, weighted to compensate for class imbalance:

- Fret bits are dense (often 1 for many ticks per chord); near-1:1 weighting works.
- The strum bit is sparse (≤2 ticks per chord, ~5% of samples); weight strongly to avoid the trivial "always 0" minimum. Focal loss is a sane alternative if simple weighting plateaus.

If per-bit independence produces poor strum timing, switch to a multi-class head over common chord shapes — see [review.md §alternatives](review.md#12-alternatives-considered).
