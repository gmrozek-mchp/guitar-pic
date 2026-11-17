# runtime

On-device deployment of the int8 model on the fretboard node, as built. Firmware lives in
[`firmware/fretboard/`](../../../firmware/fretboard/) (spec: [`SPEC.md`](../../../firmware/fretboard/SPEC.md)).

## 1. Target

PIC32CM6408PL10048, Cortex-M0+ @ 24 MHz, no FPU, **64 KB Flash / 8 KB SRAM**. The deployed
16-channel model fits with large margin (§4, §5).

## 2. Two inference modules

Both are integer-only, bit-exact with the host reference `edge_ai.quantize.int8_sim_*`, share the
`model_def_t` weight struct from the generated header, and apply the same strum monostable. One is
selected at build time by `MODEL_INFER_STREAMING` in
[`fretboard_config.h`](../../../firmware/fretboard/fretboard_config.h) (default 1); the inactive
translation unit compiles empty, so both stay in the MPLAB project.

- **Streaming — [`model_infer_stream.c`](../../../firmware/fretboard/model_infer_stream.c) (default).**
  A continuous causal convolution: one new conv column per layer per tick, prior columns cached in
  per-layer rings (depths 5 / 17 / 65). ~3 k MACs/tick at 16 ch; locks to 240 Hz. Only bit-exact
  with a model **trained at `window ≥ RF` (= 85)** — guarded by `_Static_assert(MODEL_WINDOW >= 85)`;
  the cascade is hard-coded for the 3-layer / kernel-5 / 5-in / 6-out arch (`#error` on mismatch).
- **Recompute — [`model_infer.c`](../../../firmware/fretboard/model_infer.c).** Keeps a
  `MODEL_WINDOW × 5` input ring and re-runs only the conv positions the last timestep needs
  (21 / 5 / 1 per layer at window 85). Works at any trained window — the general fallback — but
  costs ~16 k MACs/tick (~93 Hz measured at 16 ch).

## 3. Wiring (`main.c`)

```c
TC0 ISR (240 Hz, short & bounded)
    fret_scan_all();  push the scan onto the SPSC queue s_adc_q
    cmd = actuation_armed() ? s_latest_cmd : 0;
    data_stream_send(cmd, T1SDetector_TeacherMask());   // ADC + applied + commanded masks → marvin

main loop
    apply a pending model selection (model_infer_set_sel)
    drain s_adc_q in order: s_latest_cmd = model_infer_stream_step(scan)
    T1SDetector_SetCommand(cmd, armed);   // → fauxmote (drives the Wii) + guitar node, over T1S
```

- **Inference runs in the main loop, never the ISR.** Running it in the TC0 callback stretches the
  tick (sampling drops below 240 Hz, so the learned lag is wrong) and starves the link (journal
  2026-06-04). Build at `-O2`/`-O3`; `-O0` overruns.
- Inference is skipped during a capture (disarmed + streaming) and kept warm otherwise.
- **Arming:** the node's `arm on|off` CLI or marvin's control channel (`0x88B9` opcode `0x01`), last
  writer wins; boots disarmed. Disarming sends one all-released frame, then the node stays silent on
  the command channels. LED0 is the T1S liveness heartbeat, not an arm indicator.
- **Data stream:** boots off; enabled by marvin's `fretboard stream on` or the local `stream` CLI.
  `sample_seq` advances while off.
- The strum monostable counts inference iterations, not 240 Hz ticks; with streaming they are the same.

## 4. Inference budget

24 MHz × 4.17 ms ≈ 100 k cycles per tick. Streaming ~3 k MACs/tick, recompute ~16 k. `count_macs`'s
dense figure (~252 k at 16 ch / window 85) is a loose upper bound ([model.md §5](model.md)).

## 5. Memory (8 KB SRAM)

| | Streaming (deployed) | Recompute |
|---|---|---|
| Static scratch, 16 ch / window 85 | ≈ 1.4 KB (input ring 25 B, L0 ring 272 B, L1 ring 1040 B) | ≈ 5.6 KB (input ring 850 B, `s_qin` 425 B, `s_act` ≈ 4.1 KB, `s_need` 255 B) |

Weights are flash-`const` (0 RAM). A RAM-resident weight set loaded over the link would add ≈ 1.1 KB
— one slot fits.

## 6. Model selection (per difficulty)

The active model is a `model_def_t *`; `model_infer_set_sel()` swaps it at runtime, keeping the
input ring (no re-warm). [`models.h`](../../../firmware/fretboard/models.h) (hand-written) maps five
slots — `easy` / `medium` / `hard` / `expert` / `auto` — to models. Only `hard` is trained
(`model_weights.h`, `model_hard`); untrained slots alias to it, and `auto` resolves to hard via the
`model_resolve()` stub.

Selection sources: the node's `model` CLI, and marvin over T1S (`0x88B9` opcode `0x03`). marvin
pushes the committed song-select difficulty at every run start, so a typed `fretboard model <x>` is
overridden by the next `play`. `auto` is console-only.

**Adding a trained difficulty** (e.g. `easy`):

1. `uv run edge-ai quantize --name easy --out ../../firmware/fretboard/model_weights_easy.h ...`
   (`--name` prefixes every symbol and names the struct `model_easy`; arch dims and
   `MODEL_DEFAULT` are `#ifndef`-guarded, so `hard` stays the boot default).
2. In `models.h`: `#include "model_weights_easy.h"` and `#define MODEL_HAVE_EASY`.
3. Add `model_weights_easy.h` to the MPLAB fileSet (`.vscode/fretboard.mplab.json`).

**Programmable weights (not built):** a RAM `model_def_t` pointing into a received, validated
blob would let iterations load without reflashing; needs a `--out-bin` emitter and a framed receive.
