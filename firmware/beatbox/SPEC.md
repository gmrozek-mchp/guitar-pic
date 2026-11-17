# beatbox — Specification

> What beatbox *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: working on the bus.** Stereo line-in (ADC4, 48 kHz) → 512-pt FFT beat detection →
> `BeatFrame`, shown locally (onboard RGB beat indicator, LED0–7 VU meter, UART1 PC visualizer) and
> **broadcast** as an 8-byte beat frame on 10BASE-T1S (PLCA follower id 5, ethertype `0x88B8`,
> ~23.4 Hz). lightshow and lemmy both consume that broadcast; lightshow's show was verified with
> beatbox live. Not done: a tempo/BPM + phase layer (the frame's `tempo`/`phase` bytes are 0) and the
> pot read. See §6, [`docs/journal.md`](docs/journal.md), and the signal chain in
> [`docs/beat-detection.md`](docs/beat-detection.md).

## 1. Purpose

beatbox is the **beat-source node** on the marvin T1S bus — the system's ear. It listens to a
line-level audio feed, runs real-time FFT beat detection, and **broadcasts** one compact beat frame
(`LightshowFrame`, §5) that every interested node consumes:

- **[`lightshow`](../lightshow/SPEC.md) (lighting, id 7)** renders its LED show from it.
- **[`lemmy`](../lemmy/SPEC.md) (animation, id 6)** runs its own nod engine and jaw animator from
  it. The puppet choreography lives on lemmy; beatbox sends lemmy no per-node commands.

beatbox is a new **node class** (*beat source* / talker) — unlike the detector/guitar/animation/
lighting followers it is primarily a **producer** of bus traffic for its peers, closer in spirit to
how `fretboard` streams ADC to marvin. It does **no game logic** and drives no actuator on the bus.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | Microchip **dsPIC33AK512MPS512** (DSC — hardware suited to real-time FFT) |
| Board | **dsPIC33AK512MPS512 GP DIM (EV80L65A)** on the Curiosity Platform Development Board (**EV74H48A**) |
| Toolchain | **XC-DSC v3.31** (distinct from the PIC32CM nodes' XC32) |
| Build | CMake → Ninja; artifacts under `out/beatbox/` |
| DFP | `dsPIC33AK-MP_DFP` (version pinned by MCC on regen — must include MPS512) |
| MAC-PHY | LAN8651 (10BASE-T1S), SPI1 — PLCA follower **id 5** of 8, MAC `02:00:00:00:00:05` |

The MPLAB project descriptor is `.vscode/beatbox.mplab.json` (programmer: the board's on-board
PKOB4); `cmake/`, `_build/`, and `out/` regenerate under `beatbox/`.

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
| RH1 (U1TX) / RD1 (U1RX) | Bi | UART1 — GUI telemetry @ 115200 8N1 (`D,`/`S,` lines → PC visualizer) |
| RC8–RC15 | Out | Board LED0–LED7 (active-high) |
| RF3 / RF0 / RB2 | In | Board SW1 / SW2 / SW3 (active-low) |

The sensitivity pot is configured in MCC (ADC5, `ADC_POT` on AD5AN0) but nothing reads it yet.

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

  → LightshowFrame (compact 8-byte bus payload for lightshow)          (publish.c)

  → T1S broadcast (ethertype 0x88B8, ~23.4 Hz)                        (t1s_follower.c)

  Consumers: onboard RGB indicator (main.c), LED0–7 VU meter (vu_meter.c),
             UART1 telemetry → PC visualizer (uart_debug.c), `beat` + `show` CLI (cli.c),
             T1S broadcast → lightshow (LED show) + lemmy (nod + jaw)
```

## 4. Source modules (`config.mcc/src/`)

| File | Role |
|------|------|
| `audio.c/h` | 48 kHz stereo ADC4 ISR, DC-block HPF, PWM-DAC passthrough, `Audio_SampleCallbackRegister` |
| `beat_detect.c/h` | downsample + 512-pt FFT, spectral flux, kick detector, auto-ranging (features) |
| `beat_engine.c/h` | onset decision over the features; publishes a `BeatFrame` |
| `publish.c/h` | outbound-payload layer: maps `BeatFrame` → the compact `LightshowFrame` bus payload |
| `t1s_follower.c/h` | OA-TC6 / LAN8651 10BASE-T1S PLCA follower (SPI1) + `tc6-conf.h`; presence heartbeat + beat-frame broadcast (`0x88B8`) |
| `rgb_led.c/h` | onboard RGB LED (three SCCP PWM channels) |
| `vu_meter.c/h` | board LED0–LED7 as an audio VU meter off `raw_env` |
| `uart_debug.c/h` | UART1 GUI telemetry: `D,`/`S,` lines → PC visualizer (`tools/gui/puppet.py`), non-blocking TX ring |
| `cli.c/h` | UART2 command line (embedded-cli), `main.c` glues it all together |

MCC-generated files under `config.mcc/mcc_generated_files/` — **do not edit** (project rule).

The imported `dspicguitarhero` actuation code (`servo`, `nod_engine`, `ws2812`, and the original
`main.c` with its tempo/phase oscillator) is not built here. Its roles moved to lemmy (`nod_engine`
port) and lightshow (WS2812 effects port). The pre-migration tree was kept as a local, git-ignored
`config.mcc.bak/`; it is not in the repo.

## 5. Interfaces

### Present

- **`BeatFrame` (internal contract)** — `beat_engine` publishes one per frame (~23.4 Hz): bass/full/
  kick beat flags, kick strength, bass/mid+high flux, absolute envelope, bass-dominance. This is the
  data the on-bus formats are derived from. Field table in
  [`docs/beat-detection.md`](docs/beat-detection.md) §6.
- **`LightshowFrame` (bus payload, on the wire)** — `publish` builds one per `BeatFrame`
  (~23.4 Hz): an 8-byte frame (`seq`, `energy`, `bass`, `treble`, `kick`, `flags`, and reserved
  `tempo`/`phase` = 0), all 0–255; `flags` = `PUB_FLAG_*` in `publish.h`. `t1s_follower` broadcasts
  it under **ethertype `0x88B8`** (dst `FF:FF:FF:FF:FF:FF`), dropping a frame if PLCA isn't operating
  or the previous one is still draining. Consumed by lightshow and lemmy.
- **UART2 CLI @ 115200** — `info`, `beat` (latest detection frame), `show` (latest `LightshowFrame`),
  `audio` (raw/filtered levels + peaks), `rgb`, `t1s` (status incl. a `beat tx` counter; + `t1s id` /
  `t1s plca`), `gui` (GUI telemetry on/off + band), `reset`.
- **UART1 GUI telemetry @ 115200 8N1** — `uart_debug` emits a `D,` line per beat frame (env, flux,
  bass/full beats, bass-flux, bass/full peak bins) and an `S,` 64-bin spectrum line every third frame,
  for the PC visualizer `tools/gui/puppet.py`. `phase`/`bpm` are 0 (no tempo/phase layer yet) and the
  reported frame period is the constant 23.4375 Hz. Toggle with the `gui` CLI command. Separate from
  the UART2 CLI — this is a machine-readable stream, not an interactive console.
- **T1S presence heartbeat** — ethertype `0x88B6`, every 500 ms once PLCA is operating; v2 payload
  (20 bytes, incl. tx/rx/crc/sym counters), `node_type = 6` (beat source). marvin lists beatbox by
  node id 5 ([`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.2).
- **Inbound:** beatbox has no control channel; `0x88B5` frames are only logged for diagnostics.

### Not yet built

- **Tempo/BPM + phase** — would fill the frame's reserved `tempo`/`phase` bytes (lightshow's comet
  switches to the wire phase when non-zero; lemmy tracks tempo itself).

