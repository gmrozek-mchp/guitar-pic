# beatbox — Specification

> What beatbox *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: on the bench — beat detection + T1S follower up.** The peripheral config was
> regenerated from scratch on the MPS512 (fresh MCC/Melody baseline). Running today: stereo audio
> in (ADC4, 48 kHz) → 512-pt FFT beat detection → discrete beat events; an onboard RGB beat
> indicator; an audio passthrough to the PWM DACs (monitor quality); a UART2 CLI; and a
> **10BASE-T1S PLCA follower (id 5)** that brings up the LAN8651 MAC-PHY, syncs PLCA, and heartbeats
> presence. Still ahead: **publishing on the bus** (position commands → lemmy, beat frame →
> lightshow — milestone B4) and the **tempo/BPM + phase** layer those need. The imported
> puppet-motion + WS2812 effects remain parked in `config.mcc.bak/`. See §6 and
> [`docs/journal.md`](docs/journal.md); the signal chain is documented in
> [`docs/beat-detection.md`](docs/beat-detection.md).

## 1. Purpose

beatbox is the **beat-source node** on the marvin T1S bus — the system's ear. It listens to a
line-level audio feed, runs real-time FFT beat/tempo detection, and **publishes** the result on the
bus for the animation and lighting nodes to consume:

- **To [`lemmy`](../lemmy/SPEC.md) (animation, id 6):** beatbox owns the puppet *choreography* and
  sends **position commands** (neck / jaw targets). lemmy is a dumb actuator — it executes the
  positions beatbox computes. The nod/head-bang brain (`nod_engine`) lives here, not in lemmy.
- **To [`lightshow`](../lightshow/SPEC.md) (lighting, id 7):** beatbox sends a compact **beat
  frame** (~5 parameters — e.g. beat pulse, band energy, tempo/phase, big-beat flag). lightshow
  runs its own light show locally from those parameters; beatbox does not drive LEDs on the bus.

beatbox is a new **node class** (*beat source* / talker) — unlike the detector/guitar/animation/
lighting followers it is primarily a **producer** of bus traffic for its peers, closer in spirit to
how `fretboard` streams ADC to marvin. It does **no game logic**; it is an audio-domain sensor +
animation director. lemmy's spec already anticipates it as **T1S id 5**.

