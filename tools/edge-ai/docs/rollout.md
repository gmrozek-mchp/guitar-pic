# rollout

Phased plan with explicit offline / hardware split. Phases 1–4 are entirely offline (Python on a dev PC, no firmware changes). Phase 5 is the first hardware touch.

## Offline phases

| Phase | Goal | Done when |
|---|---|---|
| **1. Data pipeline** | Distillation labels available end-to-end | `--labels=actuator` exporter merged in [tools/marvin-perf](../../marvin-perf/); one captured-gameplay CSV inspected and shows plausible fret + strum timing |
| **2. Host-side baseline** | Train on PC; validate offline | MPLAB ML / PyTorch model achieves ≥95% per-bit accuracy on held-out song; strum-event timing error p95 ≤ 20 ms (one ADC sample) |

### Phase 1 verification

Run `marvin-perf export-ml --labels=actuator capture.bin out.csv`; manually inspect:

- Row count ≈ 240 × duration_s.
- Strum bits sparse (a few %).
- Fret bits dense and matching gameplay difficulty.

### Phase 2 verification

Training script reports per-bit accuracy and strum-event timing distribution on the held-out split. Plot predicted vs. true command stream over a 30 s window; visually verify chord+strum alignment.

## Hardware-touching phases

| Phase | Goal | Done when |
|---|---|---|
| **3. On-device port** | Same model running on PIC32 | Inference latency < 1 ms (well inside the 4.17 ms tick); on-device output matches host inference bit-for-bit on a recorded ADC trace (within int8 quant tolerance) |
| **4. Side-by-side dry-run** | AI predicts but doesn't drive | New perf-log record `PERF_REC_MODEL_OUT` mirrors `Actuator` shape; recorded alongside marvin's commands; per-tick agreement tracked over a real session |
| **5. Cut-over** | AI drives the controller | Fretboard mode-switch flips wire ownership from `cmd_receive` to `model_infer`; gameplay scoring measured against marvin-driven baseline |

### Phase 3 verification

Unit test feeds a recorded ADC trace through host inference and on-device inference, asserts identical outputs (or ≤1 LSB diff after quant). On-device latency measured from a perf-log timestamp around the inference call.

### Phase 4 verification

Extend perf-log with `PERF_REC_MODEL_OUT` (mirrors `Actuator` shape); marvin-perf gains a side-by-side viewer; per-tick Hamming distance between model output and marvin output is logged live.

### Phase 5 verification

Actual game scoring on a fixed song setlist (Easy + Medium + Expert), 5+ runs each: marvin-driven baseline vs. model-driven. Pass if model-driven scores within X% of baseline (X to be set by reviewer).

## Gating between offline and hardware phases

A reviewer should be able to look at phase 2's metrics — per-bit accuracy, strum-event timing distribution, and a visual side-by-side over a 30 s window — and decide *yes, this is worth porting*. If the metrics are weak, [review.md](review.md) lists the natural pivots: window length sweep, multi-class chord head, two-head event detector, alternative label source. The point of the offline-first split is to take those pivots cheaply, before any on-device work has been spent.