## 6. Milestones

The journal's [Plan](docs/journal.md) tracks the fine-grained sub-steps.

- [x] **B0 — Import.** Firmware brought into `firmware/beatbox/`, cruft dropped, docs written.
- [x] **B0.5 — Retarget to MPS512** (EV80L65A DIM on EV74H48A) via a **fresh MCC config** (not a
      device-swap): old tree → `config.mcc.bak/`, new Melody baseline generated; clock tree =
      PLL1 200 MHz / 100 MHz Fcy.
- [~] **B0.6 — Re-integrate keep-set peripherals onto the fresh config.** Done: PWM audio + 48 kHz
      ADC trigger, ADC4 stereo audio, RGB LED, UART2 CLI, UART1 GUI telemetry (`uart_debug`).
      Pending: ADC5 pot read.
- [x] **Beat detection ported.** Audio passthrough (`audio.c`) → FFT feature extraction
      (`beat_detect.c`) → onset decision (`beat_engine.c`) → `BeatFrame`; `beat` CLI + onboard RGB
      indicator. Tempo/BPM + phase deferred (downstream of the beat events).
- [x] **B3 — T1S on dsPIC33AK.** OA-TC6 follower + LAN8651 glue ported to dsPIC33A / XC-DSC (first
      non-PIC32CM node, first bus *talker*). PLCA follower **id 5**, presence heartbeat, `t1s` CLI.
- [x] **B1 — Pure publisher.** beatbox builds no actuation code; the bus data model is the single
      `LightshowFrame` broadcast (`publish.{c,h}`).
- [~] **B4 — Live show.** Beat frame broadcasts over T1S (`0x88B8`); lightshow's show verified with
      beatbox live; lemmy's nod/jaw consume the same frame (pending lemmy's on-hardware check).
      Remaining: tempo/phase layer.

## 7. Provenance

Imported (file copy, no git history) from the standalone `dspicguitarhero` project
(`Beat_Detection/`), a self-contained "Lemmy Kilmister animatronic bust that head-bangs to music."
The source's per-file version tags (`main.c` v93, `beat_detect.c` v29) and its change history are
recorded in [`docs/journal.md`](docs/journal.md), per the repo's comment rule. The source's
`referenceProjectForWS2812Inputs/` (a WS2812 demo + board-doc PDFs, flagged as duplicate-`main()`
cruft even there) was **not** imported.
