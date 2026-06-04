# model — StrumNet (the actual network)

What the model *is*, layer by layer, with the numbers that matter. Code: [`edge_ai/model.py`](../edge_ai/model.py). For the data it consumes see [`training.md`](training.md); for why we chose a CNN over MPLAB ML see the [journal](journal.md) decision log.

> **TL;DR.** A tiny causal 1D-CNN: 5 ADC channels → 3 dilated conv layers → a linear head on the **last timestep** → 6 sigmoids (5 frets + 1 strum). Two facts dominate everything: the **receptive field must cover the ~48-sample photo→strum lag** — the default is now `dilations=(1,4,16)` → **RF 85 (~354 ms)**, after the A/B below showed the old `(1,4)` RF-21 default capped `actuator-fb` frets at ~0.86 — and the **deploy cost is far below what `count_macs` prints**.

---

## 1. Input

- **Shape:** `(batch, 5, window)` — 5 phototransistor ADC channels (G/R/Y/B/O), `window` consecutive 240 Hz samples, channels-first for `Conv1d`.
- **Normalisation:** per-channel standardise (zero-mean/unit-var) using stats computed on the training set and saved in the checkpoint ([`data.compute_norm_stats`](../edge_ai/data.py)). Deploy folds this into the int8 input affine.
- **Windowing:** sliced **by row index** within a contiguous `fb_seq` run, never by the bursty `timestamp` (see [journal](journal.md)).
- **At deploy:** one inference per 240 Hz tick, the window being the most recent `window` ADC samples.

## 2. Output

- **6 logits → sigmoids**, thresholded to bits: `[green, red, yellow, blue, orange, strum]`.
- Independent per-bit (no chord-shape coupling). Strum is the collapsed down|up bit; at the wire it maps to bit 5 (strum-down).
- Frets thresholded at 0.5; **strum has its own threshold** (`--strum-thresh`, default 0.5) because its recall has headroom and we trade it for precision. After thresholding, strum goes through the **monostable** post-processor ([`metrics.monostable`](../edge_ai/metrics.py)) — not part of the network.

## 3. Architecture, layer by layer

```
  5 ADC channels × W samples              input  (5, W)
  (G R Y B O), per-channel standardised
            │
            ▼
  ┌─────────────────────────────────────┐
  │ CausalConv1d  5→8  k=5  d=1  + ReLU  │  left-pad 4    → (8, W)
  └─────────────────────────────────────┘
            │
            ▼
  ┌─────────────────────────────────────┐
  │ CausalConv1d  8→8  k=5  d=4  + ReLU  │  left-pad 16   → (8, W)
  └─────────────────────────────────────┘
            │
            ▼   last timestep only:  h[:, :, -1]          → (8,)
  ┌─────────────────────────────────────┐
  │ Linear  8→6                          │               → 6 logits
  └─────────────────────────────────────┘
            │
            ▼   sigmoid → threshold (frets 0.5, strum tunable)
     [ G  R  Y  B  O │ strum ]
                         │
                         ▼   deploy only (not in the net): monostable one-shot
                      clean strum pulse → wire byte bit 5
```

Default `StrumNet(channels=8, kernel=5, dilations=(1,4,16))`:

| Layer | Op | Out shape | Params |
|---|---|---|---|
| input | — | (5, W) | — |
| `CausalConv1d` 1 | Conv1d(5→8, k=5, **d=1**) + left-pad 4 | (8, W) | 5·8·5 + 8 = **208** |
| ReLU | — | (8, W) | — |
| `CausalConv1d` 2 | Conv1d(8→8, k=5, **d=4**) + left-pad 16 | (8, W) | 8·8·5 + 8 = **328** |
| ReLU | — | (8, W) | — |
| `CausalConv1d` 3 | Conv1d(8→8, k=5, **d=16**) + left-pad 64 | (8, W) | 8·8·5 + 8 = **328** |
| ReLU | — | (8, W) | — |
| take last timestep | `h[:, :, -1]` | (8,) | — |
| `head` | Linear(8→6) | (6,) | 8·6 + 6 = **54** |
| | | **total** | **918** |

