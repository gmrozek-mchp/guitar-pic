# beatbox signal chain — audio in to beat events

How beatbox turns a line-level audio feed into discrete beat events, and how the
pieces cooperate on a bare-metal (no-RTOS) dsPIC33A. This is the durable
"how it works" reference; decisions and history live in
[`journal.md`](journal.md), the node's role in the system in [`../SPEC.md`](../SPEC.md).

## 1. The pipeline

```
  line-in (L/R)
      │  ADC4, 48 kHz, 256x oversampled, PG1-triggered
      ▼
  ┌─────────────┐   post-HPF ±1 float, per sample (ISR)
  │   audio.c   │──────────────┐
  │  ISR + HPF  │              │  Audio_SampleCallbackRegister
  │  + PWM out  │              ▼
  └─────────────┘        ┌──────────────┐  BeatDetect_Process(mono)
        │ PWM DACs        │ beat_engine  │  (sums L+R → mono)
        ▼ (monitor)       │  on_samples  │
   RB8=L, RB9=R           └──────┬───────┘
                                 ▼
                          ┌──────────────┐  downsample ÷4, 512-pt FFT,
                          │ beat_detect  │  flux / envelope / kick features
                          │  (features)  │
                          └──────┬───────┘
                                 ▼  getters (envelope, flux, kick, …)
                          ┌──────────────┐  running-avg onset detection
                          │ beat_engine  │  → BeatFrame (bass/full/kick beats)
                          │  (decision)  │
                          └──────┬───────┘
                                 ▼  Beat_GetFrame / Beat_HasFrame
                   ┌─────────────┼───────────────┐
                   ▼             ▼               ▼
             RGB indicator   `beat` CLI     T1S publish (B4, planned)
             (main.c)        (cli.c)        → lemmy / lightshow
```

The dependency is strictly layered and additive:

```
audio (I/O) → beat_detect (features) → beat_engine (decision) → tempo/phase (later)
```

Each layer only knows the one below it. `audio.c` knows nothing about beats;
`beat_detect.c` knows nothing about the decision thresholds; `beat_engine.c`
knows nothing about who consumes the `BeatFrame`. Tempo/BPM and phase (deferred)
slot in as **consumers of the beat events**, not edits to the layers below.

## 2. Cooperation model (no RTOS)

beatbox runs a bare-metal **super-loop**, not an RTOS. There are exactly two
execution contexts, and understanding their boundary is the key to the whole
design:

| Context | What runs there | Cadence | Constraint |
|---|---|---|---|
| **ADC ISR** (`audio.c`, priority 6) | read ADC, DC-block HPF, write PWM DACs, sample callback → `BeatDetect_Process` | 48 kHz (every ~20.8 µs) | must be *short* — only per-sample bookkeeping, never the FFT |
| **main loop** (`main.c`) | `CLI_Tasks()`, `Beat_Tasks()`, `T1SFollower_Tasks()` | as fast as the loop turns | cooperative — no task may block long |

**The ISR does the cheap per-sample work; the main loop does the heavy work.**
`BeatDetect_Process` (ISR) only accumulates the downsampler and buffers one FFT
input sample. The FFT itself — hundreds of `sqrtf`/butterfly operations — runs in
`BeatDetect_RunFFT`, called from the main loop. The two are decoupled by a single
flag.

### Handshakes

Producer/consumer coupling across the ISR/main-loop boundary (and between
main-loop stages) is done with `volatile bool` flags, several of them
**consume-once** (read-and-clear):

| Flag | Set by | Cleared by | Meaning |
|---|---|---|---|
| `fft_ready` | ISR (`BeatDetect_Process`, on buffer full) | `BeatDetect_RunFFT` | a 512-sample buffer is ready to transform |
| `frame_ready` | `BeatDetect_RunFFT` | `BeatDetect_HasFrame()` | new features are available this frame |
| `spectrum_ready` | `BeatDetect_RunFFT` (every 3rd frame) | `BeatDetect_HasSpectrumFrame()` | new 64-bin spectrum available |
| `s_frame_ready` | `beat_engine` (`Beat_Tasks`) | `Beat_HasFrame()` | a new `BeatFrame` was published |

### Why no locks are needed

- **Only the ISR preempts.** The three main-loop tasks never preempt each other,
  so state shared *only* among them needs no guarding. Guarding is required only
  for state shared with the ISR.
- **Single-word reads are atomic** w.r.t. the ISR on this 32-bit core. The feature
  outputs (`out_flux`, `out_kick_beat`, …) are `volatile` scalars of ≤32 bits; the
  main loop reads a coherent value with no torn read. The sample-callback function
  pointer is likewise a single aligned word, so registration can't tear against an
  ISR dispatch.
