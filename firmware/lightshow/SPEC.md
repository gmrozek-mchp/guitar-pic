# lightshow — Specification

> What lightshow *is* (purpose, hardware, interfaces, firmware design, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

> **Status: bring-up.** Project bootstrapped from the [`lemmy`](../lemmy/SPEC.md) node's
> T1S follower (PIC32CM6408PL10048), mirroring the [`guitar`](../guitar/SPEC.md) node.
> The LED output stage (2× WS2812-class strands via TC0 NPWM + DMA) is implemented and
> driving on hardware via the `led` CLI; the beat-driven command plane (L3) comes next.
> See §6 and [`docs/journal.md`](docs/journal.md).

## 1. Purpose

lightshow is the **lighting node** on the marvin T1S bus — it drives **LEDs / lamps in time to
the music** for a stage/light-show effect. Like [`guitar`](../guitar/SPEC.md) and
[`lemmy`](../lemmy/SPEC.md) it is a **follower**: it receives signals over the bus and drives an
output. It does **no sensing and no game logic**.

lightshow is a new **node class** (*lightshow*, `node_type = 5`) alongside the existing
detector / guitar / controller / animation classes (top-level [`SPEC.md`](../../SPEC.md) §2 "Node
classes"). It shares the T1S follower glue and the LAN8651 MAC-PHY with `guitar` / `lemmy`; only
the output stage (LEDs vs. GPIO / servos) and its command source differ.

## 2. Hardware

| Spec | Value |
|------|-------|
| Device | PIC32CM6408PL10048 (same family as guitar / fretboard / lemmy; Cortex-M0+, 24 MHz) |
| Toolchain | XC32 |
| MAC-PHY | LAN8651 (10BASE-T1S), SPI Mode 0, ≤ ~12 MHz on the 24 MHz part |
| LED output | 2× WS2812-class addressable strands (RGB wire order), 33 px each, on `PA10`/`PA11` (`TC0/WO0`/`WO1`, mux E) |

**T1S control pinout** — inherited from the [`lemmy`](../lemmy/SPEC.md) / [`guitar`](../guitar/SPEC.md)
ATE_2026 board: SERCOM0 in **SPI-master mode** (Mode 0: CPOL=0/CPHA=0, MSB first) for the LAN8651,
GPIO chip-select / reset, and an EIC external-interrupt pin for `IRQ_N`:

| Signal | Pin | Function |
|--------|-----|----------|
| `T1S_SCK` | PA05 | SERCOM0 PAD1 |
| `T1S_MISO` | PA07 | SERCOM0 PAD3 |
| `T1S_MOSI` | PA04 | SERCOM0 PAD0 |
| `T1S_CS` | PA06 | GPIO, idle high |
| `T1S_RST` | PA03 | GPIO, idle high |
| `T1S_IRQ_N` | PA02 | EIC EXTINT2, falling edge |
| `CDC_TX` / `CDC_RX` | PB00 / PB01 | SERCOM1 USART (debug console) |

**LED output** — 2× WS2812-class addressable strands (33 px each) driven from **TC0 in 8-bit NPWM +
one DMA channel** (CCBUF-PWM): TC0 runs at 24 MHz / DIV1, `PER=29` → 800 kHz bit clock; one HWORD per
overflow updates `CCBUF0`/`CCBUF1` (strand 0 low byte on `WO0`/`PA10`, strand 1 high byte on
`WO1`/`PA11`), so both strands stream from a single interleaved duty buffer. The trailing `0` duty
holds both lines low for the WS2812 reset/latch. The framebuffer stores channels as **R,G,B and the
wire order is R,G,B** — these strands are RGB-ordered, not GRB (see journal). Data lines swing 0–5 V
directly from the **MVIO/VDDIO2 pins** (`PA10`/`PA11` are on the VDDIO2 domain; tie VDDIO2 to the 5 V
LED rail) — **no external level shifter**. LEDs draw from a separate 5 V rail (not the MCU 3V3) with a
common ground; LED current must not sink through the logic supply. The T1S bus is serviced from the
main loop, woken by `IRQ_N`. PoDL on the pair (design-direction only; see
[T1S/PoDL link](../../docs/t1s-podl-link.md) §6) would power the node but **not** the LEDs.

Remaining hardware validation: 5 V rail sized for ~4 A worst case (66 px × 60 mA); confirm VDDIO2 is
powered/in-range (or SUPC tri-states `PA10`/`PA11`); scope-tune `T0H`/`T1H` bit timing.

## 3. The T1S link

A **PLCA follower** — the mirror of marvin's coordinator glue, reusing the shared
[`third_party/oa-tc6-lib`](../../third_party/oa-tc6-lib) and the same L2 framing, identical in shape
to `guitar`'s:

- **Node id 7**, MAC `02:00:00:00:00:07` (addressing scheme: [T1S/PoDL link](../../docs/t1s-podl-link.md) §7.1).
- `TC6_Init` + `TC6Regs_Init(enablePlca=true, nodeId=7, nodeCount=8, …)` — follower, **not** coordinator.
- **Bare-metal** (no FreeRTOS on PL10): the TC6 service loop runs from the main loop / SysTick tick;
  `IRQ_N` is wired to a SERCOM-EIC pin.
- **Presence heartbeat** (ethertype `0x88B6`) to the coordinator so marvin's `nodes` shows lightshow
  present. A new `node_type = 5` (*lightshow*) is used for the heartbeat payload — marvin's §7.2
  decode + `nodes` display learn it (marvin-side follow-up).
- **Command plane (post-bring-up):** the music/beat signal source is **open** — a future **beatbox**
  node (id 5) and/or marvin's timing pipeline; the ethertype and payload are TBD (see journal).
  Bring-up (L1) needs no RX command semantics — link + presence + CLI only.

The marvin-side reference is [`firmware/marvin/default/src/net/t1s/t1s_link.c`](../marvin/default/src/net/t1s/t1s_link.c)
(coordinator); the follower reference is [`guitar`](../guitar/config.mcc/src/t1s_follower.c).

## 4. Firmware design

Mirrors `guitar`'s `t1s_follower.{c,h}` + `cli.{c,h}` (reused, retargeted), then adds an LED layer:

1. **Bring-up:** reset the LAN8651 (`RST` pulse), configure SPI (Mode 0), `TC6_Init` + `TC6Regs_Init`
   as follower id 7. Gate: read chip revision + PLCA *follower* status.
2. **Service:** call `TC6_Service` from the main loop / tick, woken by `IRQ_N`.
3. **Heartbeat:** periodic (≈500 ms) `0x88B6` presence frame to the coordinator.
4. **CLI** (debug aid, SERCOM1 UART via embedded-cli, static allocation): `t1s` (link / sync /
   chipRev / PLCA / counters), plus `led` (`off` / `fill <r> <g> <b>` / `set <strand> <idx> <r> <g> <b>`
   / `test`) to stage the framebuffer and drive the strands before the command plane exists.
5. **LED layer** (`neopixel.{c,h}`, implemented): R/G/B framebuffer → interleaved per-bit duty buffer
   streamed to `TC0.CCBUF` over DMA (see §2). An effect/pattern engine and beat-driven effects that map
   the music/beat signal → light patterns come later (L3).

Static allocation only (no malloc), per project rule.

## 5. What this is *not*

- **Not a sensor.** No phototransistors, no ADC.
- **Not a game brain.** No chord/strum timing.
- **Not the PLCA coordinator.** marvin (node 0) beacons the cycle; lightshow is a follower.
- **Not a Wii actuator.** It does not touch a Wii guitar / Wiimote — it drives lights. (`guitar` is
  the Wii/LED actuator node.)
- **Not a puppet.** It drives no servos — that is the [`lemmy`](../lemmy/SPEC.md) animation node.
- **Not (yet) beat-driven.** The command source is future; L1 is link + presence + CLI only.

## 6. Milestones

| Status | Item |
|---|---|
| ✅ | **L0** — project bootstrapped from `lemmy`'s T1S follower (PIC32CM6408PL10048): clock/EVSYS/NVIC/PORT, SERCOM0 SPI (Mode 0), EIC EXTINT2 on `IRQ_N`=PA02, `CS`=PA06 / `RST`=PA03 GPIO, SERCOM1 debug UART (PB00/PB01), follower glue + CLI — retargeted to id 7 / `node_type = 5` |
| 🚧 | **L1** — T1S follower bring-up on hardware: `LAN8651 up … PLCA follower id=7/8`, presence heartbeat, `t1s` CLI |
| ✅ | **L2** — LED output: `neopixel.{c,h}` driver (TC0 NPWM + DMA, 2× WS2812-class strands, RGB wire order) + `led` CLI drive the strands manually; remaining hardware validation (rail sizing, VDDIO2, scope timing) tracked in §2 / journal |
| 🔭 | **L3** — beat-driven light show: consume the music/beat signal over T1S → light patterns in time with the music |
