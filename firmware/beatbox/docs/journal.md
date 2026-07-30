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
