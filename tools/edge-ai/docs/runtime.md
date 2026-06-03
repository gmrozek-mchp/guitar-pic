# runtime

Eventual on-device deployment shape. Phase 5 in [rollout.md](rollout.md) is the first time anything in this doc actually lands in firmware — phases 1–4 are entirely offline.

## 1. Intended target

PIC32CM6408PL10048, Cortex-M0+ @ 24 MHz, no FPU, ~32 KB RAM. The current fretboard MCU. See [SPEC.md §3](SPEC.md#3-decisions-locked) on why this is a soft target rather than a locked one — the offline phases are MCU-agnostic, and if the trained model exceeds the budget the target gets revisited before phase 5 commits.

## 2. New module

Add `model_infer.c` / `model_infer.h` to [firmware/fretboard/](../../../firmware/fretboard/) alongside the existing modules:

- Owns the 60-sample × 5-channel ring buffer.
- `model_infer_push(uint16_t adc[5])` called from the TC0 callback after `fret_scan_all`.
- `model_infer_run()` returns a `uint8_t` command byte.

## 3. TC0 callback shape

```c
TC0 callback (240 Hz)
    fret_scan_all();
    data_stream_send();                    // diagnostic, optional in model mode
    if (mode == MODEL_DRIVEN) {
        model_infer_push(scan_results);
        cmd = model_infer_run();
        button_apply(cmd);
    } else {
        cmd_receive_update();              // marvin-driven (today's path)
    }
```

The model byte goes through the same `button_apply` path as marvin's command byte ([cmd_receive.c](../../../firmware/fretboard/cmd_receive.c)) — bits 0..4 to fret GPIOs, bit 5 to strum-down. Bit 6 (strum-up) is unused and the runtime should leave it at 0.

## 4. Operating-mode toggle

A new fretboard operating mode `MODEL_DRIVEN` joins the existing `MARVIN_DRIVEN` (today's default) and the dormant `STANDALONE` (the orphaned `fret_button.c` fallback documented in [fretboard journal](../../../firmware/fretboard/docs/journal.md), 2026-06-02 entry). Switch lives in `main.c`'s init or a build-time `#define` for v1; runtime toggle deferred to phase 4 of the rollout, where the model runs *alongside* marvin without driving the wire.

## 5. Inference budget

24 MHz × 4.17 ms ≈ 100 k cycles per tick. A model with ≲1 k MACs at int8 fits comfortably with margin for ring-buffer copy + feature extraction. Phase 3 of [rollout.md](rollout.md) measures actual latency; phase 5 cannot proceed if measured latency consumes more than half the tick.

## 6. Memory budget

- Input ring buffer: 60 samples × 5 channels × 2 bytes = 600 bytes.
- Model weights: depends on architecture. SensiML auto-pipeline output and the 1D-CNN fallback both fit in single-digit KB at int8.
- Activation scratch: low hundreds of bytes for the recommended baseline.

Total well under the ~32 KB available. If the architecture chosen during phase 2 blows this budget, the deployment target is reconsidered before phase 5.
