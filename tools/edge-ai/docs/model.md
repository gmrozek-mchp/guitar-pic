# model — StrumNet (the actual network)

What the model *is*, layer by layer, and *why* it's shaped that way. Code: [`edge_ai/model.py`](../edge_ai/model.py). For the data it consumes see [`training.md`](training.md); for the on-device int8 path see [`runtime.md`](runtime.md); for the historical decision trail (why a CNN over MPLAB ML, how the RF was sized) see the [journal](journal.md) decision log.

> **TL;DR.** A tiny causal 1D-CNN: 5 ADC channels → **3 dilated causal conv layers** `(d=1,4,16)` → a linear head on the **last timestep** → 6 sigmoids (5 frets + 1 strum). Three facts shape every choice:
> 1. It is a **predict-now** model — the head reads only the newest timestep, so the architecture is built around that timestep's **receptive field**.
> 2. The receptive field **must cover the photo→strum lag** (~48 samples / 200 ms on hard). That is why there are three dilated layers, not two: `(1,4,16)` gives **RF 85 (~354 ms)**; the old two-layer `(1,4)` was RF 21 and couldn't reach back to the cue, capping frets at ~0.86.
> 3. The model is the **timing function** marvin used to play. It learns whatever photo-dip→command delay was baked into the corpus, so it is only valid for the difficulty + timing constants it was distilled from (see [journal](journal.md) decision log).

The shipped model: `StrumNet(channels=16, kernel=5, dilations=(1,4,16))`, trained at `window=85`, int8-quantised, running standalone on the fretboard PIC32CM. The training *default* is `channels=8`; deploy passes `--channels 16` (§8).

---

## 1. Input

- **Shape:** `(batch, 5, window)` — 5 phototransistor ADC channels (G/R/Y/B/O), `window` consecutive 240 Hz samples, channels-first for `Conv1d`.
- **Normalisation:** per-channel standardise (zero-mean / unit-variance) using stats computed on the training set and saved in the checkpoint ([`data.compute_norm_stats`](../edge_ai/data.py)). **Why it's load-bearing:** the raw ADC band is narrow (idle ≈ 3900, dips ≈ 1000); naive `/4095` leaves every input clustered near 0.95 and starves gradients — the first baseline underfit until standardisation was added. At deploy this affine folds losslessly into the int8 input quantisation, so it costs nothing on device.
- **Windowing:** sliced **by row index** within a contiguous `fb_seq` run, never by the bursty `timestamp` column. Each CSV row is one true 240 Hz sample paired atomically with its label on-device; a dropped frame breaks the run so a window never stitches two non-adjacent samples (see [`data.contiguous_runs`](../edge_ai/data.py), and the [journal](journal.md) timestamp-burstiness entry).
- **At deploy:** one inference per 240 Hz tick. The window is the most recent `window` ADC samples (recompute path) or — in the streaming path — a continuous causal convolution that needs no explicit window buffer at all (§7).

## 2. Output

- **6 logits → sigmoids**, thresholded to bits: `[green, red, yellow, blue, orange, strum]`.
- **Independent per-bit** (no chord-shape coupling). Strum is the **collapsed** down|up bit; at the wire it maps to bit 5 (strum-down), bit 6 stays 0. The collapse halves the rare-class problem and reflects that alternating up/down is a human-ergonomics constraint the controller doesn't enforce ([SPEC.md §3](SPEC.md)).
- Frets thresholded at 0.5; **strum has its own threshold** (`--strum-thresh`, default 0.5) because its precision/recall curve has headroom we deliberately trade. After thresholding, the strum bit passes through the **monostable** post-processor — not part of the network, lives at deploy time (§7).

## 3. Architecture, layer by layer

```
  5 ADC channels × W samples              input  (5, W)
  (G R Y B O), per-channel standardised
            │
            ▼
  ┌─────────────────────────────────────┐
  │ CausalConv1d  5→C  k=5  d=1   + ReLU │  left-pad 4    → (C, W)
  └─────────────────────────────────────┘
            │
            ▼
  ┌─────────────────────────────────────┐
  │ CausalConv1d  C→C  k=5  d=4   + ReLU │  left-pad 16   → (C, W)
  └─────────────────────────────────────┘
            │
            ▼
  ┌─────────────────────────────────────┐
  │ CausalConv1d  C→C  k=5  d=16  + ReLU │  left-pad 64   → (C, W)
  └─────────────────────────────────────┘
            │
            ▼   last timestep only:  h[:, :, -1]          → (C,)
  ┌─────────────────────────────────────┐
  │ Linear  C→6                          │               → 6 logits
  └─────────────────────────────────────┘
            │
            ▼   sigmoid → threshold (frets 0.5, strum tunable)
     [ G  R  Y  B  O │ strum ]
                         │
                         ▼   deploy only (not in the net): monostable one-shot
                      clean strum pulse → wire byte bit 5
```

