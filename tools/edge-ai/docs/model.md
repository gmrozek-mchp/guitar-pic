# model — StrumNet (the actual network)

What the model *is*, layer by layer, with the numbers that matter. Code: [`edge_ai/model.py`](../edge_ai/model.py). For the data it consumes see [`training.md`](training.md); for why we chose a CNN over MPLAB ML see the [journal](journal.md) decision log.

> **TL;DR.** A tiny causal 1D-CNN: 5 ADC channels → 2 dilated conv layers → a linear head on the **last timestep** → 6 sigmoids (5 frets + 1 strum). ~590 params at the default width. Two facts dominate everything: its **receptive field is only 21 samples (~88 ms)**, and the **deploy cost is far below what `count_macs` prints**.

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

Default `StrumNet(channels=8, kernel=5, dilations=(1,4))`:

| Layer | Op | Out shape | Params |
|---|---|---|---|
| input | — | (5, W) | — |
| `CausalConv1d` 1 | Conv1d(5→8, k=5, **d=1**) + left-pad 4 | (8, W) | 5·8·5 + 8 = **208** |
| ReLU | — | (8, W) | — |
| `CausalConv1d` 2 | Conv1d(8→8, k=5, **d=4**) + left-pad 16 | (8, W) | 8·8·5 + 8 = **328** |
| ReLU | — | (8, W) | — |
| take last timestep | `h[:, :, -1]` | (8,) | — |
| `head` | Linear(8→6) | (6,) | 8·6 + 6 = **54** |
| | | **total** | **590** |

At `channels=64` (the diagnostic runs) it's **~22.6 k params** — RF and depth are unchanged, only width.

### Causal convolution
[`CausalConv1d`](../edge_ai/model.py) left-pads the input by `(kernel-1)·dilation` and uses a normal `Conv1d`, so `output[t]` depends only on inputs `≤ t` — no future leakage. That's what lets us train on whole windows and deploy as a streaming "predict-now" model.

## 4. Receptive field — the thing to internalise

Only the **last timestep** feeds the head (`h[:, :, -1]`). Its receptive field is:

```
RF = 1 + Σ (kernel-1)·dilation = 1 + (5-1)·1 + (5-1)·4 = 21 samples ≈ 87.5 ms
```

So the prediction at "now" depends **only on the last 21 ADC samples**, regardless of `channels`, and **regardless of `--window`**. Three consequences, all of which we've already seen:

1. **Widening the window does nothing.** Inputs older than 21 samples are convolved but never reach the head. This is exactly why `--window 96` and `128` gave no improvement over `60` — anything past ~21 is discarded.
2. **RF (21) < the photo→strum lag (≈48 samples / 200 ms on hard).** The causal photo dip that *causes* a strum sits ~48 samples back — **outside the receptive field.** The model can't see the dip directly. It works on hard mode only because slow scrolling makes each note's sensor occlusion *wide* enough that its tail reaches into the last 21 samples. Faster difficulties → narrower occlusion → less of it inside the RF → harder. This is a second, independent reason Expert was worse.
3. **The detector-fb fret result was flattered by timing.** detector-`pressed` (y=311) sits ~coincident with the photo dip (~5 samples apart), *inside* the RF — so frets hit 0.99 easily. **But `actuator-fb` frets are at strike-line timing (~48 samples after the dip), outside the RF.** Expect frets on the correctly-timed line-263 dataset to be **harder than the detfb 0.99**, limited by the same RF wall as strum — not because the structure is wrong, but because the network can't reach back to the cue.

**Fix direction (not yet done):** enlarge the RF to cover the lag — e.g. dilations `(1, 4, 16)` → RF = 1+4+16+64 = **85 samples (~354 ms)**, or a third layer, or replace the last-timestep head with temporal pooling over the window. This is the most likely next architecture change once we're on the consistent dataset.

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

- **Receptive field too small for the lag** (§4) — the headline issue. Almost certainly needs more dilation/depth or a pooling head before the correctly-timed `actuator-fb` frets and the strum can hit the gate robustly.
- **Last-timestep head discards the window** — a temporal pool (avg/max/attention over time) would actually *use* the window we feed and naturally widen the effective context.
- **Per-bit independence** — chords occupy a tiny subset of 2⁶; a chord-shape head or the two-head event detector ([review.md](review.md)) is the fallback if independence caps accuracy.
- **Capacity vs width** — 8 ch underfits strum recall, 64 ch fits well and (per §5) likely deploys fine; the right width is an open sweep, no longer obviously budget-bound.
- **No quantisation-aware training yet** — int8 effects on the tiny conv are unmeasured.
