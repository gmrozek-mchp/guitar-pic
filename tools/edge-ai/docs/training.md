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

A training corpus is a set of marvin self-play perf-log captures **at a single difficulty (Expert for v1)** across song variety. Each capture must have all of `SESSION + DETECTOR + ACTUATOR + FRETBOARD_RAW` enabled in the `PERF_CMD_SET_TYPE_MASK`.

A held-out song is reserved for offline validation; the same held-out set is reused across training iterations so per-iteration metrics are comparable.

> **Why single-difficulty.** If the game highway's scroll speed varies with difficulty, the photo-dip→strum delay is `(pixel gap photo-row→CV-row)/scroll_speed + 220 ms` — inversely dependent on scroll speed. With 5 sensors at one screen row and no difficulty input, mixing difficulties makes identical inputs map to different correct delays: an *ill-posed* regression, not merely an under-sampled one. So v1 fixes difficulty to Expert (densest strum signal, shortest delay → smallest window). If scroll speed turns out constant across difficulties, difficulties can be merged later at no cost. Eventual multi-difficulty support is one-model-per-difficulty (difficulty selected at runtime from marvin's game-state controller), or a dedicated fret-line scroll-speed sensor — see journal open questions.

## 5. Model

### 5.1 Recommended baseline

- **Input:** 5 ADC channels × N samples causal window @ 240 Hz, windowed **by row index** (each CSV row is one true 240 Hz sample; the `timestamp` column is bursty — see journal), int16 → int8 after normalisation. Size N from the measured photo→strum lag (§5.2), starting near 60 (250 ms).
- **Output:** 6 sigmoids during training (independent BCE per bit) — 5 frets + 1 collapsed strum; thresholded to binary at deploy. The runtime stage maps the strum bit into the wire byte's bit 5 (strum-down); bit 6 stays 0.
- **Architecture (primary):** tiny causal 1D-CNN — two conv layers ~8 channels, kernel 5–7, dilation `[1, 4]`, causal padding, → dense → 6 sigmoids. Quantise to int8. This is the primary path (see journal decision log): the strum-timing problem is a sequence/event problem the auto-pipeline's windowed-feature + tree/ensemble shape handles poorly.
- **MPLAB ML / SensiML (later demonstration track):** windowed feature extractor (MAV, std, slope, peak, simple FIR taps) feeding a gradient-boosted ensemble or tiny dense head. SensiML targets exactly this MCU class, so once a known-good model shape exists it's a strong "here's the Microchip-tooling path" comparison — but it is **not** the v1 critical path.

### 5.2 Sizing the window: measure, don't guess

`TP_STRUM_DELAY_MS = 220` ([timing_pipeline.c:21](../../../firmware/marvin/default/src/actuator/timing_pipeline.c)) is only the *fixed* part of the delay; the photo→strum lag also includes scroll-time from the photo row to marvin's CV row. With difficulty fixed (§4) that total lag is a constant we **measure** from real data by cross-correlating photo dips against strum events (the `edge-ai lag` utility). Size the causal window to span the measured lag plus margin, then sweep narrowly around it (e.g. measured ±1–2 samples of window) rather than blind-sweeping 125/250/400 ms — see [review.md Q3](review.md#q3-look-ahead-vs-now-cast).

### 5.3 Quantisation

int8 weights, int16 activations during the conv layers, int8 at the output. The intended deployment target has no FPU; floating-point inference is a non-starter. If the offline phase chooses to revisit the deployment target, the quantisation choice may relax.

## 6. Loss

Independent BCE per output bit, weighted to compensate for class imbalance:

- Fret bits are dense (often 1 for many ticks per chord); near-1:1 weighting works.
- The strum bit is sparse — measured at **~5.8 %** of samples on the first real Expert capture (551 strum events / 3 496 active rows over 59 958 rows); weight strongly (≈15–20× positive, i.e. the inverse class frequency) to avoid the trivial "always 0" minimum. Focal loss is a sane alternative if simple weighting plateaus.

If per-bit independence produces poor strum timing, switch to a multi-class head over common chord shapes — see [review.md §alternatives](review.md#12-alternatives-considered).
