# edge-ai — Design Specification

> **Status:** running on hardware (2026-06-04). A 16-channel int8 StrumNet, distilled from marvin's hard-mode play across 5 songs, runs standalone on the fretboard PIC32CM — reads its own photosensors, drives the controller, plays hard reasonably well on the bench with marvin disconnected. Offline phases (data pipeline, training, int8 quantisation) and the on-device port are done; remaining work is play-quality polish and difficulty/timing coverage. See [rollout.md](rollout.md) for phase status, the [journal](journal.md) for current focus, and [review.md](review.md) for open questions, risks, and rejected alternatives.

## 1. Purpose & scope

A development effort to build a small ML model that takes the fretboard's 5-channel phototransistor ADC stream and emits the same 1-byte fret + strum bitmask that marvin's timing pipeline produces today. Once trained, validated, and deployed, it removes marvin from the runtime gameplay loop.

This is intentionally an **offline-first development effort**. Almost all of the work — capture, label, train, validate, debug — happens on a dev PC against recorded marvin self-play data. The on-device deployment is the *last* phase, after the model has been proven offline.

**In scope:**

- Training-data plumbing: a new export mode in [tools/marvin-perf](../../marvin-perf/) that turns marvin self-play perf-log captures into labelled CSV.
- Host-side training, validation, and model artifacts.
- On-device runtime integration (**built**). The deployment target is the fretboard MCU (PIC32CM6408PL10048, M0+ @ 24 MHz) — see [runtime.md](runtime.md); the offline phases were MCU-agnostic and the target could have been revisited if the model size/shape demanded it, but the int8 model fits with large margin.

**Out of scope:**

- Replacing marvin as the *reference* detector. Marvin remains the source of truth for training and diagnostics.
- Game-state / menu navigation. The edge AI plays gameplay only; whole-session control still belongs to marvin (system [SPEC.md §4.8 of marvin spec](../../../firmware/marvin/docs/spec.md)).
- The fretboard-takeover fallback ([fretboard journal](../../../firmware/fretboard/docs/journal.md), 2026-06-02 entry). That mode runs `fret_button.c` standalone; the edge AI is a separate operating mode if/when it deploys to fretboard.

## 2. Why now

Two concrete pressures came out of the recent training-data work:

### 2.1 Spatial mismatch between current label and ADC

The current SensiML CSV exporter ([tools/marvin-perf/marvin_perf/exporters/sensiml_csv.py](../../marvin-perf/marvin_perf/exporters/sensiml_csv.py)) labels each fretboard ADC sample with `cv_marvin_v1`'s `pressed_mask`, broadcast forward by `frame_epoch`. But the two sensors look at different points on the screen:

- `cv_marvin_v1` samples at `y = 311` ([detector/cv_marvin_v1.c](../../../firmware/marvin/default/src/detector/cv_marvin_v1.c)), above the strike line.
- The five phototransistors physically sit "above the TV strike line" ([fretboard SPEC.md](../../../firmware/fretboard/SPEC.md)), at a different (unverified) row than `y = 311`.

Same falling note, different sensors, different times. Aligning them on `frame_epoch` bakes in a fixed offset that is only correct at one scroll speed. Fixing this in hardware is finicky and locks the system to a single mechanical layout.

### 2.2 Strum timing missing from the AI's job

Today's training pipeline emits per-fret pressed labels only. The chord window + +220 ms strum scheduler ([actuator/timing_pipeline.c:21,198,199](../../../firmware/marvin/default/src/actuator/timing_pipeline.c)) is real, non-trivial work that the AI is not being asked to learn. Whatever the AI replaces, the timing pipeline still has to live somewhere.

The simplifying observation is that the model only needs to learn *when* to strum — not *which direction* — because alternating up/down is a human ergonomics constraint the controller doesn't enforce. That collapses 2 strum bits into 1 and halves the rare-class label density problem.

## 3. Decisions locked

| Decision | Why (one line) |
|---|---|
| **Output:** 6 bits — 5 fret bits + 1 collapsed strum bit. At the wire it's emitted as strum-down (bit 5) only; bit 6 is always 0. | Pulls strum timing into the AI; alternating up/down is a human ergonomic constraint the controller doesn't enforce. The collapse happens *at export time*, not in the recording — `PERF_REC_ACTUATOR` keeps both strum bits as captured so a future human-trainer corpus can preserve direction information for other uses. |
| **Supervision:** distillation against `PERF_REC_ACTUATOR.intended_mask` from marvin self-play. | Sidesteps spatial mismatch — labels are marvin's chosen byte, not a CV intermediate. |
| **Intended deployment target:** fretboard PIC32CM6408PL10048, M0+ @ 24 MHz. | Removes marvin from the gameplay loop; truly "edge". Offline phases are MCU-agnostic, so this can be revisited if the model demands more compute. |
| **Inputs at inference (v1):** the existing 5 ADC channels only. | Cheapest start; per-fret HW expansion deferred (see [review.md](review.md) Q1). |
| **Training corpus:** marvin self-play recordings only. | Simplest closure. AI inherits marvin's behavior — good and bad — see [review.md §risks](review.md#risks). |

The key insight that makes this work: **distilling against marvin's commands dissolves the spatial-mismatch problem.** The label is the byte marvin chose with the +220 ms strum delay already baked in. The model just needs a wide enough causal window of ADC to learn whatever delay maps photo dip → strum, regardless of where the photoxistors physically sit. Mechanical alignment becomes a non-requirement.

## 4. Document map

| Doc | What's in it |
|---|---|
| [architecture.md](architecture.md) | Training-time and inference-time data flow; cadence; sanity-check that the shape addresses §2 |
| [training.md](training.md) | Exporter changes, CSV schema, capture protocol, model architecture |
| [model.md](model.md) | The actual network (StrumNet) layer by layer — design rationale, receptive field, params, int8 deploy path, limitations |
| [runtime.md](runtime.md) | On-device deployment (as built) — the two int8 inference modules (streaming / recompute), callback wiring, mode toggle, memory budget |
| [rollout.md](rollout.md) | Phased plan with explicit offline/hardware split, plus per-phase verification (offline + on-device port done) |
| [review.md](review.md) | Risks, open questions surfaced for reviewer pushback, and alternatives considered |
