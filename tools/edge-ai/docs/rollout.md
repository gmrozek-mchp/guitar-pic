# rollout

Phased plan with explicit offline / hardware split. **Status (2026-06-04): phases 1–2 done; phases 3–5 collapsed into a single standalone on-device bring-up that is running on the bench** (marvin out of the loop — see the journal). Remaining: play-quality scoring vs a baseline (the original Phase 5 gate) and difficulty/timing coverage.

## Offline phases

| Phase | Goal | Status / done when |
|---|---|---|
| **1. Data pipeline** | Distillation labels available end-to-end | **✓ Done.** `--labels=actuator`/`actuator-fb` exporters in [tools/marvin-perf](../../marvin-perf/); real captures inspected, plausible fret + strum timing. |
| **2. Host-side baseline** | Train on PC; validate offline | **✓ Done (PyTorch).** RF-85 model: held-out per-bit accuracy ≥0.96 (red ~0.89), strum recall ~0.965 / p95 ~20 ms. The synthetic p95 ≤ 20 ms gate is likely stricter than the game needs (real gate is game score, below). |

### Phase 1 verification

Run `marvin-perf export-ml --labels=actuator capture.bin out.csv`; manually inspect:

- Row count ≈ 240 × duration_s.
- Strum bits sparse (a few %).
- Fret bits dense and matching gameplay difficulty.

### Phase 2 verification

Training script reports per-bit accuracy and strum-event timing distribution on the held-out split. Plot predicted vs. true command stream over a 30 s window; visually verify chord+strum alignment.

## Hardware-touching phases

Scoped with the user into a single **standalone model-driven** bring-up (marvin out of the loop, fretboard debugger + serial on the PC) rather than the staged 3→4→5 dry-run, since the model owns the wire directly.

| Phase | Goal | Status / done when |
|---|---|---|
| **3. On-device port** | Same model running on PIC32 | **✓ Done.** int8 PTQ ([`quantize.py`](../edge_ai/quantize.py)); two inference modules ([runtime.md](runtime.md)) **bit-exact** with the host reference (gated by `tests/test_bitexact.py` + `test_stream_bitexact.py`). Streaming locks to 240 Hz. |
| **4. Side-by-side dry-run** | AI predicts but doesn't drive | **Subsumed.** Went straight to standalone model-driven; `applied_mask` telemetry carries the model's command for live inspection instead of a separate `PERF_REC_MODEL_OUT`. |
| **5. Cut-over** | AI drives the controller | **In progress.** Build-time `FRETBOARD_MODE=MODEL_DRIVEN` + SW0 runtime toggle flips wire ownership from `cmd_receive` to the model; plays hard on the bench. **Open:** gameplay scoring against a marvin-driven baseline. |

### Phase 3 verification

Unit test feeds a recorded ADC trace through host inference and on-device inference, asserts identical outputs (or ≤1 LSB diff after quant). On-device latency measured from a perf-log timestamp around the inference call.

### Phase 4 verification

Extend perf-log with `PERF_REC_MODEL_OUT` (mirrors `Actuator` shape); marvin-perf gains a side-by-side viewer; per-tick Hamming distance between model output and marvin output is logged live.

### Phase 5 verification

Actual game scoring on a fixed song setlist (Easy + Medium + Expert), 5+ runs each: marvin-driven baseline vs. model-driven. Pass if model-driven scores within X% of baseline (X to be set by reviewer).

## Gating between offline and hardware phases

A reviewer should be able to look at phase 2's metrics — per-bit accuracy, strum-event timing distribution, and a visual side-by-side over a 30 s window — and decide *yes, this is worth porting*. If the metrics are weak, [review.md](review.md) lists the natural pivots: window length sweep, multi-class chord head, two-head event detector, alternative label source. The point of the offline-first split is to take those pivots cheaply, before any on-device work has been spent.
