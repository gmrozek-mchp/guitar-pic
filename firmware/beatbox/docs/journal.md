# beatbox — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the beatbox
(beat-source) firmware. Newest entries at the top. For *what beatbox is* (purpose, hardware, link,
firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

## Current focus

**Import first, then re-scope to a pure T1S beat publisher.** beatbox came in from the standalone
`dspicguitarhero` project as a working **autonomous** animatronic (audio → FFT beat detect → local
servo + RGB + WS2812). The plan is to strip the local actuation and turn it into the bus **beat
source**: position commands → lemmy (id 6), a ~5-parameter beat frame → lightshow (id 7). It joins
the bus as PLCA follower **id 5** (the id lemmy's spec already reserves for it). Milestones B0–B4 in
[`../SPEC.md`](../SPEC.md) §6.

## Plan

See [`../SPEC.md`](../SPEC.md) §6 for the milestone list (B0 import → B1 pure-publisher rework →
B2 UART-fallback publish → B3 T1S-on-dsPIC port → B4 live show). Notes as work lands go here.

- [x] **B0 — Import.** File copy into `firmware/beatbox/`; cruft dropped; SPEC + journal + README
      written; registered in top-level `SPEC.md` / `CLAUDE.md`.
- [x] **B0.5 — Retarget MPS306 → MPS512** (EV80L65A DIM on EV74H48A). Done via a **fresh MCC config**,
      not a device-swap: the old `config.mcc/` was backed out to `config.mcc.bak/` and a new Melody
      config generated from scratch on the 512MPS512 (`system` module only). Fixed-function pin re-pick
      (2 audio-in ADC channels, speed pot, 2 PWM-DAC outs) happens as each peripheral is re-added in B0.6.
- [x] **B0.5.5 — Clock tree.** System clock = **PLL1 Out 200 MHz** from the board's **external 8 MHz
      clock** (Primary Oscillator, EC mode: `POSCMD=0`, `POSCEN`, wait `POSCRDY`). PLL: 8 MHz → PLLPRE 1
      → ×100 = 800 MHz VCO → ÷4 = 200 MHz; peripheral bus = Fosc/2 = **100 MHz** (keeps all parked
      module timing constants valid — UART `BRG=868`, servo 1:64, WS2812 `SPI3BRG=0x14`). The two `.bak`
      hand-edits are resolved by config, not poking: `POSCMD` is MCC-generated, and the 306-only
      `POSCIOFNC` OSCO-release is dropped (re-derive any OSC-pin GPIO on the 512/EV80L65A map). **CLK5
      (800 MHz VCO-divider tap for the high-res PWM) deferred to the PWM step** — the 800 MHz VCO already
      exists, so that step only enables the generator.
- [ ] **B0.6 — Re-integrate the keep-set peripherals into the fresh MCC config.** Starting point is now
      a bare `system`-only Melody config; the app modules live in `config.mcc.bak/`. Bring back only
      what beatbox *keeps*; do **not** re-add `servo`/`rgb_led`/`ws2812` (slated for removal at B1).
      Workflow per peripheral: add + configure in Melody → regenerate → port the `.bak` module's logic
      onto the generated API → copy the file into `config.mcc/` and add it to the descriptor fileset.
      Order (independent → coupled):
  - [~] **UART1** (`uart_debug.c`): async 8N1, 115200 (BRG 868 @ 100 MHz), TX+RX. **MCC config done**
        — module added, `U1TX→RH1` (RP114), `U1RX→RD1` (RPINR13=0x32), fractional BRG 868 (115207
        actual), polled, printf-redirect `write()` generated in `uart1.c`, wired into
        `SYSTEM_Initialize()`. **App port deferred** (bring `uart_debug.c` back from `.bak` onto the
        `UART1_*` API; must delete the module's hand-rolled `write()` to avoid a duplicate symbol).
  - [ ] **ADC1 pot** (inline in `main.c`): CH0 on RA3/AD1AN2, SW-triggered single 12-bit sample.
        Move the inline setup into MCC; read via the generated API.
  - [x] **PWM audio** (`pwm_audio.c`): high-res (16× HREN) 192 kHz on **PG1/PG2** (was PG3/PG4 in
        the `.bak`), PG2 SOC-triggered from PG1, **PG1 EVT postscale ÷4 → 48 kHz ADC trigger**
        (`PG1EVT1` `ADTR1EN1`+`ADTR1PS=1:4`+`PGTRGSEL=TRIGA`, `PG1TRIGA=0`). MCC config done.
  - [x] **ADC2 audio → ADC4** (`adc_audio.c`): built on **ADC4 CH0 (`ADC_AUDIO_L`/AD4AN0) + CH1
        (`ADC_AUDIO_R`/AD4AN1)** (was ADC2 CH6/CH7), 256× oversample, `TRG1SRC`=PWM1 (PG1),
        complete-interrupt on CH1 (higher channel) at priority 6. MCC config done. App port
        (HPF + passthrough + `BeatDetect_Process` into the CH1 callback) deferred.

## Open questions

- **lemmy command protocol.** What does beatbox send lemmy — absolute neck/jaw angles, or a
  higher-level pose/energy the puppet interprets? Ethertype + payload? Shared design with lemmy,
  which drops its own beat-driven nod logic to become a position follower. Resolve at B1/B2.
- **lightshow beat frame.** Which ~5 parameters (candidates: beat pulse, bass energy, mid+high
  energy, tempo/phase, big-beat flag)? Fixed-rate frames or event-driven? Shared design with
  lightshow (its journal has the mirror question). Resolve at B1/B2.
- **T1S on dsPIC33AK.** The PIC32CM nodes share XC32 T1S/PLCA + LAN8651 glue. How much ports vs.
  needs a dsPIC/XC-DSC rewrite? Which SERCOM/SPI + IRQ pins on the DIM board? Resolve at B3.
- **XC-DSC path on macOS.** `.vscode/settings.json` clangd path was set to
  `/Applications/microchip/xc-dsc/v3.31/bin/xc-dsc-clangd` (Mac analogue of the source's Windows
  path); verify against the actual install.

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-30 | **Backed out MCC and regenerated a fresh minimal config on the 512MPS512.** Old `config.mcc/` (hand-rolled app modules + the half-migrated MCC tree) renamed to `config.mcc.bak/` (git-ignored via `*.bak*`); a new Melody config was generated from scratch containing only the `system` module (clock, config_bits, dmt, interrupt, pins, reset, traps, watchdog). `main.c` is the pristine MCC skeleton (`SYSTEM_Initialize()` + empty loop). The 9 app modules (`adc_audio`, `beat_detect`, `nod_engine`, `pwm_audio`, `rgb_led`, `servo`, `uart_debug`, `ws2812`, old `main.c`) stay parked in `config.mcc.bak/` for re-integration (B0.6). | The prior tree was mid-migration and inconsistent (hand-written inits not called, a stub CH0 ADC2, device still 306 internally). A clean Melody baseline on the confirmed 512MPS512 is a firmer foundation than device-swapping a stale config — which the earlier note already flagged as unreliable. Supersedes the device-swap approach in the row below. |
| 2026-07-30 | **Pull peripheral config into MCC, keep-set only; audio ADC via MCC callback.** The imported firmware is almost all hand-rolled SFR writes (MCC does only system + pins + a stub ADC2). Migrate just what beatbox keeps — ADC2 (audio), PWM (audio-out + ADC trigger), ADC1 (pot), UART1 — and leave `servo`/`rgb_led`/`ws2812` hand-rolled since they're slated for deletion at B1. The 48 kHz audio-ADC complete event moves to MCC's generated interrupt + registered callback (not the hand-written `_AD2CH7Interrupt`). See plan item B0.6. | MCC-ifying peripherals we're about to remove is wasted work. Pins are already fully in MCC, so migration is module-level and low-risk. The callback model keeps the ADC path idiomatic MCC even though it adds indirection in the tight loop — accepted for maintainability now that the config is regenerated for MPS512. Watch the PG3-postscale→ADC-trigger coupling: it must be reproduced in the MCC PWM config or the audio pipeline stops clocking. |
| 2026-07-30 | **Target device changed dsPIC33AK256MPS306 → dsPIC33AK512MPS512** (EV80L65A GP DIM on the EV74H48A Curiosity Platform board). Same dsPIC33A MPS line, 200 MHz core, XC-DSC v3.31, same peripheral classes — MPS512 is the larger sibling (512 KB flash, more RAM/pins). Descriptor + docs updated (step 1); MCC device swap + full `mcc_generated_files/` regen + fixed-function pin re-map are the user's MCC-GUI steps. | Bigger part is pure headroom for the FFT/publisher work and matches the hardware on hand. Low risk: the DSP/beat code is pin-agnostic. The only real work is re-deriving the **fixed-function** pins (2 audio-in ADC channels, speed pot, 2 PWM-DAC outputs) against the MPS512 datasheet + EV80L65A DIM/EV74H48A pinout; PPS outputs (SCCP servo/RGB, SDO3, UART1, RD1) re-route freely. DFP stays `dsPIC33AK-MP_DFP` (verify the version includes MPS512; MCC pins it on regen). Melody caveat: device-change on an existing config is unreliable — likely cleaner to build a fresh MCC config for MPS512 re-adding the same module set. |
| 2026-07-30 | **Renamed the MPLAB project `Beat_Detection` → `beatbox`.** Descriptor `.vscode/Beat_Detection.mplab.json` → `.vscode/beatbox.mplab.json` (`imagePath` → `./out/beatbox/…`); `settings.json` `compileCommandsDir` → `_build/beatbox/…`; MCC association `config.mcc/mcc/mcc.vscode` `name:` → `beatbox`. `mcc_generated_files/` had **no** references, so the MCC source tree was untouched. `cmake/`/`out/`/`_build/` are generated under the project name and regenerate as `beatbox/`. | One name end-to-end (repo dir, MPLAB project, build trees). The `cmake/` tree had already been cleared by the MPLAB VS Code extension, so the rename reduced to the descriptor + settings + MCC pointer; MPLAB regenerates `cmake/beatbox/` on next open/build. Caveat: verify in MPLAB (Open project → build) that it regenerates cleanly under `beatbox`. |
| 2026-07-30 | **Keep the PWM audio output through the pure-publisher rework.** `pwm_audio` stays; the audio DAC (RB8/RB9) remains a first-class output, not stripped with the servo/RGB/WS2812. Today it is a clean line-in → line-out pass-through (input samples, DC-blocking HPF only, written straight to the DAC in the 48 kHz ISR); it is also the intended path to **voice lemmy** onto the output later (mix/generate audio instead of pure pass-through). | The DAC is already there and idle for anything we route to it, and `pwm_audio` is load-bearing regardless — it generates the 48 kHz ADC trigger (PG3 postscale), so the module can't be dropped. Resolves the "ADC trigger vs. audio-out" open question in favor of keeping both. |
| 2026-07-30 | **beatbox = pure T1S beat-source publisher; owns puppet choreography.** It sends **position commands** to lemmy (lemmy becomes a dumb follower, dropping its own beat-driven nod) and a **~5-parameter beat frame** to lightshow (which runs its own show). beatbox drives no actuator on the bus. | Puts the audio/DSP brain on the DSC (dsPIC33AK) that's built for FFT and the actuation on the cheap PIC32CM followers. Centralizes choreography where the beat data lives; keeps lemmy/lightshow dumb, matching the existing follower model. |
| 2026-07-30 | **Imported as a plain file copy, no git history.** Source `dspicguitarhero` (4 commits) stays as provenance. | Distinct project, negligible history; a subtree merge would add noise for no benefit. |
| 2026-07-30 | **Keep the autonomous baseline (local servo/RGB/WS2812) at import; strip it at B1.** Import lands a buildable, working standalone animatronic; local-actuation removal is deliberate milestone-1 work, not part of the copy. | A working baseline is worth more than a half-stripped, non-building tree; stripping requires editing `main.c`/`nod_engine.c` cleanly, which is its own step. |
| 2026-07-30 | **Did not import `referenceProjectForWS2812Inputs/`.** | The source's own CLAUDE.md flagged it as duplicate-`main()` cruft (big board-doc PDFs + a second demo project). Board docs can be added under `hardware/` later if wanted. |

## Imported source history (provenance)

Per-file version tags and notes carried in the source comments, moved here so the source describes
only current code (repo comment rule). The source project tracked versions in file-top comments
rather than a changelog.

- **`main.c` — v93** (2026-07-21): "single `NUM_LEDS` define drives all effect geometry." Earlier
  PROJECT_SUMMARY referenced v86 (2026-07-20): "Auto-band + half-period fix + comeback bang." main.c
  is the beat-decision + phase-oscillator + tempo-tracking + WS2812-effects + telemetry orchestrator.
- **`beat_detect.c` — v29**: "Audio Envelope + Spectral Flux." Computes per-frame audio envelope and
  spectral flux over the 512-pt FFT; the beat *decision* (thresholding) lives in `main.c`. (The
  source's "display only / no beat detection logic" note was folded here and the comment corrected
  to describe current behavior.)
- Source `dspicguitarhero` commits: `e3d01e4` added Microchip MCP server; `f0bfa54` "Working Single
  String of LEDs"; `e9b0d54` "Claude's Summary"; `519571d` "First Commit."

## Board GPIO (EV74H48A dev-board LEDs + switches)

Added to the MCC pin manager for bring-up. **LEDs active-high, switches active-low** (polarity is not
captured in the generated code — use these `_SetHigh/_SetLow`/`_GetValue` macros accordingly).

| Signal | Pin | Kind | Assert |
|---|---|---|---|
| `LED0`..`LED7` | RC8..RC15 | output | high = on |
| `LED_R` / `LED_G` / `LED_B` | RD9 / RD0 / RD2 | output | high = on |
| `SW1` / `SW2` / `SW3` | RF3 / RF0 / RB2 | input | low = pressed |

## Session log

### 2026-07-30 — Audio ADC (ADC4) + PWM→ADC trigger MCC config (config only, no app port)

- **Audio ADC on ADC4 CH0/CH1** (`.bak` used ADC2 CH6/CH7): `ADC_AUDIO_L` = CH0/AD4AN0,
  `ADC_AUDIO_R` = CH1/AD4AN1 (`AD4CH0CON1=0x20C2C4`, `AD4CH1CON1=0x120C2C4` — differ only in PINSEL).
  Both on the ADC4 shared core, **256× oversampling → 16-bit result** (`MODE`=oversample, `ACCNUM`=3),
  `TRG1SRC`=PWM1 (PG1) Trigger1, `SAMC`=0.5 TAD, `IRQSEL`=1.
- **Oversampling requires the retrigger:** in `MODE=11` the first conversion comes from `TRG1SRC`
  and conversions 2..256 from `TRG2SRC` — so `TRG2SRC`=2 (immediate re-trigger) is mandatory or the
  accumulation never completes and the interrupt never fires. MCC's first pass left it 0 (broken);
  fixed to 2, matching the `.bak`.
- **Interrupt on the higher channel.** By fixed channel-priority the shared core converts CH0 (L)
  before CH1 (R), so the CH1 complete-interrupt = both-done. Only `AD4CH1IE` is enabled;
  `AD4CH1IP=6` (high-priority 48 kHz path, matching the `.bak`'s `AD2CH7IP=6`). MCC's first pass
  generated priority 1; raised to 6. This is the datasheet's canonical multi-channel-scan idiom.
- **PWM→ADC trigger on PG1** (`PG1EVT1=0x10019`): `ADTR1EN1` enabled (PGxTRIGA compare = ADC Trigger 1
  source), `ADTR1PS=1:4` (192 kHz ÷ 4 = **48 kHz** sample rate), `PGTRGSEL=Trigger A compare`,
  `PG1TRIGA=0` (sample at cycle start). PG2 has no ADC trigger. This is a **bit-for-bit match to the
  `.bak` PG3** (`ADTR1EN1=1`, `ADTR1PS=3`, `PGTRGSEL=1`, `TRIGA=0`) — including the slave-sync edge,
  since PG2 SOCs off PG1's TRIGA-compare output just as `.bak` PG4 did off PG3.
- **Full register review vs `.bak`** (accounting for the channel/generator renumber): audio ADC and
  PWM are functionally equivalent. All deltas explained — ADC pins (CH0/CH1 = AN0/AN1 choice), the
  PG3/PG4→PG1/PG2 renumber carried consistently through `TRG1SRC` and `SOCS`, a slightly more accurate
  `MPER` (66651 vs 66649), and UPDMOD (settled to **SOC**, matching the `.bak`). No blocking issues.
- **This completes B0.6** — the last keep-set peripheral (audio ADC + the PWM sample trigger) is now
  in MCC. Remaining beatbox work is the app-level ports (see the deferred notes per peripheral) and
  the B1 pure-publisher rework.

### 2026-07-30 — PWM_HS audio + sample-clock MCC config (config only, no app port)

- Added the high-speed PWM (`PGx`) for the audio path, on **PG1 (left → RB8/PWM1H)** and
  **PG2 (right → RB9/PWM2H)** via PPS. Independent Edge, HREN high-resolution, MPERSEL, high-side
  output only (PENH; single-ended into an RC reconstruction filter). `PGxCON=0x40000088/0x40010088`
  (UPDMOD **SOC** — L/R duty latch together at start-of-cycle via `UPDREQ`, matching the `.bak`).
  Ported from the `.bak`, which used PG3/PG4 — no functional reason for 3/4 (outputs are PPS-routed),
  moved to the lowest generators; the ADC2 audio trigger points at PG1.
- **Master clock CLK5 = PLL1 VCO Divider = 800 MHz** (`CLK5CON=0x29700`, `CLOCK_GENERATOR_5`), selected
  via `PCLKCON` MCLKSEL. **MPER 66651 → 192.000 kHz** carrier (matches the `.bak`).
- **Verified HRPWM frequency formula.** In High-Resolution mode (HREN=1) the module's internal PLL
  locks to `pwm_master_clk` (CLK5 = 800 MHz) and **slices it into 16** (datasheet §16.2 → 78.125 ps
  LSB, 12.8 GHz effective). The clock divider (`PCLKCON` DIVSEL) does **not** apply in HREN mode. Like
  any PWM period register, the counter adds one period tick — but in the **coarse** (800 MHz) domain,
  i.e. **+16 fine counts**:
  `F_pwm = 12.8 GHz / (MPER + 16)`. Check: `12.8e9 / (66651 + 16) = 191,999 Hz` = MCC's reported
  191.999 kHz. So MCC is exact — enter 192000 Hz and it back-solves `MPER = round(12.8e9/192000) − 16
  = 66651`. (Earlier "66667 is closer" was wrong: it dropped the +1 coarse-count term and would give
  191.95 kHz.)
- **Phase-lock:** PG1 free-runs (SOCS self-trigger); **PG2 is SOC-triggered from PG1** (SOCS=PG1) so
  L/R stay aligned — same topology as the `.bak` (PG4 slaved to PG3).
- **ADC sample trigger:** wired in the audio-ADC step below (`PG1EVT1` ÷4 → 48 kHz).
- **App role (in scope, deferred port):** audio **output** is wanted — real-time **pass-through** of
  the sampled input to the PWM duty, with optional **overlay/mixing** (e.g. beat clicks off the beat
  frame, or stored PCM). Mix must **saturate** (not wrap) into the 20-bit duty; overlay buffers are
  fixed-size in flash / static RAM (no malloc). FFT beat detection shares the 48 kHz budget but runs
  per-frame, so there's headroom — confirm timing at port. Port must also call `PWM_HS.Enable()`
  (generators init with `ON=0`).

### 2026-07-30 — ADC5 pot MCC config (config only, no app read port)

- Added the sensitivity pot on the shared ADC core: **ADC5, channel `ADC_POT` on AD5AN0**,
  software-triggered, single-sample, single-ended, 12-bit integer (`AD5CH0CON1=0x3F0001`). Polled
  (IRQSEL set but IEC left disabled). `ADC5_Initialize()` powers the shared core and waits `ADRDY`;
  wired into `SYSTEM_Initialize()`. AD5AN0 is fixed-function analog, so no PPS/TRIS — `pins.c` untouched.
- **ADC clock on the external tree, not FRC.** MCC's first pass regressed **CLK6 (the ADC clock,
  `CLOCK_GENERATOR_6`) to 8 MHz FRC**; restored it to **PLL1 Out = 200 MHz** (`CLK6CON=0x29500`,
  `CLK6DIV=0`) to match the pre-MCC `.bak` baseline and the "everything off the external POSC→PLL"
  intent. TAD = 5 ns. This also settles the audio-ADC-clock question ahead of time: ADC2 (audio) rides
  the same CLK6, so it inherits the fast clock the `.bak` used for oversampling throughput — no clock
  rework at the ADC2 step.
- **Sample time:** `.bak` used SAMC 15 (~75 ns @ 200 MHz); MCC only offers half-TAD steps, so picked
  **SAMC 62.5 TAD ≈ 312.5 ns** — no reason to sample the high-impedance pot fast, longer window is
  strictly better for settling. Functionally equivalent read to the `.bak`.
- **App read port deferred.** When the pot read comes over: `ADC5_SoftwareTriggerEnable()` →
  `while(!ADC5_IsConversionComplete(ADC_POT)){}` → `ADC5_ConversionResultGet(ADC_POT) & 0x0FFF`.

### 2026-07-30 — SPI1 + T1S control pins MCC config (transport only, no driver port)

- Added SPI1 as the T1S MAC-PHY host link. `config[0]` (`T1S_CONFIG`): **12.5 MHz** (`SPI1BRG=3`,
  100 MHz std peripheral ÷ 2·(3+1) — closest to the 12 MHz the SAMD nodes use, under the LAN865x
  25 MHz cap), **mode 0** (CKP=0/CKE=1 → CPOL0/CPHA0), 8-bit, MSB-first, master.
- Multi-config host driver: `SPI1_Initialize()` leaves the module **OFF**; the driver must call
  `SPI1_Open(0)` to apply `config[0]` and set `ON=1` before transfers.
- T1S control pins generated alongside: SCK1→RE10 (RP75), SDO1/MOSI→RG4 (RP101), SDI1/MISO→RG9;
  **T1S_CS**→RE5 (manual GPIO, idles high / deasserted), **T1S_RST**→RA15 (GPIO out, held low = in
  reset at init), **T1S_IRQ_N**→RE2 (change-notice **falling-edge** IRQ, weak callback stub;
  `interrupt.c` CNEI priority 1). `SPI1_Initialize()` wired into `SYSTEM_Initialize()`.
- Driver port deferred. **Watch on port:** IRQ_N is level-low on the MAC-PHY but wired as edge CN —
  keep the OA TC6 "service until IRQ deasserts" loop so a held-low line can't stall on a missed edge.

### 2026-07-30 — UART2 MCC config for CLI (transport only, no app port)

- Added UART2 for the shared CLI (same `embedded-cli` core as guitar/lemmy/lightshow, which all run
  115200 8N1). MCC settings: async **8N1**, **115200** (fractional BRG 868 @ 100 MHz — matches UART1),
  TX+RX, **interrupt-driven with ring buffers** (TX 256 B, RX 128 B). printf redirect left **off** so
  it doesn't collide with UART1's generated `write()`.
- Pins (512 map): **U2TX → RH0** (RP113, TRISH output, LATH0 idles high), **U2RX → RD10**
  (RPINR13.U2RXR=0x3B). No conflict with UART1 (RH1/RD1), LEDs, or switches.
- Interrupts: `interrupt.c` sets U2 RX/TX/error/event priority 1; all four ISRs are defined in
  `uart2.c`; `UART2_Initialize()` wired into `SYSTEM_Initialize()`.
- **App port deferred.** When `cli.c` + `embedded-cli` come over from the SAMD nodes, shim the
  transport: `writeChar` → `UART2_Write(c)` (queues to TX ring), service loop →
  `while(UART2_IsRxReady()){ UART2_Read(); }`. Note `UART2_Write` busy-waits if the TX ring fills.

### 2026-07-30 — UART1 MCC config (transport only, no app port)

- Added UART1 in MCC: async 8N1, **115200** (fractional BRG 868 @ 100 MHz peripheral, 115207 actual —
  matches `.bak`), TX+RX, polled (no interrupts). printf-redirect enabled → `write()` generated in
  `uart1.c`. Wired into `SYSTEM_Initialize()`; `uart1.c` added to the descriptor fileset.
- Pins (512 map, MCC-computed codes): **U1TX → RH1** (RP114, TRISH output, LATH1 idles high),
  **U1RX → RD1** (RPINR13=0x32, TRISD input). No conflict with the LEDs/switches.
- **App-level port deferred** — staying MCC-config-only for now. When we port `uart_debug.c` from
  `.bak`, delete its hand-rolled `write()` (duplicate of the generated one) and retarget `ProcessRx`
  onto `UART1_IsRxReady()`/`UART1_Read()`.

### 2026-07-30 — Dev-board LEDs + switches into the pin manager

- Added the EV74H48A dev-board GPIO to MCC: 8 discrete LEDs (RC8–RC15), an RGB LED (RD9/RD0/RD2),
  and 3 pushbuttons (RF3/RF0/RB2). LEDs active-high, switches active-low — see the Board GPIO table.
- GPIO-only change (`pins.c`/`pins.h`); gives us blink/button hooks for peripheral bring-up.

### 2026-07-30 — Clock tree (external 8 MHz → PLL 200 MHz)

- Configured the fresh MCC clock: **Primary Oscillator (external 8 MHz clock, EC mode)** → **PLL1 Out
  200 MHz**. Verified `clock.c` (`POSCMD=0`/`POSCEN`/`POSCRDY`, PLL 8→800 VCO→200), `clock.h`
  (`CLOCK_SystemFrequencyGet()=200000000`, standard peripheral = 100 MHz).
- Reproduces the `.bak` 200 MHz / 100 MHz-peripheral exactly, so the parked modules' baud/timer
  constants stay valid on migration. Both `.bak` clock hand-edits eliminated (POSCMD now generated;
  306-only OSCO release dropped).
- **CLK5 (800 MHz PWM clock) intentionally not enabled yet** — deferred to the PWM migration step; the
  800 MHz VCO is already produced by PLL1. See plan item B0.5.5.

### 2026-07-30 — MCC backed out; fresh config on the 512MPS512

- Confirmed the target part stays **dsPIC33AK512MPS512** (no change).
- Backed the old `config.mcc/` (hand-rolled app modules + the half-migrated MCC tree) out to
  `config.mcc.bak/` (git-ignored via `*.bak*`) and generated a **fresh Melody config from scratch**
  on the 512MPS512 — `system` module only (clock, config_bits, dmt, interrupt, pins, reset, traps,
  watchdog). `main.c` is back to the pristine MCC skeleton.
- Descriptor `device` now `dsPIC33AK512MPS512`; fileset rewritten to the explicit generated `system`
  sources + `main.c` (was a `**/*` glob).
- Committed this baseline; `.bak` stays out of git. App modules get re-integrated in B0.6, starting
  with UART1 → ADC1 pot → PWM audio+trigger → ADC2 audio, porting logic from the `.bak` modules.

### 2026-07-30 — Retarget to dsPIC33AK512MPS512 (step 1)

- Decided to move beatbox from the MPS306 to the **dsPIC33AK512MPS512** (EV80L65A GP DIM on the
  EV74H48A board) — see decision log. Confirmed via the Microchip catalog: EV80L65A = "dsPIC33AK512MPS512 GP DIM"; part is 200 MHz / 512 KB, same MPS line / XC-DSC as the 306.
- Step 1 (files I can edit): `device` in `.vscode/beatbox.mplab.json`; `SPEC.md` (device/board rows
  + a pin-map caveat), `README.md`, top-level `SPEC.md` (node + MCU tables), `CLAUDE.md`, and the
  `rgb_led.c` header comment. Left the MCC-owned files (`config.mcc/mcc/*`, `mcc_generated_files/`,
  the `.generated/` cmake) for the MCC regen — did **not** hand-edit them.
- Left the DFP pack entry in the descriptor unchanged (`dsPIC33AK-MP_DFP`); flagged that its version
  must include MPS512 (MCC pins the exact version on regen).
- **Recovered a lost MCC config.** `config.mcc/mcc/config.mc3` had been committed at 0 bytes — the
  26 KB Melody graph was truncated *after* the import copy (later mtime than its siblings; consistent
  with the repo's known MPLAB atomic-write behavior). Restored the original 25991-byte `.mc3` from
  `dspicguitarhero`; manifests were intact. Left its internal `device="dsPIC33AK256MPS306"` as-is —
  MCC rewrites it on the device swap; hand-editing the graph's device is unsafe. Watch for re-truncation on next MPLAB open.
- Next: DFP version check, then the MCC device swap / regen / pin re-map in the GUI (B0.5).

### 2026-07-30 — Import from dspicguitarhero

- Discussed role: brought the dsPIC33AK `Beat_Detection` firmware in as `firmware/beatbox/`. Settled
  on **pure T1S beat-source publisher** (position cmds → lemmy, beat frame → lightshow), file copy
  without history.
- Copied `config.mcc/` (sources + `mcc/` + `mcc_generated_files/`), `cmake/` (minus `.generated/`),
  `.vscode/` (rewrote `settings.json` for macOS/XC-DSC). Dropped
  `.git`, `_build/`, `out/`, `captures/`, `MPLABXLog.xml`, `.mcp.json`, source `.claude/`, and
  `referenceProjectForWS2812Inputs/`.
- Cleaned version-changelog comments from `beat_detect.c` and `main.c` into this journal.
- Renamed the MPLAB project `Beat_Detection` → `beatbox` (descriptor + settings + MCC association;
  see decision log).
- Wrote `SPEC.md`, this journal, `README.md`; registered beatbox in top-level `SPEC.md` node table
  and `CLAUDE.md` journal list.
- Left standing for the next session: B1 pure-publisher rework and the two protocol designs (lemmy
  position commands, lightshow beat frame).