Today beatbox detects beats and shows them locally (the onboard RGB indicator + the `beat` CLI) and
passes audio through to the PWM DACs; it is not yet on the bus. The imported puppet-motion (`servo`,
`nod_engine`) and WS2812 strip effects — the actuation destined for lemmy/lightshow — are parked in
`config.mcc.bak/` pending the pure-publisher rework.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | Microchip **dsPIC33AK512MPS512** (DSC — hardware suited to real-time FFT) |
| Board | **dsPIC33AK512MPS512 GP DIM (EV80L65A)** on the Curiosity Platform Development Board (**EV74H48A**) |
| Toolchain | **XC-DSC v3.31** (distinct from the PIC32CM nodes' XC32) |
| Build | CMake → Ninja; artifacts under `out/beatbox/` |
| DFP | `dsPIC33AK-MP_DFP` (version pinned by MCC on regen — must include MPS512) |
| MAC-PHY | LAN8651 (10BASE-T1S), SPI1 — **up**: PLCA follower id 5, presence heartbeat, `t1s` CLI |

**Note on identity.** The MPLAB project is named `beatbox` — the descriptor is
`.vscode/beatbox.mplab.json`, and the `cmake/`, `_build/`, and `out/` trees regenerate under
`beatbox/`. (Renamed from the source project's `Beat_Detection`; MCC↔project association in
`config.mcc/mcc/mcc.vscode` updated to match.)

### Pin map (MPS512, current MCC config)

Pins as assigned in the regenerated MCC config (`mcc_generated_files/system/pins.{h,c}`). Audio-in
uses the dedicated ADC4 analog channels (AN0/AN1 — package pins per the EV80L65A GP DIM pinout); all
other peripheral routes are PPS.

| Pin | Dir | Function |
|---|---|---|
| ADC4 AN0 | In | Left audio in (`ADC_AUDIO_L`) |
| ADC4 AN1 | In | Right audio in (`ADC_AUDIO_R`) |
| RB8 (PWM1H) | Out | Left audio out (PWM DAC) |
| RB9 (PWM2H) | Out | Right audio out (PWM DAC) |
| RD9 (SCCP1/OCM1) | Out | Onboard RGB LED — red |
| RD0 (SCCP2/OCM2) | Out | Onboard RGB LED — green |
| RD2 (SCCP3/OCM3) | Out | Onboard RGB LED — blue |
| RA15 | Out | T1S MAC-PHY reset (`T1S_RST`) |
| RE5 | Out | T1S SPI chip-select (`T1S_CS`) |
| RE2 | In | T1S IRQ (`T1S_IRQ_N`, active-low, change-notice) |
| RG4 (SDO1) | Out | T1S SPI MOSI |
| RG9 (SDI1) | In | T1S SPI MISO |
| RE10 (SCK1) | Out | T1S SPI clock |
| RH0 (U2TX) / RD10 (U2RX) | Bi | UART2 — CLI @ 115200 8N1 |
| RH1 (U1TX) / RD1 (U1RX) | Bi | UART1 — debug/telemetry @ 115200 (configured; app port pending) |
| RC8–RC15 | Out | Board LED0–LED7 (active-high) |
| RF3 / RF0 / RB2 | In | Board SW1 / SW2 / SW3 (active-low) |

*Not yet re-added from the `.bak` baseline:* the speed-trim potentiometer (ADC1) and the WS2812
strip (moves to lightshow). See the journal's B0.6 plan.

## 3. Signal chain

Layered `audio → beat_detect → beat_engine`, decoupled by a stereo sample callback. The 48 kHz ADC
ISR does only the cheap per-sample work; the FFT runs in the main loop. Full detail — the DSP, the
tuning constants, and the no-RTOS cooperation model — is in
[`docs/beat-detection.md`](docs/beat-detection.md).

```
Line-in (stereo) → ADC4 @ 48 kHz, 256× oversample, PG1-triggered      (audio.c)   [ISR]
  → DC-block HPF (~20 Hz) → PWM-DAC passthrough (RB8/RB9, monitor only)
  → Audio_SampleCallbackRegister → BeatDetect_Process(L+R → mono)

  → downsample ÷4 → 512-pt Hanning FFT (radix-2 DIT, float)          (beat_detect.c) [main loop, ~23.4 Hz]
  → spectral flux: bass bins 1–10 / mid+high bins 11–255; kick detector on bins 2–4
  → auto-ranged flux + envelope + bass-dominance

  → onset decision: two bands, 4-frame rolling-average threshold + cooldown (beat_engine.c)
  → BeatFrame {bass/full/kick beats, flux, envelope, bass_dominant}

  Consumers today:   onboard RGB indicator (main.c), `beat` CLI (cli.c)
  Consumers planned: T1S position cmds → lemmy;  T1S beat frame → lightshow   (needs tempo/phase)
```

## 4. Source modules (`config.mcc/src/`)

Current app modules on the fresh MCC config:

| File | Role |
|------|------|
| `audio.c/h` | 48 kHz stereo ADC4 ISR, DC-block HPF, PWM-DAC passthrough, `Audio_SampleCallbackRegister` |
| `beat_detect.c/h` | downsample + 512-pt FFT, spectral flux, kick detector, auto-ranging (features) |
| `beat_engine.c/h` | onset decision over the features; publishes a `BeatFrame` |
| `t1s_follower.c/h` | OA-TC6 / LAN8651 10BASE-T1S PLCA follower (SPI1) + `tc6-conf.h` |
| `rgb_led.c/h` | onboard RGB LED (three SCCP PWM channels) |
| `cli.c/h` | UART2 command line (embedded-cli), `main.c` glues it all together |

MCC-generated files under `config.mcc/mcc_generated_files/` — **do not edit** (project rule).

### Parked in `config.mcc.bak/`

The pre-migration tree (imported `dspicguitarhero` modules) is retained for re-integration or
retirement: `nod_engine` (puppet choreography → the future **lemmy command source**), `servo`
(local RC-servo, role → lemmy), `ws2812` (strip, role → lightshow), `uart_debug` (UART1 CSV
telemetry + RX commands, app port pending — see B0.6), and the original `main.c` (tempo tracking +
phase oscillator + WS2812 effects, the source of the deferred tempo/phase work).

## 5. Interfaces

### Present

- **`BeatFrame` (internal contract)** — `beat_engine` publishes one per frame (~23.4 Hz): bass/full/
  kick beat flags, kick strength, bass/mid+high flux, absolute envelope, bass-dominance. This is the
  data the on-bus formats are derived from. Field table in
  [`docs/beat-detection.md`](docs/beat-detection.md) §6.
- **UART2 CLI @ 115200** — `info`, `beat` (latest frame), `audio` (raw/filtered levels + peaks),
  `rgb`, `t1s` (+ `t1s id` / `t1s plca`), `reset`.
- **T1S presence heartbeat** — ethertype `0x88B6`, `node_type = 6` (beat source), so marvin's node
  table sees beatbox once a coordinator is on the wire. (marvin-side decode of `node_type = 6` is a
  follow-up; until then it lists beatbox by src-MAC / id 5.)

### Planned (B4)

- **lemmy position commands** — beatbox computes neck/jaw targets (the ported `nod_engine`
  choreography) and sends them to lemmy over the bus. Payload/ethertype TBD; shared design with
  lemmy (which drops its own beat-driven nod and becomes a position follower).
- **lightshow beat frame** — ~5 parameters per frame (candidates: beat pulse, bass energy, mid+high
  energy, tempo/phase, big-beat flag) derived from the `BeatFrame`. Payload/ethertype TBD; shared
  design with lightshow.

Both bus formats depend on the **tempo/BPM + phase** layer, which is a downstream consumer of the
beat events and not yet ported.

## 6. Milestones

T1S was brought up ahead of the pure-publisher rework (B3 before B1/B2), so the transport
foundation exists while the local actuation is simply left parked rather than formally stripped.
The journal's [Plan](docs/journal.md) tracks the fine-grained sub-steps (B0.5 device retarget,
B0.5.5 clock tree, B0.6 per-peripheral MCC re-integration).

