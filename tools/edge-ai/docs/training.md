# training

How marvin self-play recordings become training data, and the training-pipeline choices. The
network itself is in [model.md](model.md); the end-to-end flow is in [SPEC.md §4](SPEC.md).

## 1. Exporter label modes

`marvin-perf export-ml <capture> --out <csv> --labels <mode>`
([`sensiml_csv.py`](../../marvin-perf/marvin_perf/exporters/sensiml_csv.py)):

| `--labels` | Label source | Use |
|---|---|---|
| **`commanded-fb`** | `commanded_mask` in the fretboard frame — marvin's released-style CV command, latched with the ADC scan on-device (schema v6) | **Training corpus** |
| `actuator-fb` | `applied_mask` in the fretboard frame — what the node itself drove (its own model output) | Self-label / validation; not a teacher |
| `actuator` | `PERF_REC_ACTUATOR.intended_mask` forward-filled onto each fretboard row | Legacy; crosses two USB directions, so the join has an unknown skew |
| `detector-fb` | detector `pressed_mask` frets + in-frame strum | Diagnostic probe |
| `detector` (default) | `cv_marvin_v1` `pressed_mask`, 5 frets only | Original SensiML export |

The strum label is collapsed in the exporter: `strum = bit5 | bit6`. Raw captures keep both bits.

## 2. CSV schema (`commanded-fb`, identical for `actuator-fb`)

```
timestamp, fb_seq,
ph_green, ph_red, ph_yellow, ph_blue, ph_orange,
fret_green, fret_red, fret_yellow, fret_blue, fret_orange,
strum
```

One row per fretboard sample (240 Hz). `fb_seq` is the node's sample counter; the loader windows
**within contiguous `fb_seq` runs** so a dropped frame never stitches two non-adjacent samples
([`data.contiguous_runs`](../edge_ai/data.py)). Windows are sliced by row index, never by
`timestamp` (it is stamped at marvin's receive time and is bursty).

## 3. Capture protocol

- **One difficulty per corpus.** Scroll speed varies with difficulty, so the photo→strum lag does
  too; one model per tier (journal decision log 2026-08-08). The shipped corpus is 5 hard songs
  (hitme / rockroll / slowride / story / talkdirty).
- **One timing regime per corpus.** The model *is* the photo→command timing function, so a corpus
  is only valid for the marvin timing it was captured under. The current corpus was captured with
  `TP_STRUM_PULSE_MS=40` and the strum delay of the time; any change to marvin's timing or
  observation lead means re-capturing the whole corpus.
- **Sensor calibration.** A sensor regain invalidates the corpus and the norm stats (2026-08-03).
- **Procedure.** On marvin: `active cv`, `fretboard stream on`, play the song; record with
  `marvin-perf record --out-dir` (or the viewer's Record). Export with `--labels commanded-fb` and
  check the exporter's `n_seq_gaps ≈ 0`.
- **Deploy vs evaluate.** The deploy model trains on all songs (`--no-holdout`); generalisation
  checks hold one song out and reuse it across iterations.

## 4. Training choices

- **Window ≥ receptive field.** The RF (85 samples for dilations `(1,4,16)`) must cover the
  photo→strum lag (measure with `edge-ai lag`); the deploy model trains at `--window 85` because the
  streaming inference engine requires `window ≥ RF` ([model.md §4](model.md), [runtime.md §2](runtime.md)).
- **Width.** Training default is 8 channels; deploy uses 16 for strum capacity ([model.md §8](model.md)).
- **Loss.** Independent BCE per bit. Frets ~1:1; the strum bit is sparse (~7–13 % on hard) so it
  gets a positive weight (`--strum-weight`, auto = inverse frequency ≈ 10–17×; deploy uses 5). The
  precision/recall operating point is then set at quantise time with `--strum-thresh` and the
  monostable (`--strum-hold`, `--strum-refractory`) — header-only, no retrain.
- **Quantisation.** Post-training int8, essentially lossless, no QAT ([model.md §7](model.md)).

Deploy recipe (journal "Current focus"):

```sh
uv run edge-ai train --no-holdout --channels 16 --window 85 --strum-weight 5 --epochs 50 \
    --out model.pt song1.csv song2.csv ...
uv run edge-ai quantize --model model.pt --name hard \
    --strum-thresh 0.80 --strum-hold 8 --strum-refractory 8 \
    --out ../../firmware/fretboard/model_weights.h song1.csv song2.csv ...
```

If per-bit independence ever caps strum timing, the fallbacks are in
[review.md §alternatives](review.md#alternatives-considered).