The old 2-layer `(1,4)` default was 590 params (RF 21); adding the d=16 layer is +328 params for RF 85. At `channels=64` (the diagnostic runs) it's larger again — only width changes there, not depth.

### Causal convolution
[`CausalConv1d`](../edge_ai/model.py) left-pads the input by `(kernel-1)·dilation` and uses a normal `Conv1d`, so `output[t]` depends only on inputs `≤ t` — no future leakage. That's what lets us train on whole windows and deploy as a streaming "predict-now" model.

## 4. Receptive field — the thing to internalise

Only the **last timestep** feeds the head (`h[:, :, -1]`). Its receptive field is:

```
RF = 1 + Σ (kernel-1)·dilation = 1 + (5-1)·1 + (5-1)·4 = 21 samples ≈ 87.5 ms
```

```
  prediction at "now" depends ONLY on the last 21 samples:

    … now-48 ………………………… now-21 ──────────────── now
       │                       │<───────── RF = 21 ─────────>│
       ▼                       │        (all the head sees)  │
  photo dip that CAUSES        └─────────────────────────────┘
  this strum is HERE  ✗  — ~48 samples back, outside the receptive field
  (samples older than now-21 are convolved but never reach the head)
```

So the prediction at "now" depends **only on the last 21 ADC samples**, regardless of `channels`, and **regardless of `--window`**. Three consequences, all of which we've already seen:

1. **Widening the window does nothing.** Inputs older than 21 samples are convolved but never reach the head. This is exactly why `--window 96` and `128` gave no improvement over `60` — anything past ~21 is discarded.
2. **RF (21) < the photo→strum lag (≈48 samples / 200 ms on hard).** The causal photo dip that *causes* a strum sits ~48 samples back — **outside the receptive field.** The model can't see the dip directly. It works on hard mode only because slow scrolling makes each note's sensor occlusion *wide* enough that its tail reaches into the last 21 samples. Faster difficulties → narrower occlusion → less of it inside the RF → harder. This is a second, independent reason Expert was worse.
3. **The detector-fb fret result was flattered by timing.** detector-`pressed` (y=311) sits ~coincident with the photo dip (~5 samples apart), *inside* the RF — so frets hit 0.99 easily. **But `actuator-fb` frets are at strike-line timing (~48 samples after the dip), outside the RF.** Expect frets on the correctly-timed line-263 dataset to be **harder than the detfb 0.99**, limited by the same RF wall as strum — not because the structure is wrong, but because the network can't reach back to the cue.

**Fix (done, confirmed 2026-06-04 — now the default):** enlarge the RF to cover the lag by adding a dilated layer — `dilations=(1, 4, 16)` → RF = 1 + 4·(1+4+16) = **85 samples (~354 ms)** — while **keeping the last-timestep head**. (A temporal-pooling head would also use the whole window, but for a precise predict-*now* task — strum onset especially — pooling blurs *when*; widening the RF with dilations is the better fix.)

Overfit A/B on the line-263 `actuator-fb` data (slowride-hard, train==eval) confirmed prediction #3 and the fix in one shot:

| | RF 21 `(1,4)` | RF 85 `(1,4,16)` |
|---|---|---|
| frets (G/R/Y/B/O) | 0.86 / 0.84 / 0.89 / 0.91 / 0.94 | **0.97 / 0.97 / 0.98 / 0.98 / 0.99** |
| loss (plateau) | 0.309 | **0.080** |
| strum recall / precision | 0.84 / 0.53 | **0.97 / 0.82** |
| strum p95 | 20.8 ms | 16.7 ms |

RF 21 can't reach back to the cue → caps at ~0.86; RF 85 recovers to ~0.99 at 50 k MACs (under budget). `(1,4,16)` is now the default in `model.py`/`cli.py`.

