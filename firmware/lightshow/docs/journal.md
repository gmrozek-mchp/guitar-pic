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
- [ ] **L2 — LED output.** Drive method chosen (see decision log): **TC0 8-bit NPWM + 1 DMA
      channel** for 2× WS2812 strands (33 px each). Progress:
  - [x] Pins: `PA10 = TC0/WO0` (NEOPIXEL_LEFT / strand 0), `PA11 = TC0/WO1` (NEOPIXEL_RIGHT /
        strand 1), mux E.
  - [x] MCC config: DMAC ch0 (`TRIGSRC=TC0_OVF`, `TRIGACT=BEAT`, HWORD beats, source-increment,
        dest fixed) + TC0 in `COUNT8`/`NPWM`/`DIV1`, `PER=29` → 800 kHz. Committed `f59e63c`.
  - [x] Driver `neopixel.{c,h}`: 198-byte R/G/B framebuffer → 1586-byte interleaved duty buffer
        (792 bits + 1 drain entry). `NeoPixel_Show()` arms `DMAC_ChannelTransfer` at
        `&TC0.CCBUF[0]`; one HWORD/overflow writes CCBUF0/CCBUF1 (WO0 low byte, WO1 high byte).
        Driver owns `PER`; timing `T0H=8`/`T1H=19` ticks (tune on scope). Reset/latch: the trailing
        `0` duty holds both lines low after the frame; `Show()` gates on `DMAC_ChannelIsBusy()`.
  - [x] `led` CLI command (`off` / `fill <r> <g> <b>` / `set <strand> <idx> <r> <g> <b>` / `test`)
        to stage the framebuffer and call `NeoPixel_Show()` manually.
  - [ ] Hardware: 3.3 V→5 V data level shift (74AHCT125-class) and a 5 V rail sized for ~4 A worst
        case (66 px × 60 mA); confirm on the board.
  - [ ] Bring-up check: verify a single HWORD write to `CCBUF[0]` sets **both** buffer-valid flags
        so both strands update from one beat (the crux of the single-channel trick).
- [ ] **L3 — beat-driven light show.** Consume the music/beat signal over T1S → light patterns in
      time with the music.

## Open questions

- **LED output hardware — pins + power.** Drive method + peripheral are settled (TC0 8-bit NPWM +
  1 DMA channel; see decision log). Still open: which two GPIOs carry the strand data (must be
  `TC0/WO0` + `WO1`), the 5 V power rail sizing, and the 3.3→5 V data
  level shift. Resolve against the actual board at L2.
- **Command/beat-signal plane.** What drives the light patterns — a future **beatbox** node (id 5),
  marvin's timing pipeline, or both? Over which ethertype and payload? Shared open question with
  `lemmy`; deferred until L3.

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-29 | **WS2812 drive = TC0 8-bit NPWM + 1 DMA channel (CCBUF-PWM)** (2 strands × 33 px on `TC0/WO0`+`WO1`). One DMA channel, `TRIGACT=BLOCK`, TC0-overflow-triggered, writes CCBUF0+CCBUF1 (2 BYTE beats) per bit; PER≈29 → 800 kHz bit clock at 24 MHz (T0H≈8 / T1H≈17 ticks, inside WS2812 ±150 ns). Buffer = 2×(33×24) = 1584 B interleaved per-bit duty (+198 B GRB framebuffer), fits 8 KB. Timer DMA trigger paces it directly (no EVSYS). | Confirmed on the DFP for *this* part: only **2 DMA channels**, **4 EVSYS**, **24 MHz**, **8 KB SRAM**. SPI+DMA is ruled out (both SERCOMs used: T1S + debug UART), but T1S SPI is **interrupt-driven, not DMA**, so both DMA channels are free. CCBUF-PWM modulates the fall time *within* each bit period via one CC/strand → one DMA update per bit (800 kHz, ~30 cyc/beat) and the smallest buffer. Leaves a spare DMA channel + TCC0 + TC1/2. |
| 2026-07-29 | **Parallel WS2812 via DMA→TCC0 `PATTBUF` (pattern generator) is the multi-strand fallback, *not* used for 2 strands.** This is the faithful SAMD21-style trick (SAMD21 DMAC can't reach PORT — it's on the single-cycle IOBUS — so the real method forces the WOx pin levels per sub-slot via the pattern generator, not DMA-to-PORT). Verified TCC0 here has it: `PATT`/`PATTBUF` with `PGE0–3`/`PGV0–3`, 4 WO, output matrix. 3 sub-slots/bit (all-high → data → all-low), DMA streams `PATTBUF` HWORDs on TCC0 overflow, double-buffered. | Its payoff is up to **4 bit-parallel strands** from one DMA stream (why NeoPXL8 uses a parallel scheme). Costs 3 DMA updates/bit (2.4 MHz beat, ~10 cyc/beat — tightest path at 24 MHz) and ~4.75 KB buffer — worse than CCBUF-PWM on both axes for only 2 strands. Reach for it only if strand count outgrows the CC channels or truly bit-parallel output is wanted. Both methods write only timer (APB) registers, so neither depends on DMA-to-PORT. |
| 2026-07-29 | **marvin recognizes lightshow's heartbeat** — added a lightshow node row (id 7) to marvin's `net/t1s` node table + a `"lightshow"` display name, so `nodes` lists lightshow present. marvin maps id→type via its static table (it does not decode the payload `node_type` byte), so lightshow's advertised `node_type=5` is informational. | Closes the "confirm node_type=5 with marvin" question: awareness is a table row keyed by node id, matching how guitar / fretboard / lemmy are recognized. |
| 2026-07-29 | **lightshow created as the *lighting* node class (`node_type = 5`); T1S bring-up before LED output.** PIC32CM6408PL10048, PLCA follower **id 7** / MAC `02:00:00:00:00:07` (the slot reserved in [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1). Phase order: L1 T1S follower (link + heartbeat + CLI) → L2 LED output → L3 beat-driven light show. | Prove the node on the bus first, reusing the `lemmy` / `guitar` follower glue + `oa-tc6-lib` (same MCU family — minimizes bring-up), then layer the LED output. The lighting output and its command source differ from the puppet, so it is a distinct node class from `lemmy` (animation). |

## Session log

### 2026-07-29 — WS2812 drive: MCC config + driver

- MCC: added TC0 (`COUNT8`/`NPWM`/`DIV1`, `PER=29`), DMAC ch0 (`TC0_OVF`/`BEAT`/HWORD/src-inc),
  and pins `PA10=TC0/WO0`, `PA11=TC0/WO1`. Committed `f59e63c`. Confirmed the overflow DMA request
  drives the channel directly — no EVSYS/`EVCTRL` event needed.
- Wrote `neopixel.{c,h}` (single DMA channel, interleaved CCBUF0/CCBUF1 HWORD writes) and wired
  `NeoPixel_Initialize()` into `main.c` + `user.cmake`. Committed `bbb59e1`.
- Added the `led` CLI command (`off` / `fill` / `set` / `test`) so the strands can be exercised
  from the debug UART. `test` marches R/G/B on strand 0 and a dim white every 4th px on strand 1 at
  low levels (modest bring-up current). Ready for hardware/scope verification.

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
