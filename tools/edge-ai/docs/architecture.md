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
                                                        host training (PyTorch)
                                                                     │
                                                                     ▼
                                                          int8 quantised model
                                                            (model_weights.h)
```

Both record streams already flow through perf-log keyed by `frame_epoch` + `ts_counter`. The exporter joins them offline. See [training.md](training.md) for the exporter change and CSV schema.

## 2. Inference-time data flow (on-device, as built)

```
fretboard TC0 ISR @ 240 Hz (short & bounded):
    fret_scan_all()                  ──► model_infer_stream_step(scan) ──► cmd
                                            │  (streaming: ~1 conv col/layer/tick,
                                            │   no full-window buffer)
                                            ▼
    cmd_receive_apply_mask(enabled ? cmd : 0)  (open-drain fret GPIOs + strum)
    data_stream_send()                          (applied_mask telemetry)
```

The marvin command stream (`cmd_receive`) is bypassed when fretboard is in `MODEL_DRIVEN` mode; the model owns the wire. The deployed path is **streaming** inference (locks to 240 Hz, runs inline); a **recompute** fallback runs in the main loop for non-RF-width models. SW0 toggles model control at runtime. See [runtime.md](runtime.md) for the full integration shape and the two inference modules.

## 3. Inference cadence

One inference per 240 Hz tick (every 4.17 ms). Budget at 24 MHz with no FPU is ≈100 k cycles. The deployed 16-channel model costs ~3 k MACs/tick streaming (~16 k recompute) — comfortably inside budget; the dense `count_macs` figure (~252 k) is a loose upper bound (see [model.md §5](model.md)).

## 4. Why this shape addresses both concerns

- **Spatial mismatch dissolved.** No CV-row → photo-row alignment needed at training time. The causal window (RF 85 ≈ 354 ms) captures whatever upstream photoxistor signal preceded the strum, regardless of physical sensor placement on the screen.
- **Strum timing learned.** The strum bit is part of the output. Weighted BCE (the strum bit is sparse — see [review.md §risks](review.md#risks)) lets the model learn the photo→strum delay implicitly from the data — provided the receptive field reaches back far enough to see the cue ([model.md §4](model.md)).
- **Edge AI is end-to-end standalone.** Once deployed, fretboard runs the model on its own ADC stream; marvin can be off and gameplay still works. Marvin's role becomes "training-data source + reference detector for diagnostics + game-state controller".