## 5. Compute cost — `count_macs` is a loose upper bound

[`count_macs`](../edge_ai/model.py) multiplies conv cost by the **full window** (it assumes every output position is computed, as in dense training):

| config | `count_macs` (dense, ×window) |
|---|---|
| 8 ch, window 60 | ~31 k |
| 8 ch, window 96 | ~50 k |
| 64 ch, window 96 | ~2.12 M |

But at deploy you only need the **last timestep**, whose RF is 21 samples — so you compute a handful of conv positions, not the whole window:

| config | minimal deploy MACs (last timestep only) |
|---|---|
| 8 ch | conv1: 5 positions·200 + conv2: 1·320 + head 48 ≈ **1.4 k** |
| 64 ch | 5·1600 + 1·20480 + 384 ≈ **29 k** |

So against the ~100 k MAC/tick budget at 24 MHz, **even the 64-channel model fits comfortably** if deployed last-timestep-only (or incrementally, caching conv1 activations in a ring buffer). The "13× over budget" we kept saying is a `count_macs` artifact of the dense formula — real deploy cost is 1–2 orders of magnitude lower. **We are not as capacity-constrained as the printout implies** — relevant when we consider widening or deepening for the RF fix.

## 6. Training

- **Loss:** `BCEWithLogitsLoss` with a `pos_weight` vector — 1.0 on each fret, `pos_weight` on strum (auto = inverse strum frequency, ~10–17; override with `--strum-weight`). Strum sparsity (~5–9%) otherwise collapses it to "always 0". ([`train.train`](../edge_ai/train.py))
- **Optimiser:** Adam, lr 1e-3, batch 256.
- **Strum label dilation** (`--strum-dilate`): optionally widen the strum positive by ±k samples in the *training* labels so a near-miss isn't fully penalised (eval is never dilated).
- **Checkpoint** stores `state_dict`, `window`, `channels`, `kernel`, and the norm `mean`/`std`.

## 7. Inference & deployment

- **Threshold** the 6 sigmoids (frets 0.5, strum `--strum-thresh`).
- **Monostable** on the strum bit: rising-edge-triggered one-shot (`hold` ticks high, then `refractory` block) — guarantees clean uniform pulses, absorbs glitches, one strum per rising edge. A small counter; MCU-runtime friendly. ([`metrics.monostable`](../edge_ai/metrics.py))
- **Quantisation (Phase 3, not done):** int8 weights / int16 conv activations / int8 output; the target M0+ has no FPU so float inference is out. Standardisation + threshold fold into the input/output affines.

## 8. Known limitations / open design questions

- **~~Receptive field too small for the lag~~ (resolved §4)** — was the headline issue; the `(1,4,16)` RF-85 default now covers the lag (overfit frets ~0.99). Held-out generalisation across songs is the remaining check.
- **Last-timestep head** — correct for a predict-*now* model; fine as-is now the RF covers the lag. (Pooling over time would use more of the window but blur onset timing — not the right fix here.)
- **Per-bit independence** — chords occupy a tiny subset of 2⁶; a chord-shape head or the two-head event detector ([review.md](review.md)) is the fallback if independence caps accuracy.
- **Capacity vs width — resolved for deploy: the deployed hard model uses `--channels 16`.** 8 ch is capacity-limited on the sparse strum class (overstrummed on hardware; no threshold fixed it). 16 ch lifts the strum precision/recall curve (mono .856/.969 → .942/.981) and is affordable now that inference runs in the main loop, not the ISR (~12.5k MACs, RAM ~4 KB of 8 KB). The training *default* stays 8 ch; deploy passes `--channels 16`. 32 ch would be RAM-bound (`s_act` ≈ 5.8 KB), not compute-bound.
- **Quantisation** — PTQ is effectively lossless (frets ±0.001 vs float, strum recall unchanged), so QAT is unneeded so far; revisit only if a future architecture degrades under int8.