Parameter counts at the training default `StrumNet(channels=8, kernel=5, dilations=(1,4,16))`:

| Layer | Op | Out shape | Params |
|---|---|---|---|
| input | — | (5, W) | — |
| `CausalConv1d` 0 | Conv1d(5→8, k=5, **d=1**) + left-pad 4 | (8, W) | 5·8·5 + 8 = **208** |
| ReLU | — | (8, W) | — |
| `CausalConv1d` 1 | Conv1d(8→8, k=5, **d=4**) + left-pad 16 | (8, W) | 8·8·5 + 8 = **328** |
| ReLU | — | (8, W) | — |
| `CausalConv1d` 2 | Conv1d(8→8, k=5, **d=16**) + left-pad 64 | (8, W) | 8·8·5 + 8 = **328** |
| ReLU | — | (8, W) | — |
| take last timestep | `h[:, :, -1]` | (8,) | — |
| `head` | Linear(8→6) | (6,) | 8·6 + 6 = **54** |
| | | **total** | **918** |

The shipped 16-channel model is **3 110** params (§8). Only width changes with `--channels`; depth (3 layers) and the dilation schedule are fixed.

### Why these layers, in this order

- **1D convolution, not a dense window or an RNN.** The signal is a 5-channel time series whose informative event (a phototransistor dimming, then recovering) is *local in time but shift-equivariant* — it can occur at any offset within the window. Convolution shares weights across time, so it learns the dip→command mapping once rather than per-position; that is exactly what a dense layer over the flattened window cannot do efficiently, and it has no recurrent state to unroll on an MCU.
- **Causal padding (left-pad only).** [`CausalConv1d`](../edge_ai/model.py) left-pads by `(kernel-1)·dilation` and uses a plain `Conv1d`, so `output[t]` depends only on inputs `≤ t` — no future leakage. This is what lets us train densely on whole windows yet deploy as a streaming "predict-now" model: the training-time and deploy-time computations of `output[t]` are identical.
- **Dilation `(1, 4, 16)`, three layers.** Stacking dilated convs grows the receptive field *geometrically* for a *linear* parameter cost. Three layers at kernel 5 reach RF 85 (§4) — enough to cover the lag — for 918 params; reaching the same RF with un-dilated kernel-5 layers would need ~21 layers. The schedule `1→4→16` keeps coverage gap-free (each layer's stride is ≤ the previous layer's reach) while quadrupling reach per layer.
- **8 channels (training default).** Width sets capacity. 5→8 in layer 0 is a mild expansion; 8 is the smallest width that fit the frets well in early experiments. The strum class is sparse enough that capacity *there* is the binding constraint, which is why deploy uses 16 (§8).
- **ReLU.** Cheap, int8-friendly (a clamp at 0 in the requant), no FPU needed.
- **Linear head on the last timestep only.** The job is "what command applies *now*", so only `h[:, :, -1]` feeds the head. A temporal-pooling head would use the whole window but blur *when* an event happened — fatal for strum onset. The right way to use more of the window is to widen the RF (done), not to pool.

## 4. Receptive field — the thing the architecture is built around

Only the **last timestep** feeds the head (`h[:, :, -1]`). Its receptive field — how many of the newest input samples can influence the prediction — is:

```
RF = 1 + Σ (kernel-1)·dilation = 1 + (5-1)·(1 + 4 + 16) = 85 samples ≈ 354 ms
```

This single number governs the architecture, because of three consequences:

1. **The window cannot help past the RF.** Inputs older than 85 samples are convolved internally but never reach the head. This is why early `--window 96 / 128` sweeps gave nothing over `60` *when the RF was 21* — anything past the RF is discarded. The window only needs to be **≥ RF**; we train the deploy model at `window=85` (= RF) so the streaming deploy path is bit-exact (§7).

2. **The RF must cover the photo→strum lag.** The photo dip that *causes* a strum sits ~48 samples (~200 ms on hard) before the strike-line command. If the RF is shorter than that lag, the head literally cannot see the cue:

   ```
     prediction at "now" depends ONLY on the last RF samples:

       … now-48 ───────────────────────────────────── now
          │                                             │
          ▼            │<──────── RF = 85 ─────────────>│
     photo dip that    │   covers the cue ✓             │
     CAUSES this strum  └────────────────────────────────┘
     (~48 samples back, now INSIDE the receptive field)
   ```

   With the old RF-21 default the dip was *outside* the RF; the model only worked at all because slow hard-mode scrolling widened each note's occlusion enough that its tail leaked into the last 21 samples. Faster difficulties → narrower occlusion → less of it inside the RF → harder. RF 85 removes that fragility.

