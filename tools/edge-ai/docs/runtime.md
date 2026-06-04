# runtime

Eventual on-device deployment shape. Phase 5 in [rollout.md](rollout.md) is the first time anything in this doc actually lands in firmware — phases 1–4 are entirely offline.

## 1. Intended target

PIC32CM6408PL10048, Cortex-M0+ @ 24 MHz, no FPU, **64 KB Flash / 8 KB SRAM** (datasheet DS40002667). The current fretboard MCU. See [SPEC.md §3](SPEC.md#3-decisions-locked) on why this is a soft target rather than a locked one — the offline phases are MCU-agnostic, and if the trained model exceeds the budget the target gets revisited before phase 5 commits.

## 2. Module (as built)

[`model_infer.c`](../../../firmware/fretboard/model_infer.c) / [`.h`](../../../firmware/fretboard/model_infer.h):

- Owns the 60-sample × 5-channel input ring.
- `model_infer_push(const uint16_t adc[5])` — called from the TC0 ISR each tick.
- `model_infer_run()` — integer-only int8 inference (last-timestep-only conv + integer threshold + strum monostable) returning the `uint8_t` command byte. **Runs in the main loop, not the ISR** (see §3).
- `model_infer_set_model(const model_def_t *m)` — runtime weight swap (per-difficulty); weights/scales live in the generated `model_weights.h`.

## 3. Inference must run OUTSIDE the TC0 ISR — proven on hardware (2026-06-04)

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

24 MHz × 4.17 ms ≈ 100 k cycles per tick. A model with ≲1 k MACs at int8 fits comfortably with margin for ring-buffer copy + feature extraction. Phase 3 of [rollout.md](rollout.md) measures actual latency; phase 5 cannot proceed if measured latency consumes more than half the tick.

## 6. Memory budget (8 KB SRAM)

As built (RF85 `(1,4,16)`, channels 8, window 60), `model_infer` static scratch:

- Input ring: 60 × 5 × 2 B = 600 B.
- Standardised int8 input `s_qin`: 60 × 5 = 300 B.
- Per-layer int8 activations `s_act`: 3 × 60 × 8 = 1440 B.
- Needed-position table `s_need`: 3 × 60 = 180 B.
- ≈ **2.5 KB total**. The weights are flash-`const` (0 RAM).

That leaves ample room in 8 KB for stack + SERCOM buffers. A **RAM-resident programmable weight set** (for loading weights over serial without reflashing) would add ≈ **1.1 KB** (840 B int8 conv weights + biases + head + scales) → ~3.6 KB, still comfortable; confirm against the linker `.map`. Multiple RAM-resident models would not fit — one override slot does.

## 7. Runtime weight swap (built) + programmable weights (future)

The active model is a `model_def_t` pointer ([model_infer.h](../../../firmware/fretboard/model_infer.h)) — a struct of pointers to the int8 weights/scales/thresholds. `model_infer_set_model()` swaps it at runtime. The quantiser emits one named `model_<name>` per `--name`; several link together (an aggregator `model_weights.h` that `#include`s the per-difficulty headers), arch dims are shared `#define`s guarded by a `_Static_assert`, and `MODEL_DEFAULT` = the first one. **Per-difficulty models**: train one per difficulty, generate a header each, switch with `set_model()` — selection signal (serial command from marvin / button / scroll-speed sensor) is the open piece.

**Programmable weights over serial (future, fits RAM):** because the active model is just a pointer, a RAM-resident `model_def_t` whose arrays point into a received buffer is a drop-in. Needs: a binary blob emitter in the quantiser (`--out-bin`), a framed/checksummed serial receive on the fretboard that validates magic/version/arch and copies into a RAM store, then `set_model(&ram_model)`. Flash `MODEL_DEFAULT` stays the fallback. Lets training iterations be loaded without reflashing.
