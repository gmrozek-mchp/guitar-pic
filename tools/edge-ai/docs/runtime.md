# runtime

On-device deployment shape. **As built and running standalone on the fretboard MCU (2026-06-04).** The int8 model reads its own photosensors and drives the controller with marvin disconnected.

## 1. Intended target

PIC32CM6408PL10048, Cortex-M0+ @ 24 MHz, no FPU, **64 KB Flash / 8 KB SRAM** (datasheet DS40002667). The current fretboard MCU. See [SPEC.md §3](SPEC.md#3-decisions-locked) on why this is a soft target rather than a locked one — the offline phases are MCU-agnostic. The deployed 16-channel int8 model fits with large margin (§5, §6).

## 2. Two inference modules (as built)

There are **two** int8 inference implementations of the same model; one is selected at build time by `MODEL_INFER_STREAMING` in [`fretboard_config.h`](../../../firmware/fretboard/fretboard_config.h). Both are integer-only (no FPU), bit-exact with the host reference `edge_ai.quantize.int8_sim`, share the `model_def_t` weight struct from the generated `model_weights.h`, and apply the same strum monostable.

**Streaming — [`model_infer_stream.c`](../../../firmware/fretboard/model_infer_stream.c) (default, `MODEL_INFER_STREAMING=1`).** Computes a *continuous* causal convolution: one new conv column per layer per tick, caching prior columns in small per-layer rings (depths 5 / 17 / 65, sized to each layer's deepest tap). ~3 k MACs/tick at 16 ch — locks comfortably to 240 Hz. The catch: a continuous conv is only bit-exact with a model **trained at `window ≥ receptive field` (= 85 for `(1,4,16)`)** — at a shorter window the windowed model's moving zero-pad boundary wouldn't match. A `_Static_assert(MODEL_WINDOW >= 85)` guards this. The cascade is hard-coded for the 3-layer / kernel-5 / 5-in / 6-out arch (an `#error` guards a mismatched regeneration). The TC0 ISR pushes each scan onto an SPSC sample queue (`s_adc_q`); the main loop drains it and calls `model_infer_stream_step(adc[5])` once per queued sample, in order.

**Recompute — [`model_infer.c`](../../../firmware/fretboard/model_infer.c) (`MODEL_INFER_STREAMING=0`).** Owns a `MODEL_WINDOW × 5` input ring; each tick re-runs only the conv positions the last timestep transitively needs (a back-propagated `s_need` table — 21 / 5 / 1 positions per layer at window 85), skipping the dense per-position conv. Works at **any** trained window, so it's the general-window fallback, but it re-runs the receptive field each tick (~16 k MACs at 16 ch / window 85, measured ~93 Hz) so it **must run in the main loop, not the ISR** (§3). `model_infer_push(adc[5])` from the ISR, `model_infer_run()` from the main loop.

Both modules guard their entire body on `MODEL_INFER_STREAMING`, so the inactive translation unit is empty and allocates no static buffers — both can stay in the MPLAB project; flip the mode in `fretboard_config.h` (or with `-D`) with no add/remove of source files. `model_infer_set_model(const model_def_t *m)` (each module) does the runtime weight swap (per-difficulty).

## 3. The recompute path must run OUTSIDE the TC0 ISR — proven on hardware (2026-06-04)

> Both inference paths run in the main loop; the ISR only samples (pushing to the SPSC queue). The streaming path is cheap enough (~3 k MACs/tick) that it *could* run inline, but it still drains from the queue in the main loop for a uniform shape. This section is the on-hardware proof of why inference is decoupled from sampling — established with the recompute path, which is too heavy to run in the ISR.


The first bring-up ran `model_infer_run()` *inside* the 240 Hz TC0 callback. That fails two ways at once: the long ISR delays the next tick (sampling drops to ~150–190 Hz, so the model's window is no longer 250 ms and its learned photo→strum lag is wrong → late/missed notes), **and** it starves the interrupt-driven SERCOM TX, collapsing the data stream (recv ~22 fps with heavy drops). Measured with [`tools/marvin-perf/fretboard_rate.py`](../../marvin-perf/fretboard_rate.py), which reads `sample_seq` to report the true tick rate.

**Final shape — decouple sampling from inference:**

```c
TC0 ISR (240 Hz, short & bounded)
    fret_scan_all();
    // streaming: push scan onto the SPSC queue s_adc_q for the main loop
    // recompute: model_infer_push(scan)   // sample the window at a clean 240 Hz
    cmd = actuation_armed() ? s_latest_cmd : 0u;  // SW0 arms actuation; LED0 shows it
    s_current_cmd = cmd;                          // also the applied_mask in the frame
    data_stream_send(cmd);                        // stream ADC + driven mask up to marvin

main loop (best-effort)
    // streaming: drain s_adc_q, one model_infer_stream_step() per sample, in order
    // recompute: s_latest_cmd = model_infer_run();  // heavy, preempted by the ISR
    T1SDetector_SetCommand(s_current_cmd);        // forward the command to the guitar over T1S
```

`s_latest_cmd` is a single `volatile uint8_t` (atomic on the M0+). `model_infer_run()` snapshots the ring cursor on entry so a concurrent `push()` can't shift the window mid-inference. Result: ISR stays short → 240 Hz sampling + serial both clean (measured 240/240, ~0 drops); command latency ≈ one inference period (~5 ms) + ≤1 tick. Build at **`-O2`/`-O3`** — `-O0` alone overruns the tick (the int8 conv + requant is heavy without optimisation).

The command byte is forwarded to the guitar node over T1S via `T1SDetector_SetCommand()` ([t1s_detector.c](../../../firmware/fretboard/t1s_detector.c)) from the main loop; the fretboard no longer drives controller GPIOs locally. The guitar node applies the byte — bits 0..4 fret GPIOs, bit 5 strum-down, bit 6 left 0 — to its open-drain outputs.

> Caveat: the strum monostable lives inside `run()`, so its ticks are inference iterations, not 240 Hz ticks — pulse *width* ≈ `hold × inference_period`. Onset (what the game catches) is unaffected. Move it to the ISR if exact width ever matters.

## 4. Enable switch

**SW0 (PB03, active-low momentary)** arms guitar actuation at runtime; **LED0 (PB02)** lit while armed; boots disarmed (command 0 = released) ([main.c](../../../firmware/fretboard/main.c)). While disarmed the gated command is 0 (all released) but sampling and the data stream keep running, so re-arming is seamless. There is no build-time mode gate — the inference module is selected by `MODEL_INFER_STREAMING` (§2); SW0-arm is the only runtime control. Eventual plan: marvin commands the difficulty/enable over the link (§ runtime weight swap).

## 5. Inference budget

24 MHz × 4.17 ms ≈ 100 k cycles per tick. The deployed 16-channel model costs ~3 k MACs/tick streaming (the recompute path ~16 k at window 85) — both well inside budget; the dense `count_macs` figure (~252 k) is a loose upper bound that assumes every window position is computed (see [model.md §5](model.md)). Measured rates: streaming locks to 240 Hz; recompute ~93 Hz at 16 ch.

## 6. Memory budget (8 KB SRAM)

**Streaming (deployed: `(1,4,16)`, channels 16, window 85)** — `model_infer_stream.c` static scratch:

- `s_qin` ring: 5 × 5 = 25 B.
- L0 ring: 17 × 16 = 272 B.
- L1 ring: 65 × 16 = 1040 B.
- ≈ **1.4 KB total** (rings + monostable state). No full-window buffer — the streaming cascade keeps only each layer's deepest-tap history.

**Recompute (`model_infer.c`, channels 16, window 85)** static scratch, for contrast:

- Input ring: 85 × 5 × 2 B = 850 B.
- Standardised int8 input `s_qin`: 85 × 5 = 425 B.
- Per-layer int8 activations `s_act`: 3 × 85 × 16 ≈ 4.1 KB.
- Needed-position table `s_need`: 3 × 85 = 255 B.
- ≈ **5.6 KB total** (the `s_act` window×channels term dominates — this is what makes 32 ch RAM-bound).

Weights are flash-`const` (0 RAM) in both. A **RAM-resident programmable weight set** (loading weights over serial without reflashing) would add ≈ **1.1 KB** (int8 conv weights + biases + head + scales); confirm against the linker `.map`. One override slot fits; multiple RAM-resident models would not.

## 7. Runtime weight swap (built) + programmable weights (future)

The active model is a `model_def_t` pointer ([model_infer.h](../../../firmware/fretboard/model_infer.h)) — a struct of pointers to the int8 weights/scales/thresholds. `model_infer_set_model()` swaps it at runtime. The quantiser emits one named `model_<name>` per `--name`; several link together (an aggregator `model_weights.h` that `#include`s the per-difficulty headers), arch dims are shared `#define`s guarded by a `_Static_assert`, and `MODEL_DEFAULT` = the first one.

**Per-difficulty models — the selection path is now built (2026-08-02).** The fretboard carries **5 selectable slots**: one per Guitar Hero difficulty (`easy`/`medium`/`hard`/`expert`) plus a reserved `auto` slot for a future adaptive policy. Selection is runtime, over the node's local `model` CLI command **and** T1S (marvin's `fretboard model <difficulty>`, detector control opcode `0x03`); `main.c` applies the selected model via `model_infer_set_sel()`. A firmware-owned (not generated) [`models.h`](../../../firmware/fretboard/models.h) holds the difficulty→`model_def_t*` registry and the `auto` resolve hook.

Only `hard` is trained today; the untrained slots **alias to `&model_hard`** (`#ifdef MODEL_HAVE_*`) so all five are selectable and behave identically. **To add a trained difficulty** (e.g. `easy`) once its corpus is captured and a model is distilled:

1. Generate a prefixed header: `edge-ai quantize --name easy --out firmware/fretboard/model_weights_easy.h` (the `--name` prefixes every array `M_EASY_*`, names the struct `model_easy`, and re-uses the shared `#ifndef MODEL_ARCH_DIMS` dims + `#ifndef MODEL_DEFAULT` guard — first-included wins, so `model_weights.h`/`hard` stays the boot default).
2. In [`models.h`](../../../firmware/fretboard/models.h): `#include "model_weights_easy.h"` and `#define MODEL_HAVE_EASY`. The `MODEL_BY_DIFFICULTY[]` / `MODEL_DIFFICULTY_TRAINED[]` tables already switch on `MODEL_HAVE_EASY` — no other edit.
3. Register `model_weights_easy.h` in the mplab fileSet (`.vscode/fretboard.mplab.json`).

No quantiser change is needed for this — the exporter already emits correctly-prefixed multi-model headers. A convenience `--difficulty` flag is optional future polish. The `auto` slot's adaptive logic (currently a documented stub resolving to hard) is a separate future piece; its single hook is `model_resolve()` in `models.h`.

**Programmable weights over serial (future, fits RAM):** because the active model is just a pointer, a RAM-resident `model_def_t` whose arrays point into a received buffer is a drop-in. Needs: a binary blob emitter in the quantiser (`--out-bin`), a framed/checksummed serial receive on the fretboard that validates magic/version/arch and copies into a RAM store, then `set_model(&ram_model)`. Flash `MODEL_DEFAULT` stays the fallback. Lets training iterations be loaded without reflashing.
