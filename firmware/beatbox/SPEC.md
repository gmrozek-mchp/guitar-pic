# beatbox — Specification

> What beatbox *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: imported, autonomous baseline.** The firmware was brought in from the standalone
> `dspicguitarhero` project (a self-contained head-banging animatronic) and currently runs
> **autonomously** — audio in → FFT beat detection → local servo + RGB + WS2812 outputs, UART
> telemetry. It is **not yet on the T1S bus**. The re-scoping to a **pure T1S beat-source
> publisher** (below) is the work ahead. See §6 and [`docs/journal.md`](docs/journal.md).

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

Until the bus work lands, beatbox also retains its original **standalone** outputs (local servo,
RGB, WS2812) so it runs as a self-contained demo — the actuation that will move to lemmy/lightshow.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | Microchip **dsPIC33AK512MPS512** (DSC — hardware suited to real-time FFT) |
| Board | **dsPIC33AK512MPS512 GP DIM (EV80L65A)** on the Curiosity Platform Development Board (**EV74H48A**) |
| Toolchain | **XC-DSC v3.31** (distinct from the PIC32CM nodes' XC32) |
| Build | CMake → Ninja; artifacts under `out/beatbox/` |
| DFP | `dsPIC33AK-MP_DFP` (version pinned by MCC on regen — must include MPS512) |
| MAC-PHY | LAN8651 (10BASE-T1S) — **not yet wired** (see §6, the porting milestone) |

**Note on identity.** The MPLAB project is named `beatbox` — the descriptor is
`.vscode/beatbox.mplab.json`, and the `cmake/`, `_build/`, and `out/` trees regenerate under
`beatbox/`. (Renamed from the source project's `Beat_Detection`; MCC↔project association in
`config.mcc/mcc/mcc.vscode` updated to match.)

### Pin map (autonomous baseline, from the source project)

> **These pins are the MPS306-era baseline and must be re-derived for the MPS512.** The
> PPS-remappable outputs (SCCP servo/RGB, SDO3, UART1, RD1) re-route freely; the fixed-function
> pins — the two audio-in ADC channels, the speed pot, and the two PWM-DAC outputs — must be
> re-picked against the MPS512 datasheet pin table and the EV80L65A GP DIM / EV74H48A pinout. See
> the journal.

| Pin | Direction | Function |
|---|---|---|
| RB3 (AD2AN3) | In | Left audio |
| RB4 (AD2AN4) | In | Right audio |
| RA3 (AD1AN2) | In | Speed-trim potentiometer |
| RB8 (PWM4H) | Out | Left audio out (PWM DAC) |
| RB9 (PWM3H) | Out | Right audio out (PWM DAC) + ADC trigger source |
| RA9 (SCCP4) | Out | Servo PWM (head nod) — *moves to lemmy* |
| RA10 (SCCP1) | Out | LED green — *moves to lightshow* |
| RA2 (SCCP2) | Out | LED red — *moves to lightshow* |
| RC0 (SCCP3) | Out | LED blue — *moves to lightshow* |
| RA11 (SDO3) | Out | WS2812 strip data (SPI3 + DMA0) — *moves to lightshow* |
| RD1 | Out | Digital beat indicator |
| UART1 | Bi | Debug / telemetry @ 115200 |

## 3. Signal chain

```
Line-in (stereo, 3.3V, RB3/RB4)
  → ADC2 @ 192 kHz, 256× oversample → effective 48 kHz  (adc_audio.c, ADC ISR)
  → DC-blocking HPF (~20 Hz)
  → PWM audio passthrough @ 192 kHz (pwm_audio.c) — also generates the 48 kHz ADC trigger
  → BeatDetect_Process(mono)                              [ADC ISR, 48 kHz]

  → 4× downsample → 512-pt Hanning FFT (radix-2 DIT, float) (beat_detect.c)  [main loop, ~23.4 Hz]
  → spectral flux: bass bins 1–10 / mid+high bins 11–255
  → kick detector: fast-attack/slow-release envelope on bins 2–4
  → auto-ranged 0–1000 flux + audio envelope

  → beat decision + tempo tracking (main.c, ~23.4 Hz)
     two detectors (bass, mid+high), 4-frame rolling threshold, phase oscillator,
     median-filtered interval history, pot speed-trim
  → nod_engine.c: band selection, adaptive oscillator, beat snaps, comeback bang, idle drift

  Outputs today (autonomous):     audio out (PWM DAC), servo angle, RGB pulse, WS2812 effects, UART telemetry
  Outputs planned (pure publisher): audio out (PWM DAC, retained);  T1S position cmds → lemmy;  T1S beat frame → lightshow
```

## 4. Source modules (`config.mcc/`)

| File | Role | Fate under pure-publisher |
|------|------|---------------------------|
| `adc_audio.c/h` | 48 kHz stereo ADC ISR, DC-blocking HPF, calls `BeatDetect_Process` | keep |
| `beat_detect.c/h` | 512-pt FFT, spectral flux, kick detector, auto-ranging | keep |
| `main.c` | Orchestration: beat decision, tempo tracking, phase oscillator, pot, WS2812 effects, telemetry dispatch | keep (WS2812 effect code retires with the strip) |
| `nod_engine.c/h` | Puppet choreography — becomes the **lemmy command source** (compute position, send over bus) instead of driving a local servo | keep, retarget output |
| `pwm_audio.c/h` | PWM DAC @ 192 kHz **and the 48 kHz ADC trigger (PG3 postscale)** | keep — retained audio output (line-in → line-out pass-through today; a candidate path for voicing lemmy onto the output), and the ADC trigger depends on it |
| `uart_debug.c/h` | UART1 telemetry (CSV `D`/`S` rows) + RX command parser | keep — likely the first publish transport before T1S |
| `servo.c/h` | Local RC-servo PWM (SCCP4) | **drop** at strip-out (role → lemmy) |
| `rgb_led.c/h` | Local RGB PWM (SCCP1/2/3) | **drop** at strip-out (role → lightshow); beat→color mapping is a reference for the lightshow beat frame |
| `ws2812.c/h` | Local WS2812 strip driver (SPI3 + DMA0) | **drop** at strip-out (role → lightshow) |

MCC-generated files under `config.mcc/mcc_generated_files/` — **do not edit** (project rule).

## 5. Interfaces (planned)

- **lemmy position commands** — beatbox computes neck/jaw targets in `nod_engine` and sends them to
  lemmy over the bus. Payload/ethertype TBD; shared design with lemmy (which drops its own
  beat-driven nod logic and becomes a position follower).
- **lightshow beat frame** — ~5 parameters per frame (candidates: beat pulse, bass energy,
  mid+high energy, tempo/phase, big-beat flag). Payload/ethertype TBD; shared design with lightshow.
- **UART telemetry** — retained from the source: `D,` data rows (~23.4 Hz), `S,` spectrum rows
  (~8 Hz); RX commands `F` (band), `V` (servo test), `O` (oscillator), `M`/`B`/`T`.

## 6. Milestones

- [x] **B0 — Import.** Firmware brought into `firmware/beatbox/`, cruft dropped, docs written.
      Autonomous baseline preserved (builds + runs standalone).
- [ ] **B1 — Pure-publisher rework.** Strip the on-node actuation (`servo`, `rgb_led`, `ws2812`).
      **Keep the PWM audio output** (`pwm_audio`) — it stays a retained output (pass-through, and a
      candidate for voicing lemmy) and the 48 kHz ADC trigger depends on it. Define the beat/command
      data model `nod_engine` and the lightshow frame will carry.
- [ ] **B2 — Publish over UART fallback.** Emit position commands + beat frame over UART first, to
      prove the data model against lemmy/lightshow before the T1S port.
- [ ] **B3 — T1S on dsPIC33AK.** Port the T1S/PLCA follower + LAN8651 MAC-PHY glue (today shared by
      the PIC32CM `guitar`/`lemmy`/`lightshow` nodes, XC32) to dsPIC33AK + XC-DSC. First non-PIC32CM
      node and first bus *talker*. Join as PLCA follower **id 5**, presence heartbeat, `t1s` CLI.
- [ ] **B4 — Live show.** Drive lemmy (position cmds) and lightshow (beat frame) from live audio
      over T1S; marvin recognizes beatbox's heartbeat (node table row, as for the other nodes).

## 7. Provenance

Imported (file copy, no git history) from the standalone `dspicguitarhero` project
(`Beat_Detection/`), a self-contained "Lemmy Kilmister animatronic bust that head-bangs to music."
The source's per-file version tags (`main.c` v93, `beat_detect.c` v29) and its change history are
recorded in [`docs/journal.md`](docs/journal.md), per the repo's comment rule. The source's
`referenceProjectForWS2812Inputs/` (a WS2812 demo + board-doc PDFs, flagged as duplicate-`main()`
cruft even there) was **not** imported.
