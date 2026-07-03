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

The **deployed** path is the atomic `actuator-fb` schema: the fretboard frame carries its `applied_mask` + `fb_seq` in the same 240 Hz frame as the ADC scan, so labels pair with features on-device with no cross-stream join. The `frame_epoch` + `ts_counter` cross-stream join shown above is the **legacy `actuator` mode** — the two record streams flow through perf-log and the exporter joins them offline (with an unknown skew). See [training.md](training.md) §2 for both exporter modes and the CSV schema.

## 2. Inference-time data flow (on-device, as built)

```
fretboard TC0 ISR @ 240 Hz (short & bounded):
    fret_scan_all()  ──► push scan onto SPSC queue s_adc_q
    cmd = armed ? s_latest_cmd : 0            (SW0 arms actuation)
    data_stream_send(cmd)                     (ADC + driven applied_mask → marvin)

fretboard main loop:
    drain s_adc_q ──► model_infer_stream_step(scan) ──► s_latest_cmd
                        │  (streaming: ~1 conv col/layer/tick, no full-window buffer)
    T1SDetector_SetCommand(s_current_cmd) ──► guitar node over T1S (open-drain frets + strum)
```

The fretboard no longer drives controller GPIOs locally; the model's command byte is forwarded to the **guitar node over T1S** (`T1SDetector_SetCommand`), which applies it to its open-drain controller outputs. marvin coordinates and logs but is out of the command path. The deployed path is **streaming** inference (locks to 240 Hz); both it and the **recompute** fallback (for non-RF-width models) run in the main loop — the ISR only samples. SW0 arms actuation at runtime. See [runtime.md](runtime.md) for the full integration shape and the two inference modules.

## 3. Inference cadence

One inference per 240 Hz tick (every 4.17 ms). Budget at 24 MHz with no FPU is ≈100 k cycles. The deployed 16-channel model costs ~3 k MACs/tick streaming (~16 k recompute) — comfortably inside budget; the dense `count_macs` figure (~252 k) is a loose upper bound (see [model.md §5](model.md)).

## 4. Why this shape addresses both concerns

- **Spatial mismatch dissolved.** No CV-row → photo-row alignment needed at training time. The causal window (RF 85 ≈ 354 ms) captures whatever upstream photoxistor signal preceded the strum, regardless of physical sensor placement on the screen.
- **Strum timing learned.** The strum bit is part of the output. Weighted BCE (the strum bit is sparse — see [review.md §risks](review.md#risks)) lets the model learn the photo→strum delay implicitly from the data — provided the receptive field reaches back far enough to see the cue ([model.md §4](model.md)).
- **Edge AI is end-to-end standalone.** Once deployed, fretboard runs the model on its own ADC stream; marvin can be off and gameplay still works. Marvin's role becomes "training-data source + reference detector for diagnostics + game-state controller".