### Why RF 85, decided by an overfit A/B

The fret-accuracy ceiling was first diagnosed on a detector-labelled probe (labels coincident with the photo dip, so *inside* even a tiny RF — frets hit 0.99 trivially) and then predicted to bite the deployable `actuator-fb` labels (strike-line timing, ~48 samples after the dip). An overfit A/B on the line-263 `actuator-fb` data (slowride-hard, train == eval) confirmed the prediction and the fix in one shot:

| | RF 21 `(1,4)` | RF 85 `(1,4,16)` |
|---|---|---|
| frets (G/R/Y/B/O) | 0.86 / 0.84 / 0.89 / 0.91 / 0.94 | **0.97 / 0.97 / 0.98 / 0.98 / 0.99** |
| loss (plateau) | 0.309 | **0.080** |
| strum recall / precision | 0.84 / 0.53 | **0.97 / 0.82** |
| strum p95 | 20.8 ms | 16.7 ms |

RF 21 can't reach back to the cue → caps at ~0.86; RF 85 recovers to ~0.99 at ~50 k dense MACs (well under budget — §5). `(1,4,16)` is the default in [`model.py`](../edge_ai/model.py) / [`cli.py`](../edge_ai/cli.py); the dilation list is a `--dilations` flag and is stored in the checkpoint (older checkpoints fall back to `(1,4)`).

## 5. Compute cost — `count_macs` is a loose upper bound

[`count_macs`](../edge_ai/model.py) multiplies each conv's cost by the **full window** — it assumes every output position is computed, as in dense training:

| config | `count_macs` (dense, ×window) |
|---|---|
| 8 ch, window 60 | ~50 k |
| 8 ch, window 85 | ~71 k |
| 16 ch, window 85 | ~252 k |

But at deploy you only need the **last timestep**. Two on-device paths exploit that (both in firmware, both bit-exact with the host int8 sim — §7):

| path | what it computes per tick | 16 ch, window 85 |
|---|---|---|
| **recompute** ([`model_infer.c`](../../../firmware/fretboard/model_infer.c)) | only the conv positions the last timestep transitively needs (back-propagated `s_need`: 21 / 5 / 1 positions per layer) | **~16 k MACs** (~12.5 k at window 60) |
| **streaming** ([`model_infer_stream.c`](../../../firmware/fretboard/model_infer_stream.c)) | exactly **one new conv column per layer**, caching prior columns in small rings | **~3.1 k MACs** (888 at 8 ch) |

So against the ~100 k MAC/tick budget at 24 MHz, **even the 16-channel model fits comfortably**, and the streaming path clears 240 Hz with large margin. The "13× over budget" figure we once quoted was a `count_macs` artifact of the dense formula — real deploy cost is 1–2 orders of magnitude lower. We are **not** as capacity-constrained as the dense printout implies, which is what made widening to 16 channels affordable.

## 6. Training

- **Loss:** [`BCEWithLogitsLoss`](../edge_ai/train.py) with a `pos_weight` vector — 1.0 on each fret, `pos_weight` on strum (auto = inverse strum frequency, ~10–17; override with `--strum-weight`). Strum sparsity (~7–13 % on hard) otherwise collapses it to "always 0". The deploy model uses `--strum-weight 5` (below the auto value) to trim the recall bias at the source (§8).
- **Optimiser:** Adam, lr 1e-3, batch 256, 30 epochs (deploy: 50).
- **Strum label dilation** (`--strum-dilate`): optionally widen the strum positive by ±k samples in the *training* labels so a near-miss isn't fully penalised. Eval is **never** dilated.
- **Lead compensation** (`--label-lead`): shift training labels earlier by N samples to pre-empt on-device lag; eval uses the true labels. Unused in the shipped model (kept for residual-lag tuning — see [journal](journal.md) next-steps).
- **Checkpoint** stores `state_dict`, `window`, `channels`, `kernel`, `dilations`, `label_lead`, and the norm `mean`/`std` — everything needed to reconstruct the model and the input affine at eval and quantise time.

## 7. Inference & deployment (int8, on hardware)

This is **built and running standalone on the fretboard PIC32CM** (the int8 path is no longer a future "Phase 3" — it is the deployed path; see [`runtime.md`](runtime.md) and the [journal](journal.md)).

