# lightshow — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the lightshow (LED
lighting) firmware. Newest entries at the top. For *what lightshow is* (purpose, hardware, link,
firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

## Current focus

**Bring lightshow up on the T1S bus first, then add the LED output.** lightshow is the **lighting
node**: a PIC32CM6408PL10048 PLCA follower (id 7) that will drive LEDs / lamps in time to the music.
Phase order: L1 T1S follower (link + heartbeat + CLI) → L2 LED output → L3 beat-driven light show.

## Plan

- [ ] **L1 — T1S follower bring-up on hardware.** Link sync as PLCA follower id 7/8, presence
      heartbeat (`0x88B6`, `node_type = 5`), `t1s` CLI. Ported wholesale from `lemmy` / `guitar`.
  - LED0 (PB02) liveness heartbeat ported from `lemmy` (`status_led.{c,h}`): non-blocking, off the
    SysTick clock, lub-dub double pulse when on the bus / single blip when link down.
- [ ] **L2 — LED output.** Pick the drive method + pin/peripheral, add the LED driver and a
      `led`/pattern CLI to exercise the lights manually.
- [ ] **L3 — beat-driven light show.** Consume the music/beat signal over T1S → light patterns in
      time with the music.

## Open questions

- **LED output hardware.** Addressable LED string (WS2812-class, serial protocol) vs. PWM-dimmed
  lamp channels? Which pin/peripheral, and which power rail? Deferred until L2.
- **Command/beat-signal plane.** What drives the light patterns — a future **beatbox** node (id 5),
  marvin's timing pipeline, or both? Over which ethertype and payload? Shared open question with
  `lemmy`; deferred until L3.
- **Heartbeat `node_type = 5`** — confirm with the marvin side before it's baked in: marvin's §7.2
  decode + `nodes` display need to learn value 5 (marvin-side follow-up). The top-level
  [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1 reserves lightshow at **id 7**.

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-29 | **lightshow created as the *lighting* node class (`node_type = 5`); T1S bring-up before LED output.** PIC32CM6408PL10048, PLCA follower **id 7** / MAC `02:00:00:00:00:07` (the slot reserved in [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1). Phase order: L1 T1S follower (link + heartbeat + CLI) → L2 LED output → L3 beat-driven light show. | Prove the node on the bus first, reusing the `lemmy` / `guitar` follower glue + `oa-tc6-lib` (same MCU family — minimizes bring-up), then layer the LED output. The lighting output and its command source differ from the puppet, so it is a distinct node class from `lemmy` (animation). |

## Session log

### 2026-07-29 — bootstrap from lemmy

- Bootstrapped `firmware/lightshow/` by copying `firmware/lemmy` at commit `aa8bd0f` (the L1 T1S
  follower state, *before* the servo commits) — straight copy, then renamed `lemmy` → `lightshow`
  across the project: MPLAB project (`.vscode/lightshow.mplab.json`), cmake target
  (`cmake/lightshow/default/user.cmake`), `mcc.vscode` association, `settings.json` build path,
  and the source identifiers (`cli.c` prompt/`info`, `t1s_follower.{c,h}` banners/comments,
  `tc6-conf.h`). MCC-generated tree (`config/default/`, `packs/`) untouched.
- Retargeted the T1S identity: follower **id 6 → 7** (MAC last byte follows), heartbeat
  `node_type` **4 (animation) → 5 (lightshow)** (`T1S_HB_TYPE_LIGHTSHOW`).
- Rewrote `SPEC.md` / `README.md` / this journal for the LED lighting role (dropped lemmy's puppet /
  servo content).
- Added the `lightshow` folder to `guitar-pic.code-workspace`.
