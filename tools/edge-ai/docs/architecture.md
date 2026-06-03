# architecture

How the edge AI fits at training time and at inference time. For decisions and the document map, see [SPEC.md](SPEC.md).

## 1. Training-time data flow (offline)

```
Wii ──► marvin (cv_marvin_v1 + timing_pipeline)
          │   │
          │   ├── PERF_REC_ACTUATOR (intended_mask, strum_dir)   ──┐
          │   └── (commands over USB CDC to fretboard)              │
          │                                                          ▼
          │                                                  marvin-perf
          │                                                   (offline)
          │   ▲                                                      │
          │   │ PERF_REC_FRETBOARD_RAW (5×ADC @ 240 Hz)              │
          │   │                                                      │
          │   └─────────────────────────────────────────────────────►│
          │                                                          │
          │                                                          ▼
          │                                                   SensiML CSV
          │                                            (per-row: 5 ADC + 6 labels)
                                                                     │
                                                                     ▼
                                                        host training (PyTorch /
                                                              MPLAB ML)
                                                                     │
                                                                     ▼
                                                            quantised model
                                                              (int8 / int16)
```

Both record streams already flow through perf-log keyed by `frame_epoch` + `ts_counter`. The exporter joins them offline. See [training.md](training.md) for the exporter change and CSV schema.

## 2. Inference-time data flow (eventual on-device)

```
fretboard TC0 callback @ 240 Hz:
    fret_scan_all()             ──► s_adc_ring[60][5]    (250 ms history)
                                       │
                                       ▼
                                 model_infer(ring) ──► uint8_t cmd_byte
                                       │
                                       ▼
                                 button_apply(cmd_byte)  (open-drain GPIOs)
```

`cmd_receive_update()` is bypassed when fretboard is in model-driven mode; the model owns the wire instead of marvin. See [runtime.md](runtime.md) for the integration shape.

## 3. Inference cadence

One inference per 240 Hz tick (every 4.17 ms), called inline from the existing TC0 callback after `fret_scan_all` and before `button_apply`. Budget per inference at 24 MHz with no FPU is ≈100 k cycles — comfortable for a model with ≲1 k MACs at int8. If host-side training reveals the chosen architecture exceeds this budget, the deployment target gets revisited (see [SPEC.md §3](SPEC.md#3-decisions-locked)) before phase 5 commits.

## 4. Why this shape addresses both concerns

- **Spatial mismatch dissolved.** No CV-row → photo-row alignment needed at training time. The 250 ms causal window captures whatever upstream photoxistor signal preceded the strum, regardless of physical sensor placement on the screen.
- **Strum timing learned.** The strum bit is part of the output. Weighted BCE (the strum bit is sparse — see [review.md §risks](review.md#risks)) lets the model learn the +220 ms photo-to-strum delay implicitly from the data.
- **Edge AI is end-to-end standalone.** Once deployed, fretboard runs the model on its own ADC stream; marvin can be off and gameplay still works. Marvin's role becomes "training-data source + reference detector for diagnostics + game-state controller".
