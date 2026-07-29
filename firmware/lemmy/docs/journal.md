# lemmy — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the lemmy (animated guitar-playing puppet) firmware. Newest entries at the top. For *what lemmy is* (purpose, hardware, link, firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Bring lemmy up on the T1S bus first, then add motion.** lemmy is the **animation node**: a
PIC32CM6408PL10048 T1S PLCA *follower* (node id 6, MAC `02:00:00:00:00:06`) that will animate a
guitar-playing puppet with **two R/C hobby servos** — a **neck joint** (nod / head-bang) and a
**bottom jaw** (mouth open/close). Its primary job is to nod the head in time to the music from a
future **beatbox** node (id 5); long term the jaw may animate "talking."

The base MCC project is scaffolded and committed (unmodified generator output: clock/EVSYS/NVIC/PORT,
CMSIS+DFP, default `SYS_Initialize`/`SYS_Tasks` main loop). The immediate path mirrors the
[`guitar`](../../guitar/SPEC.md) node's G0→G1: add the T1S/CLI peripherals in MCC (L0b), then port the
`t1s_follower` + `cli` glue for follower bring-up (L1). Servo motion (L2+) comes after the link is
proven.

**Next:** L0b — SERCOM0 SPI (Mode 0), EIC EXTINT2 on `IRQ_N`=PA02, `CS`=PA06 / `RST`=PA03 GPIO,
SERCOM1 debug UART (PB00/PB01), via the mplab-mcc MCP server (no hand-edited MCC files).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-28 | **lemmy created as the *animation* node class; T1S bring-up before motion.** PIC32CM6408PL10048, PLCA follower **id 6** / MAC `02:00:00:00:00:06` (the slot reserved in [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1). Two R/C hobby servos: neck joint (nod) + bottom jaw. Phase order: L1 T1S follower (link + heartbeat + CLI) → L2 servo motion → L3 beat-driven nod from beatbox (id 5). | Greg's call: prove the node on the bus first, reusing the `guitar` follower glue + `oa-tc6-lib` (same MCU family — keeps the "OA SPI driver scales across the family" demo and minimizes bring-up), then layer motion. The puppet's animation source is a beat feed, not the guitar button bitmask, so it's a distinct node class. |
| 2026-07-28 | **T1S control pinout reuses `guitar`'s ATE_2026 map** (`CS`=PA06, `RST`=PA03, `IRQ_N`=PA02/EXTINT2; SPI SCK=PA05/MISO=PA07/MOSI=PA04; debug UART PB00/PB01). | Same MCU and same LAN8651 wiring lets the `t1s_follower` glue port over near-verbatim (only `T1S_NODE_ID` = 6 changes). Servo PWM pins are separate and fixed at L2. |
| 2026-07-28 | **Heartbeat `node_type = 4` (*animation*) proposed** for lemmy's `0x88B6` presence frame. | Existing enum is 1=detector, 2=guitar, 3=controller; lemmy is a new class. marvin's §7.2 decode + `nodes` display need to learn value 4 (marvin-side follow-up). |

---

## Open questions

- **Beat-signal command plane.** What does beatbox (id 5) send lemmy, and over which ethertype? A beat
  tick / tempo / phase? A new ethertype vs. reusing an existing one? Coupled to the (not-yet-existing)
  beatbox node's design — deferred until L3. marvin may also want to drive lemmy from its timing
  pipeline.
- **Servo PWM peripheral + pins.** TCC0 (two channels) vs. two TCs; which pins. Fixed at L2/MCC.
  Confirm 50 Hz / 1–2 ms pulse resolution off the 24 MHz clock is adequate.
- **Servo power / drive.** Separate servo rail + common ground; brown-out / inrush handling so servo
  current doesn't disturb the LAN8651 or MCU supply. Hardware, not firmware — flag at L2 bring-up.
- **Heartbeat node_type = 4** — confirm with the marvin side before it's baked into lemmy's payload.

---

## Session log

### 2026-07-28 — subproject created (scaffold + plan)

- Greg set up the base lemmy MPLAB/MCC project (PIC32CM6408PL10048) in `firmware/lemmy/`; committed as
  unmodified generator output (`lemmy: scaffold base MCC project`, commit `713a9db`) — clock/EVSYS/NVIC/
  PORT plibs, CMSIS+DFP packs, linker/startup, default main loop; workspace registers the folder and
  pins clangd `--header-insertion=never`.
- Recorded the design: **animation node**, PLCA follower id 6, two R/C servos (neck nod + jaw), beat-
  driven from a future beatbox (id 5). Phase order agreed: **T1S first, motion second.**
- Created `SPEC.md`, this journal, and refreshed `README.md`. Registered in top-level
  [`SPEC.md`](../../SPEC.md), [`CLAUDE.md`](../../CLAUDE.md), and confirmed the id-6 reservation in
  [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1.
- **No firmware code yet.** Next: L0b (T1S/CLI peripherals in MCC) then L1 (follower bring-up),
  mirroring `guitar` G0/G1.