- **Quantisation — post-training, int8, essentially lossless.** [`quantize.py`](../edge_ai/quantize.py) does per-tensor symmetric int8 weights, int32 accumulators, and int64 fixed-point requant back to int8 activations (the target M0+ has no FPU, so float inference is out). Standardisation folds into an integer **input affine** `(M_in, B_in, S_in)`; output thresholding folds into an integer **compare on the head accumulator** (no on-device sigmoid: `sigmoid(x) ≥ t ⇔ x ≥ logit(t)`, so frets compare against 0 and strum against `logit(thresh)`). Measured PTQ error on slowride-hard: frets within ±0.001 of float, strum recall unchanged — so **no QAT is needed**.
- **Bit-exactness gate.** The host int8 reference (`quantize.int8_sim_*`) is asserted bit-for-bit against the actual firmware C — both the recompute path ([`tests/test_bitexact.py`](../tests/test_bitexact.py)) and the streaming path ([`tests/test_stream_bitexact.py`](../tests/test_stream_bitexact.py)), over a full capture, 0 mismatches.
- **Threshold** the 6 head accumulators (frets at 0, strum at the folded `--strum-thresh`).
- **Monostable** on the strum bit: a rising-edge-triggered one-shot (`hold` ticks high, then `refractory` ticks blocking new triggers — [`metrics.monostable`](../edge_ai/metrics.py)). It guarantees clean uniform pulses, absorbs mid-pulse glitches, and emits exactly one pulse per rising edge. A few counters; runs on the MCU. The same logic lives in both firmware paths.
- **Streaming vs recompute (why two paths).** The streaming path is bit-exact with a continuous causal conv, which only matches a model **trained at `window ≥ RF` (= 85)** — at a shorter window the moving zero-pad boundary differs. So the shipped model is trained at window 85 and deployed streaming (locks to 240 Hz, ~3 k MACs/tick). The recompute path works at any trained window but re-runs the needed positions each tick (~93 Hz at 16 ch) and is kept as the general-window fallback. Selected at build time by `MODEL_INFER_STREAMING` in [`fretboard_config.h`](../../../firmware/fretboard/fretboard_config.h).
- **Runtime weight swap.** The active model is a `model_def_t` pointer; the quantiser emits one named `model_<name>` per `--name`, several link together (shared arch dims guarded by a `_Static_assert`), and `model_infer_set_model()` swaps at runtime — the mechanism for per-difficulty models.

## 8. Capacity, limitations & open design questions

- **Deploy width = 16 channels (not the 8 ch training default).** 8 ch is capacity-limited on the sparse strum class: it overstrummed on hardware and **no threshold fixed it** — 0.85 missed real strums *and* still overstrummed, i.e. the precision/recall *curve itself* was the wall (the same signature as the early 8 ch-vs-64 ch overfit diagnostic, strum recall .86 vs .99). 16 ch lifts the whole curve (mono precision/recall .856/.969 → .942/.981) so a single threshold lands both. It's affordable because inference runs cheaply on device (§5: ~3 k MACs streaming, RAM ~4 KB of 8 KB). 32 ch would be RAM-bound, not compute-bound. The training *default* stays 8 ch; deploy passes `--channels 16`.
- **~~Receptive field too small for the lag~~ (resolved §4).** Was the headline issue; `(1,4,16)` RF-85 covers the lag (overfit frets ~0.99, held-out ≥0.96 except red). The remaining open item is generalisation polish, not the RF.
- **Red fret is the lone laggard** (held-out ~0.89 vs ≥0.96 for the others), consistent across epochs and not a corpus-coverage gap. Hypotheses (holdout variance / green-yellow neighbour confusion / `ph_red` channel quality) are listed in the [journal](journal.md) open questions; cheapest test is rotate-holdout cross-val. Carried as a known caveat — confirm it costs game score before treating it as blocking.
- **Last-timestep head** — correct for a predict-*now* model; fine as-is now the RF covers the lag. Temporal pooling would use more of the window but blur onset timing — not the right fix.
- **Per-bit independence** — chords occupy a tiny subset of 2⁶; a chord-shape head or the two-head event detector ([review.md](review.md)) is the fallback if independence caps accuracy. Not hit yet.
- **Single-difficulty validity.** The model *is* the timing function it was distilled from; mixing difficulties (or timing-constant regimes) teaches two photo→strum lags for identical inputs — ill-posed. v1 is one model per regime; multi-difficulty routes (per-difficulty models, or a scroll-speed sensor) are in [review.md](review.md) / the [journal](journal.md).
</content>
</invoke>
