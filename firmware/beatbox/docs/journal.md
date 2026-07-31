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
      what beatbox *keeps*; do **not** re-add `servo`/`ws2812` (slated for removal at B1). `rgb_led`
      **is kept** — the EV74H48A board's RGB LED stays a driven output (see decision log).
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
  - [x] **PWM audio** (folded into `config.mcc/src/audio.c`): high-res (16× HREN) 192 kHz on
        **PG1/PG2** (was PG3/PG4 in the `.bak`), PG2 SOC-triggered from PG1, **PG1 EVT postscale
        ÷4 → 48 kHz ADC trigger** (`PG1EVT1` `ADTR1EN1`+`ADTR1PS=1:4`+`PGTRGSEL=TRIGA`, `PG1TRIGA=0`).
        MCC config done; **app port landed** — audio-out is the passthrough half of `audio.c` (idle
        both channels mid-scale, `PWM_Enable()` to start PG1's ADC trigger, per-sample
        `PWM_DutyCycleSet` + `PWM_SoftwareUpdateRequest`).
  - [x] **ADC2 audio → ADC4** (folded into `config.mcc/src/audio.c`): built on **ADC4 CH0
        (`ADC_AUDIO_L`/AD4AN0) + CH1 (`ADC_AUDIO_R`/AD4AN1)** (was ADC2 CH6/CH7), 256× oversample,
        `TRG1SRC`=PWM1 (PG1), complete-interrupt on CH1 (higher channel) at priority 6. MCC config
        done; **app port landed as a passthrough** — `ADC4_ChannelCallbackRegister` callback reads
        L (`ADC4_ConversionResultGet`) + R (callback arg), normalizes, mirrors to the PWM DACs.
        HPF + `BeatDetect_Process` deferred to the beat-detect port (kept out to prove I/O first).
  - [x] **RGB LED** (`config.mcc/src/rgb_led.{c,h}`): three SCCP in edge-aligned buffered PWM —
        **SCCP1→RD9 (R), SCCP2→RD0 (G), SCCP3→RD2 (B)** via PPS (OCM1/2/3), R/G confirmed on the
        bench (the `.bak`'s G=SCCP1/R=SCCP2 was backwards for the EV74H48A). App keeps MCC's
        `SCCPn_PWM_Initialize` (mode + `OCAEN` + PPS + enable, from `SYSTEM_Initialize`) and
        `RGB_LED_Initialize()` overrides only the prescale + period MCC leaves wrong (`TMRPS=1:1`,
        `CCPxPR=0xFFFF`): `Disable → CCPxCON1bits.TMRPS=1:16 → PeriodSet(6250) → Enable` = exactly
        1 kHz at Fcy/16. `RGB_LED_Set(r,g,b)` takes **8-bit** levels, scaled `level*PERIOD/255` into
        `SCCPn_PWM_DutyCycleSet` (CCPxRB, buffered). Exercised over the CLI: `rgb <r> <g> <b>` /
        `rgb off`. Called from `main()` after `SYSTEM_Initialize`.
- [x] **B3 — T1S-on-dsPIC follower port.** Up on hardware: EV74H48A syncs as PLCA follower **id 5**
      against marvin, `t1s` reports `link: up`, `synced: yes`, `chipRev: 2`, 0 errors. Ported the SAMD
      nodes' OA-TC6 follower to dsPIC/XC-DSC + MCC: new `config.mcc/src/{tc6-conf.h,
      t1s_follower.{c,h}}`, wired into `main.c`, `t1s` CLI command (+ `t1s id`/`t1s plca` diagnostics),
      and `cmake/beatbox/default/user.cmake` picking up the follower + vendored `tc6.c`/`tc6-regs.c`.
      Blocking SPI1 + GPIO CS + `T1S_RST`/`T1S_IRQ_N` (CN IRQ) + TMR1 ms clock; 500 ms presence
      heartbeat (node_type 6). Publish path to lemmy/lightshow is B4.
- [~] **B1 — Data model / producer.** Lightshow half landed: `config.mcc/src/publish.{c,h}` maps each
      `BeatFrame` → an 8-byte `LightshowFrame` (see decision log), wired into `main.c`, observable via
      the `show` CLI command. Still pending: the lemmy position-command data model (needs the ported
      choreography layer), formally retiring the parked `servo`/`nod_engine`/`ws2812`, and the T1S
      transport for both payloads (B4).

## Open questions

- **lemmy command protocol.** What does beatbox send lemmy — absolute neck/jaw angles, or a
  higher-level pose/energy the puppet interprets? Ethertype + payload? Shared design with lemmy,
  which drops its own beat-driven nod logic to become a position follower. Resolve at B1/B2.
- **XC-DSC path on macOS.** `.vscode/settings.json` clangd path was set to
  `/Applications/microchip/xc-dsc/v3.31/bin/xc-dsc-clangd` (Mac analogue of the source's Windows
  path); verify against the actual install.

## Decision log

| Date | Decision | Rationale |
| 2026-07-31 | **lightshow beat frame = fixed-rate 8-byte `LightshowFrame`, produced by a new `publish.{c,h}` outbound-payload layer.** Resolves the "lightshow beat frame" open question (both parts: field set + cadence). Fields, all normalized so the consumer drives LEDs directly: `seq` (frame counter, wrap-at-256 drop detection), `energy` (raw_env 0–10000 → 0–255), `bass` (flux_bass 0–1000 → 0–255), `treble` (flux_full 0–1000 → 0–255), `kick` (kick_strength → 0–255, 0 when no kick), `flags` (bit0 bass onset · bit1 mid/high onset · bit2 kick · bit3 big-beat=any-strong · bit4 bass-dominant), and **reserved** `tempo`/`phase` (0 until the tempo/BPM+phase layer lands). Cadence = one frame per `BeatFrame` (~23.4 Hz), i.e. fixed-rate, not event-only. `Publish_Update(&f)` runs in `main.c`'s existing `Beat_HasFrame()` block; `Publish_GetLightshowFrame` snapshots (a `show` CLI command dumps it), `Publish_HasLightshowFrame` is a consume-once hook for the future T1S sender. Scaling uses explicit `uint32_t` casts (`int` is 16-bit on dsPIC33A). **T1S transport (ethertype + marshalling + send) deferred** — this is the producer only. | Splits the wire payload from the internal `BeatFrame` (12 B of mixed-range features): lightshow gets a few 0–255 bytes it can use as brightness/color/pulse without re-scaling, and beatbox's detection internals stay private. A reactive frame (band energy + beat pulses) needs no tempo/phase, so a first show ships now; reserving the two phase bytes keeps the wire size stable when the predictive layer lands (additive, no re-cut). A dedicated `publish` module is the B1 "data model the bus carries" — lemmy position commands join it later, keeping all outbound-payload mapping in one place. `seq` lets the consumer detect drops once it's on the lossy bus. |
|------|----------|-----------|
| 2026-07-31 | **Beat-detect port: decouple via a stereo audio sample callback, split feature extraction from beat decision, scope to events+features only.** `audio.c` gains `Audio_SampleCallbackRegister(void(*)(float left, float right))` (mirrors MCC's `ADC4_ChannelCallbackRegister` idiom); the 48 kHz ISR feeds the post-HPF normalized L/R pair to a single registered consumer (NULL detaches; aligned function-pointer store is atomic on this 32-bit core so the ISR-read vs registration-write race is a non-issue). Two new modules: `beat_detect.{c,h}` holds the DSP **verbatim** from `.bak` (512-pt radix-2 FFT, DS_RATE=4 → 12 kHz → ~23.4 Hz frames, peak envelope, bass/mid-high spectral flux, bass-dominance, kick detector, 64-bin display spectrum) — every constant preserved (BASS_BIN 1–10, KICK_BIN 2–4, KICK_ATTACK 0.7, etc.); `beat_engine.{c,h}` lifts the beat *decision* out of `.bak/main.c` (per-band running-average delta threshold + cooldown + envelope noise gate) and publishes a `BeatFrame`. `beat_engine`'s `on_samples` sums L+R to mono for analysis (per user: audio callback stays stereo, the beat detector decides mono). Dropped the GUI-only frame-timing plumbing (`sample_counter`/`out_frame_us`/`GetFrameTime`) and all "keep build happy" stubs. `main.c` reduced to `Beat_Initialize()` + `Beat_Tasks()`; `beat` CLI command prints the latest frame. **Scope deliberately limited to events + features — tempo/BPM/phase deferred** (they are downstream consumers of these events, so additive later). | Unwinds the `.bak` three-module cross-call tangle (`adc_audio`→`pwm_audio`+`beat_detect`) into a clean layered dependency: `audio` (I/O) → `beat_detect` (features) → `beat_engine` (decision) → tempo → phase, each additive. Preserving the DSP byte-for-byte keeps the proven detection behavior unchanged while the structure improves; the callback breaks the audio↔analysis coupling the same idiomatic way MCC exposes the ADC event. Stereo-in/mono-in-detector keeps the option open to use per-channel info later without touching `audio.c`. Watch-item: confirm XC-DSC cmake links libm (`sqrtf`/`cosf`/`sinf` in `beat_detect.c`) — add `target_link_libraries(... m)` only if undefined-reference errors appear. |
| 2026-07-30 | **Audio app port: passthrough first, one `audio.c` module, beat detect later.** The `.bak` split the audio path across three modules that cross-called each other (`adc_audio.c`'s `_AD2CH7Interrupt` normalized/HPF'd, then reached into `pwm_audio.c` to output *and* into `beat_detect.c` to analyze). First step re-integrates only the input→output half, in a single `config.mcc/src/audio.{c,h}`: `Audio_Initialize()` idles PG1/PG2 mid-scale, `PWM_Enable()`s them (MCC leaves generators `ON=0`), and registers an ADC4 channel callback; the callback fires on CH1 (right), reads CH0 (left, already latched) via `ADC4_ConversionResultGet`, normalizes both about 32768, and mirrors straight to the PWM DACs (`PWM_DutyCycleSet` + `PWM_SoftwareUpdateRequest`, PG1=L/RB8, PG2=R/RB9). No HPF, no FFT yet. A `audio` CLI command reports the latest raw L/R sample for bench diagnosis. | Proving the ADC-in → PWM-out path in isolation removes the biggest unknowns (MCC ADC4 callback wiring, PG1-triggered 48 kHz clocking, DAC output) before layering DSP on top, and collapsing the parked three-module tangle into one owner is exactly the "muddled interaction" cleanup wanted. HPF/`BeatDetect_Process` slot back into the same callback once I/O is confirmed. |
| 2026-07-30 | **Core OA-TC6 lib ports as-is; only the per-node glue is dsPIC-specific.** Answers the "T1S on dsPIC33AK" open question. `tc6.c`/`tc6-regs.c` compile under XC-DSC unchanged (the vendor ships a dsPIC33AK example), same vendored files the SAMD nodes build. beatbox's `t1s_follower.c` mirrors guitar's structure/public API and swaps the platform layer: blocking SPI1 byte-loop (vs guitar's async SERCOM), CN IRQ on `T1S_IRQ_N` (vs EIC), TMR1 ms tick (vs SYSTICK), MCC pin macros (`T1S_RST`/`T1S_CS`), UART2 logging (vs SERCOM1), and **no actuator GPIO** (beatbox publishes, doesn't actuate — guitar's `FRET_*`/`STRUM`/button API dropped). | Keeps beatbox on the exact same proven TC6 transport as guitar/lemmy/lightshow with zero lib divergence, so protocol fixes stay shared. Pins were already assigned in the SPI1/T1S MCC step; the only real porting surface was the ~5 platform primitives above. |
| 2026-07-30 | **`t1s` CLI reports real on-bus state, not just local init.** The MAC-PHY bring-up completes purely over SPI with nothing on the wire, so `initDone` and the OA-TC6 config-sync footer bit (`TC6_GetState` `synced`) both assert with no cable/coordinator — the old `link: up` / `synced: yes` lines lied. Split the state: `T1SFollower_IsInitialized()` = local config done; `T1SFollower_IsConnected()` now = **PLCA operating** (PLCA_STATUS bit 15), refreshed by a 250 ms background register read cached in `s_plca_op`. The presence heartbeat is gated on PLCA-operating (a follower has no transmit slot without the coordinator beacon; sending earlier queues a frame that never drains and stalls `s_hb_busy`). CLI now shows `chip` (rev / absent), `init`, `plca` (operating / idle-no-coordinator), `cfgsync` (relabeled so it stops masquerading as connectivity), credits, rx, errors. | PLCA_STATUS is the only field that requires a beacon on the wire, so it's the honest connectivity signal. Same trap exists in the guitar/lemmy/lightshow followers (they mirror this code) — carry this fix to them when each is next touched. |
| 2026-07-30 | **Blocking SPI: settle `TC6_SpiBufferDone` inline in `TC6_CB_OnSpiTransaction`, not deferred to the service pump.** SPI1 is a blocking 8-bit driver, so `TC6_CB_OnSpiTransaction` runs the whole transfer inline (`T1S_CS_SetLow` → byte-loop `SPI1_ByteExchange` bridging TC6's separate pTx/pRx → `T1S_CS_SetHigh`), then calls `TC6_SpiBufferDone(tc6instance, true)` before returning. **A first cut deferred that call to `service_pump()` (after `TC6_Service()` returned) — it deadlocked at boot:** `TC6Regs_Init`→`DoInitialization` drives the LAN8651 bring-up by pumping `TC6_Service()` in its *own* `while` loops (tc6-regs.c:325–414), spinning until read results (e.g. `chipRev`) post — but those only post via `TC6_SpiBufferDone`, which never ran because `service_pump` isn't reached during init. Freeze right after the `TC6_Init` log; `chipRev` stuck at `0xFF`. Completing inline lets those internal spins make progress. | Verified reentrancy-safe: `TC6_SpiBufferDone` only advances the op queue, resets `currentOp`, and flags need-service (guards with `intContext`, no re-entry into `serviceControl`/`serviceData`, no nested transaction); the library advances the send-stage *before* invoking `OnSpiTransaction` (tc6.c:718/735), so inline completion is functionally identical to the async DMA-done callback firing — just synchronous, which is exactly right for a blocking driver. Confirmed on hardware: link syncs, 0 errors. |
| 2026-07-30 | **TMR1 1 ms tick is the follower time base** (via MCC). `TMR1_TimeoutCallbackRegister(tick_cb)` increments a `volatile uint32_t s_ticks_ms`; `TC6Regs_CB_GetTicksMs()`/reset-pulse delays read it. 32-bit reads aren't atomic on this core, so `now_ms()` double-reads until two samples agree (avoids a torn read at the low/high-word carry every ~49 days — cheap insurance). Timer is already started by `SYSTEM_Initialize`, so the follower only registers its callback. | The lib needs a monotonic ms clock for its timeout logic + the heartbeat cadence; TMR1 was the MCC-idiomatic choice (user added it) vs. a free-running SCCP or CPU-cycle counter. |
| 2026-07-30 | **beatbox heartbeat `node_type = 6` (beat source).** New code, next free after 1=detector, 2=guitar, 3=controller, 4=animation(lemmy), 5=lightshow (`docs/t1s-podl-link.md` §7.2). **Follow-up:** marvin's heartbeat decode + `docs/t1s-podl-link.md` §7.2 table need the code 6 label added (same follow-up lemmy=4/lightshow=5 carried); until then marvin still lists beatbox via src-MAC (id 5). Note: node **id** 5 and node **type** 6 differ — id is the PLCA slot, type is the role. | Keeps the presence protocol's role enum contiguous and lets marvin's `nodes` view label beatbox once decode lands. |
| 2026-07-30 | **Keep the board's RGB LED as a driven output (rgb_led → keep-set), but let the app own period/prescale/duty.** The EV74H48A has an RGB LED (RD9/RD0/RD2); drive it with the three SCCP in edge-aligned buffered PWM (OCM1/2/3), matching the `.bak` color→module map (G=SCCP1, R=SCCP2, B=SCCP3). MCC's generated config is kept only for **mode + `OCAEN` + PPS pin routing** — the parts it got right. **Period, prescale, and duty-scaling are set from the app driver**, not MCC. | MCC can't back-solve period+prescale from a requested frequency for the SCCP on this clock tree: at Fcy 100 MHz with the 2-bit prescaler (1:1/4/16/64, no 1:32) it emitted `TMRPS=1:1`, `CCPxPR=0xFFFF` (~1526 Hz, wrong). The `.bak`'s clean 1 kHz relied on Fcy=200 MHz (`200e6/64/3125`); at 100 MHz you can't hit exactly 1 kHz *and* a 3125 full-scale. Resolved by **decoupling app duty from the raw compare value** — `RGB_LED_Set` takes abstract brightness and scales to whatever period the driver picks (e.g. 1:16 + `PR=6250` = exactly 1 kHz), so the period stops being an app constant. App overrides at init while the module is off (`Disable` → set `TMRPS` → `PeriodSet` → `Enable`); no generated files edited (repo rule), no fighting the MCC frequency field. |
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

### 2026-07-31 — lightshow beat-frame producer (B1, producer only)

- **Built the outbound-payload layer.** New `config.mcc/src/publish.{c,h}`: `Publish_Update(&f)`
  maps each `BeatFrame` → an 8-byte `LightshowFrame` (`seq`, `energy`, `bass`, `treble`, `kick`,
  `flags`, reserved `tempo`/`phase`) — the compact bus payload for lightshow, normalized to 0–255
  so the consumer drives LEDs without re-scaling. `Publish_GetLightshowFrame` snapshots;
  `Publish_HasLightshowFrame` is a consume-once hook the future T1S sender will use. See the
  decision-log row for the field/scaling rationale and the "reserve phase bytes" choice.
- **Wired in + observable.** `Publish_Initialize()` in `main()`, `Publish_Update(&f)` added to the
  existing `Beat_HasFrame()` block in the loop (alongside the RGB indicator), source added to
  `cmake/beatbox/default/user.cmake`. New `show` CLI command dumps the latest `LightshowFrame` for
  bench verification (7 bindings now, under the max 8).
- **Scope:** producer only, per the ask. The T1S transport (ethertype + marshalling + send) is
  still B4; `tempo`/`phase` stay 0 until the deferred tempo/BPM+phase layer lands. Resolved the
  "lightshow beat frame" open question.

### 2026-07-31 — Beat-detection port (features + events)

- **Dropped the noise gate tried the prior session.** Too sensitive (cut real audio) and it
  didn't kill the hiss anyway — the background noise is in *both* the ADC front-end (~60–100pp,
  source-independent) *and* the PWM output stage (hiss persists with the DAC held at mid-scale,
  MCP662 op-amps, no mute pin). Passthrough is monitor/scratch quality only, not clean enough to
  feed the main speakers without a hardware redesign. See the audio decision-log rows.
- **No LPF added.** 48 kHz + the ADC's 256× oversampling decimation handles anti-aliasing; beat
  detection needs only ~1–4 kHz. Any band-limiting belongs inside the FFT stage later, not in the
  analog/ISR path.
- **Ported beat detection as two modules, DSP verbatim.** `audio.c` now offers a stereo
  `Audio_SampleCallbackRegister` fed the post-HPF ±1 L/R pair from the ISR. `beat_detect.{c,h}` =
  the `.bak` FFT/feature DSP unchanged (512-pt radix-2, DS_RATE 4, envelope, bass/mid-high flux,
  bass-dominance, kick detector, 64-bin spectrum); `beat_engine.{c,h}` = the beat *decision* lifted
  out of `.bak/main.c` (per-band delta-over-average threshold + cooldown + envelope gate), summing
  L+R to mono in `on_samples`, publishing a `BeatFrame`. `main.c` calls `Beat_Initialize()` /
  `Beat_Tasks()`; dropped the GUI-only frame-timing plumbing and the old build-happy stubs. See
  decision log.
- **CLI:** added `beat` — prints the latest frame (env, bass/full flux, bass-vs-treble, the three
  beat/kick flags + kick strength) for bench verification.
- **Onboard RGB LED = live beat indicator.** `main` consumes each published `BeatFrame` and drives
  the LED: kick=white, bass=red, mid/high=blue, strong beats full brightness, decaying ~0.75/frame.
  The `.bak` drove RGB mostly off the phase oscillator (deferred) + a big-beat green flash; this
  reuses only the events we have now, as a visual check of beat detection. Note it continuously
  drives the LED, so the `rgb` CLI command is overwritten each frame while beats run.
- **Committed the port** as `411b3e0` (beatbox files only; unrelated marvin/docs/hardware changes
  left out of scope).
- **Documentation.** Wrote `docs/beat-detection.md` — the durable "how the signal chain works"
  reference: the layered pipeline, the **no-RTOS cooperation model** (ISR vs super-loop, the
  `volatile` consume-once handshakes, why no locks are needed, the single-buffer main-loop-latency
  constraint), the DSP (downsample/FFT/flux/envelope/kick with a bin→frequency table + tuning
  constants), the beat decision, the `BeatFrame` interface, and the planned B4 bus mapping.
  Rewrote `README.md` into a real project README (was stale — claimed audio/beat were parked in
  `.bak`); added the CLI table + module map + signal-chain link.
- **Found while documenting (verbatim-ported quirk):** `beat_engine`'s envelope noise gate is
  currently **inert** — `NOISE_GATE_BYPASS_DELTA` (250) equals `BEAT_DELTA_THR` (250), so any delta
  large enough to fire already bypasses the gate; the flux-delta threshold alone gates. Documented
  as-is (not silently changed). To make the envelope gate active, lower the bypass delta or raise
  the fire threshold. **Follow-up:** decide whether the gate should do anything, now that the input
  noise floor is characterized.
- **Refreshed `SPEC.md`** to current reality: status header (beat detect + T1S up), MAC-PHY row,
  the pin map (rewritten to the actual MPS512/MCC pins from `pins.{h,c}` — audio ADC4 AN0/AN1, PWM
  RB8/RB9, RGB RD9/RD0/RD2, T1S RA15/RE5/RE2 + SPI1 RG4/RG9/RE10, UART2 RH0/RD10 @115200), the
  signal chain (current modules, delegating depth to `beat-detection.md`), the module table (current
  set + a parked-`.bak` note), interfaces (present `BeatFrame`/CLI/heartbeat vs planned bus), and
  the milestones (B3 done ahead of B1/B2; B0.5/B0.6 sub-steps; beat-detect port).
- **Scope:** events + features only. Tempo/BPM and phase are deferred as downstream consumers of
  these events (confirmed cleanly additive), then B4 T1S publish.
- **Build watch-item:** confirm XC-DSC cmake links libm for `beat_detect.c` (`sqrtf`/`cosf`/`sinf`);
  add `target_link_libraries(... m)` only if undefined-reference errors surface. Awaiting user
  build/flash + bench check that env/flux track live audio.

### 2026-07-30 — Audio passthrough app port (ADC4-in → PWM-out)

- **Scoped the audio port to a passthrough proof first.** Rather than porting the parked
  `adc_audio.c` + `pwm_audio.c` + `beat_detect.c` tangle in one go, landed just the input→output
  path to prove the ADC-in / PWM-out hardware works on the fresh MCC config. See decision log.
- **One module, `config.mcc/src/audio.{c,h}`.** `Audio_Initialize()`: idle PG1/PG2 at mid-scale
  (`AUDIO_PWM_CENTER=33325`, MPER/2), register the ADC4 channel callback, then `PWM_Enable()` —
  MCC's `PWM_Initialize()` configures the generators but leaves them `ON=0`, and PG1's enable is
  what starts the 48 kHz ADC trigger. The callback fires on CH1 (`ADC_AUDIO_R`, the only enabled
  IRQ, at priority 6), reads CH0 (`ADC_AUDIO_L`, already latched from the same trigger) via
  `ADC4_ConversionResultGet`, normalizes both about 32768, and mirrors to the DACs via
  `PWM_DutyCycleSet` + `PWM_SoftwareUpdateRequest` (PG1=L/RB8, PG2=R/RB9). No HPF, no FFT.
- **Cleanups over the `.bak`:** duty write drops the manual `& 0x000FFFFF` (generated
  `PWM_DutyCycleSet` masks); ADC/PWM register poking is gone (MCC owns config); the ISR is a
  registered callback, not a hand-written `_AD4CH1Interrupt`.
- **CLI:** `audio` prints the latest raw L/R sample (0–65535, mid 32768) **and** a peak envelope
  (min/max/peak-to-peak per channel) tracked in the ISR since the last call. Instantaneous samples
  read at CLI speed badly undersample the waveform, so peak-to-peak is the honest swing measure.
- **Wiring:** `main.c` calls `Audio_Initialize()` after `RGB_LED_Initialize()`; `audio.c` added to
  `cmake/beatbox/default/user.cmake`. Loop unchanged (path is fully ISR-driven).
- **Confirmed on hardware — passthrough works** (output on headphones). Bench findings:
  - **Range is healthy:** full-volume input reaches **~30000 peak-to-peak** (~46% of full scale) —
    the earlier "27000–29000" impression was just instantaneous CLI reads undersampling the
    waveform, not a small signal. The DC bias idles near ~28000 (≈1.4 V), not the 32768 mid-rail —
    a front-end biasing property; the deferred DC-block HPF is what recenters it.
  - **Background noise:** up to **~100 pp** with nothing connected / nothing playing (noise floor
    ≈0.15% FS). Address if practical (candidates: DC-block HPF, noise gate, front-end review).
  - **Output pop — accepted (no firmware fix possible).** A 200 ms DC soft-start (and a symmetric
    soft-stop before CLI reset) were both tried and **removed** — neither meaningfully reduced the
    pop. The output stage is **MCP662 op-amps** (plain dual op-amp, no `SHUTDOWN`/enable pin) with no
    muting switch on the board, so there's no hardware hook to sequence. The dominant transient is
    the analog turn-on (supply ramp + op-amp bias settling to its operating point), which happens as
    power comes up — before firmware runs and outside the digital sample path — so ramping the duty
    can't reach it. Conclusion: without added mute hardware (a shunt FET / analog switch on the
    output, one GPIO, released after settle) the power-on pop can't be cleanly fixed; it's a one-time
    cosmetic tick on a node that boots once and stays on. Accepted as-is; audio path keeps no ramp.
  - **DC-block HPF landed** (first-order, ~20 Hz @ 48 kHz, ported from the parked `adc_audio.c`).
    The line-in idles ~29000 (front-end bias, not the 32768 mid-rail) — expected for an AC-coupled
    input, not a fault. `normalize()` assumes 32768, so that offset was riding through as a constant
    DC term at the DAC; the HPF removes it adaptively so the **output** idles at true mid-scale
    whatever the input bias is, and strips sub-audible rumble. Runs on every sample (even during the
    soft-start ramp) so its state is settled before passthrough uses it — no handover transient.
    Peak/`audio`-CLI reporting now shows **both** raw and filtered (post-HPF, re-expressed in
    count units, mid 32768) sample + peak-envelope lines.
  - **Background noise is in both stages, not just the input.** Raw and filtered pp are the same
    (~60–70, up to ~100) whether or not a source is connected, so part of it is the ADC/front-end
    floor. But hiss persists audibly even with the output frozen at mid-scale (gate closed → PWM
    duty constant), so the PWM output stage adds noise of its own.
  - **Noise gate tried and dropped.** A hysteresis gate (envelope-driven, smooth attack/release)
    was too sensitive — it dropped real audio — and, because output-stage hiss survives with the
    DAC held constant, it couldn't clean up idle noise either. Removed; passthrough is back to
    HPF-only. Verdict: the passthrough isn't clean enough to drive the main speakers (the original
    hope: passthrough + overlay audio on demand) without a hardware redesign of the output stage.
    Treat the audio path as beat-detect input + a scratch/monitor output, not a speaker feed.
- Next: `BeatDetect` port (FFT) onto the filtered mono sum.

### 2026-07-30 — RGB LED app port (B0.6 rgb_led done)

- **Ported `rgb_led` onto the generated SCCP PWM API.** New `config.mcc/src/rgb_led.{c,h}` +
  `RGB_LED_Initialize()` call in `main.c`, added to `cmake/.../user.cmake`. Build clean (`.elf`/`.hex`
  produced), no warnings on the new files. Not yet run on hardware (user builds/flashes).
- **Kept MCC's `SCCPn_PWM_Initialize`, overrode only what it got wrong.** MCC's generated init
  isn't a working PWM setup on this clock tree (1:1 prescale, `CCPxPR=0xFFFF`). Rather than re-init
  the channels from the app, `RGB_LED_Initialize()` does the minimal fix: `SCCPn_PWM_Disable()` →
  poke `CCPxCON1bits.TMRPS = 1:16` (no generated setter) → `SCCPn_PWM_PeriodSet(6250)` →
  `SCCPn_PWM_Enable()`, giving exactly 1 kHz at Fcy/16. Everything else (mode, `OCAEN`, PPS) stays
  MCC's. Matches the RGB decision-log row's app-owns-timing plan.
- **8-bit RGB API.** `RGB_LED_Set(r,g,b)` takes 0–255 per channel, scaled `level*RGB_PWM_PERIOD/255`
  into `SCCPn_PWM_DutyCycleSet` (buffered CCPxRB). Colour→channel map corrected against the bench:
  **R=SCCP1, G=SCCP2, B=SCCP3** (the `.bak`'s G=SCCP1/R=SCCP2 was swapped for the EV74H48A wiring).
- **CLI:** added `rgb <r> <g> <b>` / `rgb off` (with a `parse_u8` helper mirroring lightshow's `led`).

### 2026-07-30 — T1S follower up on hardware (B3 done); boot-freeze fix

- **On the wire.** EV74H48A syncs as PLCA follower **id 5** against marvin: `t1s` reports
  `link: up`, `synced: yes`, `chipRev: 2`, `plca: follower id=5/8`, tx credits present, 0 errors.
  B3 complete.
- **Diagnosed + fixed a boot freeze.** First build froze right after the `TC6_Init` log. Root
  cause: the blocking-SPI completion (`TC6_SpiBufferDone`) was deferred to `service_pump`, but
  `TC6Regs_Init`→`DoInitialization` runs the LAN8651 bring-up by pumping `TC6_Service()` in its own
  `while` loops (tc6-regs.c:325–414), spinning on read results (e.g. `chipRev`) that only post via
  `TC6_SpiBufferDone` — which never ran during init, so `chipRev` stuck at `0xFF` forever. Fix:
  call `TC6_SpiBufferDone(tc6instance, true)` **inline** at the end of `TC6_CB_OnSpiTransaction`
  (safe — it only advances the op queue + flags need-service, guarded by `intContext`; the library
  stages the send before the callback, so inline completion == the async DMA-done callback firing).
  Dropped the `s_spi_done_pending` flag and the deferred settle in `service_pump`. See decision log.
- **Init is synchronous now.** `TC6Regs_Init` completes the whole register sequence before it
  returns, so init is done on the first `T1SFollower_Tasks` pass (the "LAN8651 configured …" line
  prints immediately). Removed the temporary bring-up instrumentation (per-stage boot logs +
  iteration-gated liveness probe) added while localizing the freeze.
- **Made `t1s` state honest.** Caught that `link: up` / `synced: yes` asserted with nothing
  connected — both are local (init-done + config-sync footer bit) and don't need a wire. Added a
  250 ms background poll of PLCA_STATUS (bit 15 = operating) cached in `s_plca_op`; `IsConnected()`
  now means PLCA-operating and `IsInitialized()` covers the local state. Heartbeat gated on
  PLCA-operating. CLI reworked to `chip / init / plca / cfgsync / credits / rx / errors`. See
  decision log (same trap lives in the sibling followers). Not yet built/re-flashed with this change.

### 2026-07-30 — T1S follower bring-up (B3), code landed

- **Ported the OA-TC6 follower to dsPIC/XC-DSC + MCC** as PLCA follower **id 5**, mirroring
  guitar's `t1s_follower.{c,h}` with the platform glue swapped (see decision log). New app files
  under `config.mcc/src/`: `tc6-conf.h` (single-chunk config, copy of guitar's), `t1s_follower.h`
  (public API, guitar's minus the actuator/button calls), `t1s_follower.c`.
- **Platform layer:** `TC6_CB_OnSpiTransaction` = blocking `T1S_CS_SetLow` → byte-loop
  `SPI1_ByteExchange` (bridges TC6's separate pTx/pRx) → `T1S_CS_SetHigh`, then
  `TC6_SpiBufferDone` from `service_pump` after `TC6_Service` returns; `T1S_IRQ_N` CN handler sets
  need-service and `service_pump` passes `T1S_IRQ_N_GetValue()` as `no_int` (held-low guard, per the
  SPI1/T1S config watch-item); TMR1 ms tick; `SPI1_Open(0)` + `T1S_RST` reset pulse; UART2 logging;
  500 ms presence heartbeat (node_type 6). RX frames are counted for diagnostics only — no bus
  actuator.
- **Integration:** `main.c` calls `T1SFollower_Initialize()` after `CLI_Initialize()` and
  `T1SFollower_Tasks()` in the loop; `cli.c` gains a `t1s` command (status) with `t1s id` / `t1s plca`
  diagnostic subcommands and drops the stale "(planned)" role text; `cmake/beatbox/default/user.cmake`
  adds `t1s_follower.c` + vendored `tc6.c`/`tc6-regs.c` sources and the `oa-tc6-lib/libtc6/{inc,src}`
  include dirs.
- **Verified statically:** all MCC APIs referenced exist with the expected signatures (`T1S_*` pin
  macros, `SPI1_Open`/`SPI1_ByteExchange`, `UART2_Write`/`_Read`/`_IsRxReady`/`_IsTxDone`,
  `TMR1_TimeoutCallbackRegister`); all TC6 callback/API signatures match guitar's compiling source
  1:1 (incl. `TC6Regs_Init` arg list).
- **Not yet built or on hardware.** Next: MPLAB build of `beatbox_default_default_XC_DSC_compile`
  (watch for XC-DSC warnings in the vendored lib — none expected), then bench bring-up: `t1s` shows
  link up / `synced=1` once PLCA locks / plausible chipRev; `t1s id` logs OA IDVER + PHY id (lib
  wants oui 0x1F0 / model 0x1B); `t1s plca` shows `plca_status=1`; with marvin coordinating, its
  `nodes` view lists id 5 (heartbeat within ~2 s); errors don't flood (diag rate-limited) and the
  link recovers after a cable pull (`TC6Regs_Reinit`).

### 2026-07-30 — CLI on UART2 (first app-level port)

- **First app-level port** — peripheral MCC config (B0.6) is done; started wiring app code onto the
  generated drivers, beginning with the operator CLI. Lives in a new app tree `config.mcc/src/`
  (mirrors the SAMD nodes' layout).
- **Pulled `embedded-cli` verbatim** from lightshow (`third_party/embedded-cli/embedded_cli.{c,h}`,
  md5-identical across guitar/lemmy/lightshow) — static-allocation mode, no malloc. Wrote
  `cli.{c,h}` modeled on lightshow's, with a **starter command set (`info`, `reset`)**; peripheral
  commands (rgb/pot/t1s/beat) get added as those ports land.
- **Transport shimmed to the dsPIC UART2 driver** (single-byte API vs the SAMD nodes' buffer API):
  `cli_write_char` → `UART2_Write(c)` (queues to the interrupt TX ring, busy-waits only on a full
  ring), RX loop → `while(UART2_IsRxReady()){ UART2_Read(); }`. `reset` drains with
  `while(!UART2_IsTxDone()){}` then `__asm__ volatile("reset")` (dsPIC software reset — MCC's
  `reset.h` only exposes cause helpers, no SW-reset trigger).
- **Wired into `main.c`:** `CLI_Initialize()` after `SYSTEM_Initialize()`, `CLI_Tasks()` polled in
  the main loop.
- **Build:** app sources + include dirs added via a new **`cmake/beatbox/default/user.cmake`** (the
  MPLAB user-maintained cmake, included by `CMakeLists.txt` if present — same mechanism guitar uses;
  keeps app files out of the MCC-owned `.generated/` tree and the `.mplab.json` fileset). Targets
  `beatbox_default_default_XC_DSC_compile`.
- **Not yet built on hardware** — needs an MPLAB build + console check on the EV74H48A (`info`,
  autocomplete, `reset`).

### 2026-07-30 — RGB LED (SCCP1/2/3) MCC config (mode + pins only; app owns timing)

- **Reopened `rgb_led` as a keep-set peripheral** — the EV74H48A board's RGB LED (RD9/RD0/RD2) stays
  a driven output, so it needs config after all (earlier it sat in the B0.6 drop-set with servo/WS2812).
- **Three SCCP in PWM added and pin-routed correctly:** `CCPxCON1=0x5` (MOD = dual-edge buffered PWM),
  `CCPxCON2=0x1000000` (`OCAEN`), `ON=1`; PPS `RD9→OCM1` (G), `RD0→OCM2` (R), `RD2→OCM3` (B) in
  `pins.c` — matching the `.bak` color→module map. These parts of the generated config are correct
  and kept.
- **MCC can't compute the timing here.** It emitted `TMRPS=1:1`, `CCPxPR=0xFFFF` (~1526 Hz) instead of
  the requested 1 kHz — at Fcy 100 MHz with the 2-bit prescaler (1:1/4/16/64) it doesn't auto-select a
  prescale, and 1 kHz at 1:1 overflows the 16-bit period. **Decision:** app driver owns
  period/prescale/duty-scaling; `RGB_LED_Set` takes abstract brightness and scales to the driver's
  period (e.g. 1:16 + `PR=6250` = exactly 1 kHz), so the `.bak`'s 3125 magic number goes away. See
  decision log. Config left as generated; overrides happen in the deferred app port
  (`Disable` → `TMRPS` → `PeriodSet` → `Enable`).
- **B0.6 status:** all keep-set peripherals now have MCC config (audio ADC/PWM, pot, UART1/2, SPI1,
  and now RGB). Remaining beatbox work is the app-level ports + the B1 pure-publisher rework.

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