- **Multi-word values do need care** — e.g. the 32-bit millisecond tick in
  `t1s_follower.c` uses a double-read-until-stable guard, because a 32-bit read is
  two instructions and the ISR can land between them. No such value crosses the
  ISR boundary in the beat path.

### The one real constraint: main-loop latency

`fft_input` is **single-buffered**. The ISR keeps filling it for the next frame
while `BeatDetect_RunFFT` is copying it out (windowing into `fft_real`). This is
safe only because the main loop revisits `Beat_Tasks()` fast enough that the copy
(512 floats, microseconds) finishes long before the ISR has overwritten more than
the first couple of bins — the ISR produces one new downsampled sample only every
~83 µs (4 × 20.8 µs). Keep the other main-loop tasks non-blocking and this holds
with wide margin. If the loop ever stalls for tens of milliseconds, the frame
being transformed can tear. (Double-buffering would remove the constraint; it
wasn't needed for the proven behavior.)

## 3. Audio front-end (`audio.c`)

- **Input:** ADC4 samples line-in at 48 kHz, 256× oversampled to 16-bit, triggered
  by PWM generator PG1. CH0 = left (`AD4AN0`), CH1 = right (`AD4AN1`); the CH1
  completion interrupt (priority 6) is the 48 kHz heartbeat of the whole node.
- **DC-block HPF:** a first-order high-pass (~20 Hz corner) removes the ADC's DC
  bias so the signal is centered: `y = a·(y_prev + x − x_prev)`,
  `a = fs/(fs + 2π·fc)`.
- **Output (monitor only):** each HPF'd pair is written straight back to the PWM_HS
  audio DACs — PWM1H/RB8 = left, PWM2H/RB9 = right — as a pass-through. **This path
  is scratch/monitor quality, not a clean speaker feed**: background hiss lives in
  both the ADC front-end and the PWM output stage (see the journal). It's useful
  for bench monitoring, not for driving the main PA.
- **Fan-out:** after the passthrough writes, the ISR hands the post-HPF, normalized
  (±1 float) L/R pair to a single registered consumer via
  `Audio_SampleCallbackRegister(void (*)(float left, float right))`. This is the
  seam that decouples audio from analysis — the same idiom MCC uses to expose the
  ADC event. The callback stays **stereo**; the decision to collapse to mono is the
  beat detector's, not the audio layer's.

## 4. Feature extraction (`beat_detect.c`)

`beat_engine`'s `on_samples` sums L+R to mono and feeds it to
`BeatDetect_Process`. From there:

### Framing

- **Downsample ÷4** by box-averaging 4 consecutive samples (48 kHz → 12 kHz). The
  averaging is a crude anti-alias low-pass; it drops the analysis Nyquist to 6 kHz,
  which is all beat detection needs.
- Buffer 512 downsampled samples → one FFT frame. Frame rate =
  12000 / 512 ≈ **23.4 Hz** (frame period ~42.7 ms).
- **Envelope** is tracked in parallel as the peak |sample| over the *full-rate*
  frame (reset each frame).

### FFT

- Radix-2 decimation-in-time, 512-point, in-place. Twiddle factors and a **Hanning
  window** are precomputed once in `BeatDetect_Init`. Input is real (imag = 0);
  only the lower half (256 bins) is used (the spectrum is symmetric).
- **Bin resolution** = 12000 / 512 = **23.4375 Hz/bin**; bin *k* ≈ 23.4·*k* Hz.

| Feature | Bins | Frequency | Notes |
|---|---|---|---|
| Bass band | 1–10 | ~23–234 Hz | kick/bass region |
| Kick sub-band | 2–4 | ~47–94 Hz | dedicated kick detector |
| Mid/high band | 11–255 | ~258–5977 Hz | "full" flux |
| Display spectrum | 1–64 | ~23–1500 Hz | 64 bins, every 3rd frame (~8 Hz) |

### Per-frame features

- **Spectral flux** = sum of *positive* bin-to-bin magnitude increases (onset
  energy). Split into **bass flux** (bins 1–10) and **mid/high flux** (bins 11–255).
  Each is **auto-ranged** to 0–1000 against a slowly-decaying running max (bass/mid
  max decay ×0.999/frame), so absolute FFT scale doesn't matter.
- **Envelope** exported two ways: `raw_env` (absolute, 0–10000, 1.0 full-scale =
  10000 — used as the noise reference) and an auto-ranged 0–1000 value (max decays
  ×0.995/frame, gated to 0 below a floor so the range can't creep into noise).
- **Bass dominance:** smoothed ratio of bass energy to total energy; `bass_dominant`
  when it exceeds 35%. Distinguishes bass-driven from treble-driven material.
- **Kick detector:** energy in bins 2–4 through a fast-attack / slow-release
  envelope (`KICK_ATTACK`/`KICK_RELEASE`) compared against a slow adaptive average
  (`KICK_AVG_ALPHA`). A kick fires when the envelope exceeds `KICK_THRESH_MULT`×
  average, subject to `KICK_MIN_INTERVAL` (≈256 ms → caps at ~234 kicks/min) and an
  absolute-energy floor so the noise floor can't trigger it. Reports strength 0–1000
  proportional to how far above threshold, and 2 ("strong") past double threshold.

### Tuning constants

| Constant | Value | Meaning |
|---|---|---|
| `DOWNSAMPLE_FACTOR` | 4 | 48 kHz → 12 kHz |
| `FFT_SIZE` | 512 | → ~23.4 Hz frames, 23.4 Hz/bin |
| `BASS_BIN_START/END` | 1 / 10 | bass flux band |
| `BASS_RATIO_THRESH` | 0.35 | bass "dominant" threshold |
| `KICK_BIN_START/END` | 2 / 4 | kick sub-band |
| `KICK_ATTACK / RELEASE` | 0.7 / 0.04 | kick energy-envelope attack/release |
| `KICK_AVG_ALPHA` | 0.015 | adaptive-threshold tracking rate |
| `KICK_THRESH_MULT` | 2.0 | kick fires above 2× average |
| `KICK_MIN_INTERVAL` | 6 frames | ~256 ms kick refractory |

Concurrency note: `BeatDetect_Process` runs in ISR context; everything else in
`beat_detect.c` runs from the main loop. Outputs are `volatile` and read via the
getters.

## 5. Beat decision (`beat_engine.c`)

`Beat_Tasks()` runs the pending FFT, and on each new frame turns the raw features
into discrete beat events, publishing a `BeatFrame`. Two independent onset
detectors — one on **bass flux**, one on **mid/high flux** — share the same logic
(`detect_band`):

1. Keep a `BEAT_HIST_LEN` (4) history of recent flux; compute the running average
   (excluding the current sample).
2. `delta = flux − avg` (clamped ≥0).
3. A beat fires when `delta ≥ BEAT_DELTA_THR` (250), subject to a
   `BEAT_COOLDOWN` (8 frames ≈ 340 ms) refractory. Strong (2) when `delta` exceeds
   3× the threshold.

A `raw_env` noise gate (`NOISE_GATE_THR`) is meant to suppress beats on near-silence
unless the flux delta is strong on its own (`NOISE_GATE_BYPASS_DELTA`). **Note:** as
currently tuned, `NOISE_GATE_BYPASS_DELTA` (250) equals `BEAT_DELTA_THR` (250), so
any delta large enough to fire is already large enough to bypass the gate — the
envelope gate is effectively inert, and the flux-delta threshold alone gates. Lower
`NOISE_GATE_BYPASS_DELTA` (or raise `BEAT_DELTA_THR`) to make the envelope gate
active. (Ported verbatim from the original; documented here rather than silently
"fixed".)

The kick beat/strength and the bass-dominance flag pass straight through from
`beat_detect`.

## 6. Published interface — `BeatFrame`

`beat_engine` publishes one `BeatFrame` per frame (~23.4 Hz).
`Beat_HasFrame()` returns true once per new frame (consume-once); `Beat_GetFrame()`
snapshots the latest regardless. Today's consumers are the RGB indicator in
`main.c` and the `beat` CLI command.

| Field | Type | Range | Meaning |
|---|---|---|---|
| `bass_beat` | `uint8_t` | 0/1/2 | bass-band onset (2 = strong) |
| `full_beat` | `uint8_t` | 0/1/2 | mid/high-band onset (2 = strong) |
| `kick_beat` | `uint8_t` | 0/1/2 | kick-drum hit (2 = strong) |
| `kick_strength` | `uint16_t` | 0–1000 | kick intensity above threshold |
| `flux_bass` | `uint16_t` | 0–1000 | bass-band spectral flux (auto-ranged) |
| `flux_full` | `uint16_t` | 0–1000 | mid/high-band spectral flux (auto-ranged) |
| `raw_env` | `uint16_t` | 0–10000 | absolute amplitude envelope |
| `bass_dominant` | `bool` | — | bass energy > 35% of total |

### Planned bus mapping (B4, not yet implemented)

The `BeatFrame` is the internal contract; the on-bus contract is downstream of it:

- **→ lightshow (id 7):** a compact ~5-parameter **beat frame** derived from these
  fields (e.g. beat pulse + band energy + big-beat flag), sent over T1S.
- **→ lemmy (id 6):** **position commands** for the puppet, computed by beatbox's
  choreography layer (the old `nod_engine`, not yet ported).

Both require the **tempo/phase** layer, which is a downstream consumer of these beat
events (see §1). See [`../SPEC.md`](../SPEC.md) §1 and the journal for the bus data
model.