- [x] **B0 — Import.** Firmware brought into `firmware/beatbox/`, cruft dropped, docs written.
- [x] **B0.5 — Retarget to MPS512** (EV80L65A DIM on EV74H48A) via a **fresh MCC config** (not a
      device-swap): old tree → `config.mcc.bak/`, new Melody baseline generated; clock tree =
      PLL1 200 MHz / 100 MHz Fcy.
- [~] **B0.6 — Re-integrate keep-set peripherals onto the fresh config.** Done: PWM audio + 48 kHz
      ADC trigger, ADC4 stereo audio, RGB LED, UART2 CLI. Pending: UART1 app port (`uart_debug`),
      ADC1 pot read.
- [x] **Beat detection ported.** Audio passthrough (`audio.c`) → FFT feature extraction
      (`beat_detect.c`) → onset decision (`beat_engine.c`) → `BeatFrame`; `beat` CLI + onboard RGB
      indicator. Tempo/BPM + phase deferred (downstream of the beat events).
- [x] **B3 — T1S on dsPIC33AK.** OA-TC6 follower + LAN8651 glue ported to dsPIC33A / XC-DSC (first
      non-PIC32CM node, first bus *talker*). PLCA follower **id 5**, presence heartbeat, `t1s` CLI.
- [ ] **B1 — Pure-publisher rework.** Formalize dropping the parked on-node actuation
      (`servo`/`nod_engine` → lemmy, `ws2812` → lightshow). Keep the PWM audio output and the RGB
      indicator. Define the beat/command data model the bus will carry.
- [ ] **B2 — Publish over UART fallback.** *(optional, now that T1S is up)* Emit position commands +
      beat frame over UART1 first to prove the data model against lemmy/lightshow.
- [ ] **B4 — Live show.** Drive lemmy (position cmds) and lightshow (beat frame) from live audio over
      T1S; marvin recognizes beatbox's heartbeat. Requires the tempo/phase layer.

## 7. Provenance

Imported (file copy, no git history) from the standalone `dspicguitarhero` project
(`Beat_Detection/`), a self-contained "Lemmy Kilmister animatronic bust that head-bangs to music."
The source's per-file version tags (`main.c` v93, `beat_detect.c` v29) and its change history are
recorded in [`docs/journal.md`](docs/journal.md), per the repo's comment rule. The source's
`referenceProjectForWS2812Inputs/` (a WS2812 demo + board-doc PDFs, flagged as duplicate-`main()`
cruft even there) was **not** imported.
