# edge-ai — Design Specification

> **Status:** running on hardware. A 16-channel int8 StrumNet, distilled from marvin's hard-mode
> play, runs standalone on the fretboard PIC32CM — reads its own photosensors and drives the game
> with marvin out of the command path. The deploy model was retrained 2026-08-03 on the
> recalibrated-sensor `commanded-fb` corpus. Only `hard` is trained; play quality has not yet been
> scored against a marvin baseline (§6). Current focus and next steps: [journal](journal.md).

## 1. Purpose & scope

A small ML model that takes the fretboard's 5-channel phototransistor ADC stream and emits the same
fret + strum bitmask marvin's timing pipeline produces, so gameplay runs on the fretboard MCU without
marvin in the loop.

Offline-first: capture, label, train and validate happen on a dev PC against recorded marvin
self-play; on-device deployment came last, after the model was proven offline.

**In scope:** the training-data export mode in [marvin-perf](../../marvin-perf/) (`export-ml`), host
training/validation/quantisation (this package), and the on-device inference on the fretboard MCU
(PIC32CM6408PL10048, M0+ @ 24 MHz — [runtime.md](runtime.md)).

**Out of scope:** replacing marvin as the *reference* detector (marvin stays the teacher and
diagnostic source of truth), and game-state / menu navigation (still marvin's — marvin
[spec.md](../../../firmware/marvin/docs/spec.md) §4.8).

## 2. Why distill marvin's command

- **Spatial mismatch.** marvin's CV (`cv_marvin_v1`) samples a different screen row than the
  physical phototransistors. Labelling ADC samples with the CV's `pressed_mask` bakes in a fixed
  offset that is correct at only one scroll speed.
- **Strum timing.** The chord window + strum scheduling in marvin's timing pipeline is real work;
  a per-fret "pressed" label leaves it out.

Distilling against the command marvin actually *decided* dissolves both: the label already contains
the strum timing, and the model only needs a causal window long enough to learn whatever photo-dip →
command delay the sensor placement implies. Mechanical alignment becomes a non-requirement.

## 3. Decisions locked

| Decision | Why (one line) |
|---|---|
| **Output:** 6 bits — 5 frets + 1 collapsed strum. On the wire the strum is bit 5 (strum-down); bit 6 is always 0. | Alternating up/down is a human-ergonomics constraint the controller doesn't enforce; collapsing halves the rare-class problem. The collapse happens at export time — raw captures keep both bits. |
| **Supervision:** `commanded_mask` — marvin's *released-style* per-note CV command, stamped by the fretboard into the same 240 Hz frame as the ADC scan (`--labels=commanded-fb`, schema v6). | Atomic sensor↔label pairing (no cross-stream USB skew) and no legato hold in the label. See journal decision log 2026-08-03. |
| **Deployment target:** fretboard PIC32CM6408PL10048. | Removes marvin from the gameplay loop. The int8 model fits with large margin. |
| **Inputs (v1):** the existing 5 ADC channels only. | Cheapest start; extra sensors are an open question ([review.md](review.md) Q1/Q5). |
| **One model per difficulty.** | GH3 scroll speed varies with difficulty (measured 2026-08-08), so the photo→strum lag differs per tier and mixing tiers is ill-posed. marvin pushes the played difficulty to the node's model selector. |
| **Primary path PyTorch,** MPLAB ML / SensiML a later demonstration track. | Strum timing is a sequence/event problem; windowed-feature AutoML handles it poorly. |

## 4. Data flow

**Training (offline):**

```
marvin self-play (marvin = active CV teacher, `fretboard stream on`)
  fretboard 18-byte T1S frame @ 240 Hz: 5×ADC + sample_seq + applied_mask + commanded_mask
        │  (marvin logs it as PERF_REC_FRETBOARD_RAW)
        ▼
marvin-perf record → export-ml --labels commanded-fb → CSV (fb_seq, 5 ADC, 6 labels)
        ▼
edge-ai train → edge-ai quantize → firmware/fretboard/model_weights.h
```

**Inference (on the fretboard, as built):**

```
TC0 ISR @ 240 Hz:  fret_scan_all() → push scan onto SPSC queue
                   cmd = armed ? s_latest_cmd : 0
                   data_stream_send(cmd, teacher_mask)      (ADC + masks → marvin, when streaming)
main loop:         drain queue → model_infer_stream_step() → s_latest_cmd
                   T1SDetector_SetCommand(cmd, armed)        → fauxmote (drives the Wii) + guitar node
```

One inference per 240 Hz tick; the deployed streaming path costs ~3 k MACs/tick against a ~100 k
cycle budget. Arming is the node's `arm on|off` CLI or marvin's control channel; it boots disarmed.
Details in [runtime.md](runtime.md).

## 5. Status by phase

| Phase | Status |
|---|---|
| 1. Data pipeline (exporter) | Done — `commanded-fb` is the current label source; `actuator` / `actuator-fb` / `detector-fb` kept for validation and diagnostics. |
| 2. Host baseline (PyTorch) | Done — RF-85 model; held-out frets ≥0.96 except red ~0.89, strum recall ~0.965 (2026-06-04). |
| 3. On-device port | Done — int8 PTQ, two inference modules, bit-exact with the host reference (`tests/test_bitexact.py`, `tests/test_stream_bitexact.py`). |
| 4. Side-by-side dry-run | Subsumed — went straight to standalone; `applied_mask` telemetry carries the model's output. |
| 5. Cut-over | In progress — model drives the game when armed. **Open:** game-score comparison vs a marvin-driven baseline; models for easy/medium/expert. |

## 6. Phase 5 verification (open)

Actual game score on a fixed setlist, 5+ runs per song: marvin-driven baseline vs model-driven.
Pass threshold not yet set.

## 7. Document map

| Doc | What's in it |
|---|---|
| [training.md](training.md) | Exporter label modes, CSV schema, capture protocol, loss |
| [model.md](model.md) | StrumNet layer by layer — receptive field, compute cost, int8 path, limitations |
| [runtime.md](runtime.md) | On-device deployment — the two inference modules, main-loop wiring, memory, model selection |
| [review.md](review.md) | Risks, open design forks, alternatives rejected |
| [journal.md](journal.md) | Current focus, decision log, session log |
