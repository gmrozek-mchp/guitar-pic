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
- [ ] **B0.6 — Re-integrate the keep-set peripherals into the fresh MCC config.** Starting point is now
      a bare `system`-only Melody config; the app modules live in `config.mcc.bak/`. Bring back only
      what beatbox *keeps*; do **not** re-add `servo`/`rgb_led`/`ws2812` (slated for removal at B1).
      Workflow per peripheral: add + configure in Melody → regenerate → port the `.bak` module's logic
      onto the generated API → copy the file into `config.mcc/` and add it to the descriptor fileset.
      Order (independent → coupled):
  - [ ] **UART1** (`uart_debug.c`): async 8N1, 115200 (BRG 868 @ 100 MHz), TX+RX. Keep the
        `printf`→`write()` redirect but target the MCC UART1 API. Proves the flow.
  - [ ] **ADC1 pot** (inline in `main.c`): CH0 on RA3/AD1AN2, SW-triggered single 12-bit sample.
        Move the inline setup into MCC; read via the generated API.
  - [ ] **PWM audio (PG3/PG4)** (`pwm_audio.c`): high-res (16× HREN) 192 kHz, PG4 SOC-triggered
        from PG3, **PG3 EVT postscale ÷4 → 48 kHz ADC trigger**. The hard one — the ADC trigger
        must be reproduced in the MCC PWM config (it's what clocks the audio ADC).
  - [ ] **ADC2 audio** (`adc_audio.c`): CH6 (AD2AN3/RB3 L) + CH7 (AD2AN4/RB4 R), 256× oversample,
        `TRG1SRC`=PG3, complete-interrupt on CH7. Use **MCC's generated interrupt + registered
        callback** (decided) for the 48 kHz complete event; the DC-blocking HPF + passthrough +
        `BeatDetect_Process` move into the callback. Depends on the PWM trigger being in place.

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

## Session log

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
