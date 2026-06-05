# runtime

On-device deployment shape. **As built and running standalone on the fretboard MCU (2026-06-04).** The int8 model reads its own photosensors and drives the controller with marvin disconnected.

## 1. Intended target

PIC32CM6408PL10048, Cortex-M0+ @ 24 MHz, no FPU, **64 KB Flash / 8 KB SRAM** (datasheet DS40002667). The current fretboard MCU. See [SPEC.md §3](SPEC.md#3-decisions-locked) on why this is a soft target rather than a locked one — the offline phases are MCU-agnostic. The deployed 16-channel int8 model fits with large margin (§5, §6).

## 2. Two inference modules (as built)

There are **two** int8 inference implementations of the same model; one is selected at build time by `MODEL_INFER_STREAMING` in [`fretboard_config.h`](../../../firmware/fretboard/fretboard_config.h). Both are integer-only (no FPU), bit-exact with the host reference `edge_ai.quantize.int8_sim`, share the `model_def_t` weight struct from the generated `model_weights.h`, and apply the same strum monostable.

**Streaming — [`model_infer_stream.c`](../../../firmware/fretboard/model_infer_stream.c) (default, `MODEL_INFER_STREAMING=1`).** Computes a *continuous* causal convolution: one new conv column per layer per tick, caching prior columns in small per-layer rings (depths 5 / 17 / 65, sized to each layer's deepest tap). ~3 k MACs/tick at 16 ch — locks comfortably to 240 Hz. The catch: a continuous conv is only bit-exact with a model **trained at `window ≥ receptive field` (= 85 for `(1,4,16)`)** — at a shorter window the windowed model's moving zero-pad boundary wouldn't match. A `_Static_assert(MODEL_WINDOW >= 85)` guards this. The cascade is hard-coded for the 3-layer / kernel-5 / 5-in / 6-out arch (an `#error` guards a mismatched regeneration). `model_infer_stream_step(adc[5])` is called once per scan, in order.

**Recompute — [`model_infer.c`](../../../firmware/fretboard/model_infer.c) (`MODEL_INFER_STREAMING=0`).** Owns a `MODEL_WINDOW × 5` input ring; each tick re-runs only the conv positions the last timestep transitively needs (a back-propagated `s_need` table — 21 / 5 / 1 positions per layer at window 85), skipping the dense per-position conv. Works at **any** trained window, so it's the general-window fallback, but it re-runs the receptive field each tick (~16 k MACs at 16 ch / window 85, measured ~93 Hz) so it **must run in the main loop, not the ISR** (§3). `model_infer_push(adc[5])` from the ISR, `model_infer_run()` from the main loop.

Both modules guard their entire body on `MODEL_INFER_STREAMING`, so the inactive translation unit is empty and allocates no static buffers — both can stay in the MPLAB project; flip the mode in `fretboard_config.h` (or with `-D`) with no add/remove of source files. `model_infer_set_model(const model_def_t *m)` (each module) does the runtime weight swap (per-difficulty).

## 3. The recompute path must run OUTSIDE the TC0 ISR — proven on hardware (2026-06-04)

> Applies to the **recompute** path (`model_infer.c`). The streaming path is cheap enough (~3 k MACs/tick) to run inline; this section is why the recompute path can't.


The first bring-up ran `model_infer_run()` *inside* the 240 Hz TC0 callback. That fails two ways at once: the long ISR delays the next tick (sampling drops to ~150–190 Hz, so the model's window is no longer 250 ms and its learned photo→strum lag is wrong → late/missed notes), **and** it starves the interrupt-driven SERCOM TX, collapsing the data stream (recv ~22 fps with heavy drops). Measured with [`tools/marvin-perf/fretboard_rate.py`](../../marvin-perf/fretboard_rate.py), which reads `sample_seq` to report the true tick rate.

**Final shape — decouple sampling from inference:**

```c
TC0 ISR (240 Hz, short & bounded)
    fret_scan_all();
    model_infer_push(scan);                 // sample the window at a clean 240 Hz
    enabled = poll_sw0_debounced();         // SW0 toggles model control; LED0 shows it
    cmd_receive_apply_mask(enabled ? s_latest_cmd : 0u);
    data_stream_send();                     // applied_mask carries the model's command

main loop (best-effort)
    s_latest_cmd = model_infer_run();       // heavy inference, preempted by the ISR
```

`s_latest_cmd` is a single `volatile uint8_t` (atomic on the M0+). `model_infer_run()` snapshots the ring cursor on entry so a concurrent `push()` can't shift the window mid-inference. Result: ISR stays short → 240 Hz sampling + serial both clean (measured 240/240, ~0 drops); command latency ≈ one inference period (~5 ms) + ≤1 tick. Build at **`-O2`/`-O3`** — `-O0` alone overruns the tick (the int8 conv + requant is heavy without optimisation).

The command byte goes through `cmd_receive_apply_mask()` ([cmd_receive.c](../../../firmware/fretboard/cmd_receive.c)) — bits 0..4 fret GPIOs, bit 5 strum-down, bit 6 left 0.

> Caveat: the strum monostable lives inside `run()`, so its ticks are inference iterations, not 240 Hz ticks — pulse *width* ≈ `hold × inference_period`. Onset (what the game catches) is unaffected. Move it to the ISR if exact width ever matters.

## 4. Operating mode + enable switch

Build-time `FRETBOARD_MODE` (`MODEL_DRIVEN` default, `MARVIN_DRIVEN` to restore the marvin-driven path) in [main.c](../../../firmware/fretboard/main.c). **SW0 (PB03, active-low momentary)** toggles model control at runtime; **LED0 (PB02)** lit while enabled; boots disabled (outputs released). When disabled the ISR applies mask 0 (all released) but keeps sampling, so re-enable is seamless. Eventual plan: marvin commands the difficulty/enable over serial (§ runtime weight swap).

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

The active model is a `model_def_t` pointer ([model_infer.h](../../../firmware/fretboard/model_infer.h)) — a struct of pointers to the int8 weights/scales/thresholds. `model_infer_set_model()` swaps it at runtime. The quantiser emits one named `model_<name>` per `--name`; several link together (an aggregator `model_weights.h` that `#include`s the per-difficulty headers), arch dims are shared `#define`s guarded by a `_Static_assert`, and `MODEL_DEFAULT` = the first one. **Per-difficulty models**: train one per difficulty, generate a header each, switch with `set_model()` — selection signal (serial command from marvin / button / scroll-speed sensor) is the open piece.

**Programmable weights over serial (future, fits RAM):** because the active model is just a pointer, a RAM-resident `model_def_t` whose arrays point into a received buffer is a drop-in. Needs: a binary blob emitter in the quantiser (`--out-bin`), a framed/checksummed serial receive on the fretboard that validates magic/version/arch and copies into a RAM store, then `set_model(&ram_model)`. Flash `MODEL_DEFAULT` stays the fallback. Lets training iterations be loaded without reflashing.
