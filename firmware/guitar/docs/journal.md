# guitar — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the guitar (Wii-guitar actuator node) firmware. Newest entries at the top. For *what guitar is* (purpose, hardware, link, firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**G1 + G2 done — the node is proven end-to-end on hardware.** guitar is the **Wii-guitar actuator node**:
a PIC32CM PL10 T1S PLCA *follower* (node id 3, MAC `02:00:00:00:00:03`) that receives marvin's 1-byte
button bitmask over ethertype `0x88B5` and drives a Wii guitar controller via open-drain GPIO — the
actuation half of today's [fretboard](../../fretboard/SPEC.md) firmware, on its own node. Firmware
(`config.mcc/src/t1s_follower.{c,h}` + `cli.{c,h}`, reusing `third_party/oa-tc6-lib`, wired via
`user.cmake` + `main.c`) brings the LAN8651 up — G1/G2 were bench-proven at the old id 2
(`LAN8651 up - chipRev=2, MAC=02:00:00:00:00:02, PLCA follower id=2/8`); renumbered to id 3 in source
2026-07-28 (re-flash reproduces at `id=3/8`) — and **marvin's command over T1S drives the addressed Wii
button**. The node sends a 500 ms presence heartbeat (ethertype `0x88B6`) so marvin's `nodes` shows it
present, and an embedded-cli console on the SERCOM1 debug UART (`t1s`/`btn`/`tap`/`id`/`plca`) drives the
GPIOs and reads link/sync/PLCA diagnostics.

**Board port (2026-07-23):** the firmware is being brought to the **ATE_2026 board**, which carries
**status LEDs** rather than a Wii guitar — it does not touch a Wiimote. The T1S command path is unchanged;
only the output stage and pinout differ: the fret/strum GPIOs now drive **active-high LEDs** (`Set` = lit,
`Clear` = off) and strum up/down collapse to a single **STRUM** indicator (doc's white/strobe). The
`BTN_APPLY` macro / `buttons_*` "button" naming is retained deliberately — a future board may carry both
Wii actuators *and* LEDs. See the 2026-07-23 decision-log/session entries for the pin remap.

**Next (G3, full system):** `fretboard` (detector) + `guitar` (actuator) both on the bus with marvin
selecting the active of each — marvin already targets the guitar node for actuation; the remaining work is
moving the fretboard onto a T1S detector node and the active-detector/active-guitar selector. Tracked on
the marvin side.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-28 | **T1S PLCA node id renumbered 2 → 3** (`t1s_follower.c` `T1S_NODE_ID`, MAC now `02:00:00:00:00:03`). Node_type heartbeat code stays 2 (guitar). This is the coordinated guitar-id change: marvin's coordinator node table (`net/t1s/t1s_link.c` `s_nodes[]`) and the fretboard's peer-to-peer guitar target (`t1s_detector.c` `T1S_GUITAR_ID`) both move to 3 in the same pass. Last on-hardware bring-up (G1/G2) was at id 2; re-flash guitar **and** marvin together for id 3 to take effect on the wire. | Adopting the target bus table (docs/t1s-podl-link.md §7.1): controllers/fauxmotes take 1–2, guitar 3, fretboard 4. The guitar's id is set at three points that all address it — the node itself, marvin (TX target), and the fretboard (peer-to-peer) — so all three change together or commands miss the node. |
| 2026-07-23 | **Port to the ATE_2026 board: active-high LED indicators, not open-drain Wii actuators.** Output stage flips from software open-drain (`Clear`+`OutputEnable` assert / `InputEnable` release) to **active-high push-pull** (`Set` = lit / `Clear` = off) in the `BTN_APPLY` macro; strum up/down (bits 5, 6) collapse to a single **STRUM** indicator. Pins remapped (see session log): control `CS`=PA06, `RST`=PA03, `IRQ_N`=PA02 on **EIC EXTINT2** (falling); LEDs `FRET_GREEN`=PA12, `FRET_RED`=PA11, `FRET_YELLOW`=PA10, `FRET_BLUE`=PA09, `FRET_ORANGE`=PA08, `STRUM`=PA13. The `BTN_APPLY` / `buttons_*` "button" naming is kept intentionally. | The new board only exposes status LEDs (no Wiimote), and the doc specifies active-high drive. Keeping the button naming leaves room for a future board that carries both Wii actuators and LEDs, so the command → output mapping code stays shared. Done via the mplab-mcc MCP server (no hand-edited MCC/generated files). |
| 2026-06-16 | **Wii-guitar actuation becomes its own subproject (`guitar`), split out of fretboard.** PIC32CM PL10, T1S PLCA **follower** node id 2 (MAC `02:00:00:00:00:02`), receives a 1-byte bitmask over ethertype `0x88B5` and drives 7 open-drain Wii-guitar GPIOs. Sensing stays on `fretboard` (detector node). Actuation logic is a verbatim port of fretboard's `cmd_receive.c`. The link reuses `oa-tc6-lib` + the shared L2 framing as the mirror of marvin's coordinator glue. **Bare-metal** (no FreeRTOS on PL10): TC6 serviced from the main loop / tick; `IRQ_N` on a SERCOM-EIC pin. | The T1S bus was built for multiple node classes; separating detector from actuator lets multiple guitar/detector variants coexist on one PLCA pair with marvin selecting the active of each. Same MCU family as fretboard keeps the "OA SPI driver scales across the family" demo and minimizes bring-up. Greg spins up a *fresh* guitar MCC project (not a fork of fretboard). |
| 2026-06-16 | **Edge-ai / on-device model is out of scope; fretboard keeps actuating until guitar is proven.** This subproject covers only the marvin-driven actuation path. fretboard's MODEL_DRIVEN/standalone modes stay untouched; re-homing the model (detector infers → T1S → guitar) and the `applied_mask` training-label coupling are deferred. marvin flips its command target from the fretboard node to the guitar node only once G3 passes. | Contains the blast radius — the playing system stays up throughout, mirroring the UART/T1S parallel-coexistence approach. |

---

## Open questions

- **Active-guitar selection on marvin** — how marvin picks which guitar node is active (mirror of `Detector_SetActive`) and flips its command target from the fretboard node to the guitar node; designed on the marvin side, tracked there.

*(Resolved at G0: MCC pinout fixed — see session log; CS is a GPIO driven across the chunk, not hardware SS.)*

---

## Session log

### 2026-07-23 — port to the ATE_2026 board (LED indicators, pin remap)

Bringing the guitar firmware (most-recent T1S logic) onto the **ATE_2026 board**, which has status LEDs and no Wii guitar. All MCC changes made via the **mplab-mcc MCP server** (per project rule — no hand-editing `.yml` or generated source).

- **Control pins remapped:** `T1S_CS` PA15→**PA06**, `T1S_RST` PA14→**PA03**, `T1S_IRQ_N` PA13→**PA02**. SPI unchanged (SERCOM0: SCK=PA05, MISO=PA07, MOSI=PA04). Debug UART now `CDC_TX`/`CDC_RX` on PB00/PB01 (SERCOM1, same pins).
- **EIC moved EXTINT13 → EXTINT2** (falling edge) on the new `IRQ_N`=PA02. Kept the hardware EIC interrupt (Greg's call). *Gotcha:* a manual MCC regen left the EIC in **polled mode** — channel 2 detection was on but its interrupt (`EIC_INT_2`) was off, so MCC dropped the whole `EIC_CallbackRegister` API and the build failed on an undeclared `EIC_CallbackRegister`. Fix: set `EIC_INT_2=true` + clear the stale `EIC_INT_13=true`, regenerate → callback API restored.
- **Output stage → active-high LEDs.** Frets on PA08–PA12, single **STRUM** on PA13; all outputs, init `Low` (off). `BTN_APPLY` rewritten from software open-drain to `Set`/`Clear`; `buttons_release_all()` now drives all indicators low; `buttons_apply_mask()` collapses command bits 5|6 into `STRUM`. `EIC_PIN_13`→`EIC_PIN_2` in `t1s_follower.c`; comments in `.c`/`.h` refreshed (EXTINT2, "status indicator" wording). Freed the old board's pins (PB02/PB03, PA14/PA15, PA18, PA21/PA22).
- **Build: `default: SUCCESS`.** Bit→LED map now: bit0=GREEN(PA12), 1=RED(PA11), 2=YELLOW(PA10), 3=BLUE(PA09), 4=ORANGE(PA08), 5|6→STRUM(PA13).
- Not yet exercised on the physical ATE_2026 board — bring-up (LEDs light on marvin's command) is the next hardware gate.

### 2026-07-03 — doc-vs-code audit fixes

Part of a repo-wide doc audit ([`../../../docs/doc-audit-2026-07.md`](../../../docs/doc-audit-2026-07.md)). `README.md` said "skeleton + design — firmware not yet built"; corrected to "working on hardware" to match `SPEC.md` (follower up, receives+actuates marvin's command over T1S, heartbeat + CLI live). `SPEC.md` §2/§4 reframed from "to build" to implemented-in-`t1s_follower.c`; dropped the stale "port of fretboard's `cmd_receive.c`" reference (that file is gone — the open-drain logic is the `BTN_APPLY` macro). Also fixed a wrong in-code comment in `t1s_follower.h` (EIC EXTINT15 → EXTINT13).

### 2026-06-17 — fretboard live on the bus; planned SPI bump to 12 MHz (both ends)

- The `fretboard` detector+actuator node is up on T1S (id 1) and **commands the guitar directly** over the bus (peer-to-peer, ethertype `0x88B5`) — the guitar applies it like any `0x88B5` frame (no source filtering). End-to-end command-latency budget ≈ 6–7 ms worst case, dominated by the 240 Hz tick + the **1 MHz host SPI** (`docs/t1s-podl-link.md` §4.1).
- **Planned:** raise SERCOM0 SPI from `BAUD=11` (≈1 MHz) to `BAUD=0` (GCLK/2 ≈ **12 MHz**) on **both** guitar and fretboard — removes ~1 ms of command latency (each chunk ~0.55 ms → ~45 µs). Watch on the bump: CS-held-across-chunk timing and SPI/`Loss_of_Framing` errors at the higher rate (jumper-wire signal integrity); 1 MHz was the conservative bring-up value.
- **Caveat (active-source):** marvin can also command the guitar (`02:..:02`); with the fretboard now driving it too, only one source should be armed at a time until marvin's active-detector/active-guitar selector lands.

### 2026-06-17 — `id`/`plca` over-servicing tripped Loss_of_Framing

- Running `id` on a *live* link emitted a `Loss_of_Framing_Error` between reads (reads themselves fine — reg 0x01 = `0x0007C1B4`). Cause: `T1SFollower_ReadId`/`ReadPlca` still used the tight `service_pump` loops (2000 + 5000 per reg) added for the dead-link bring-up case; hammering `TC6_Service` while the link is up trips a transient RX framing error (which then self-recovers via `OnEvent` reinit). Fix: the commands now just **enqueue** the reads and let the normal main-loop servicing complete them + log async. Also only decode oui/model for reg `0x01` (meaningless for `0x00`/`0x000A0094`).

### 2026-06-17 — Presence heartbeat (ethertype 0x88B6)

- The guitar now TXes a periodic (500 ms) heartbeat to the coordinator (`02:00:00:00:00:00`) under a **separate ethertype `0x88B6`** so marvin can show real per-node presence (PLCA has no discovery). Payload: `ver, node_type(2=guitar), node_id, flags(bit0=synced), seq_u32`. First TX path on the follower — `send_heartbeat()` builds the frame + `TC6_SendRawEthernetPacket` (one in-flight, `s_hb_busy`-guarded), driven from `T1SFollower_Tasks`. Format documented in T1S doc §7.2; marvin stamps last-seen and reports via its `nodes` command.

### 2026-06-17 — T1S diagnostics in the CLI

- After G2 (marvin drives the guitar over T1S) added link visibility to the CLI: `status` now also shows `synced` (from `TC6_GetState`), TX/RX credits, and the PLCA `id/count`; new `plca` command async-reads the PLCA status register (bit 15 = `plca_status`). New accessors `T1SFollower_GetState`/`NodeId`/`NodeCount`/`ReadPlca`. Mirrors a marvin-side `t1s` console command.
- Next: a lightweight **heartbeat** (ethertype `0x88B6`) so marvin can show real per-node presence (`nodes` command) — PLCA itself has no node discovery.

### 2026-06-17 — G1 bring-up: LAN8651 connected, `Unsupported_Hardware`

- First power-up with the board: boot banner + responsive CLI ✓, but `TC6Regs_Init rejected` and repeating `t1s event: Unsupported_Hardware`. The lib's `OnReadId1` reads control reg `0x01` and requires OUI `0x1F0` / model `0x1B` (and `0x000A0094` chip-rev nonzero); the readback doesn't match.
- Ruled out: **async-SPI mismatch** (TC6Regs_Init pumps `TC6_Service` while waiting, so our ISR-completed transfers work — tc6-regs.c:325-340); **SPI clock too fast** (SERCOM0 BAUD=11 ≈ **1 MHz**, conservative). So a wrong reg readback points to **wiring / reset / CS / MISO**, not timing.
- Added an **`id` CLI command** (`T1SFollower_ReadId`) to raw-read the ID regs. The on-demand reads got flushed by the lib's re-identification churn, so a **temporary diag hook** (`T1SReg_DiagId`, called from the vendored `tc6-regs.c` `OnReadId1`/`OnReadId2` — marked `TEMP diag`, to be reverted) surfaces the lib's own readback.
- **Readback: reg `0x01` = `0xFFFFFFFF`** (and `0x000A0094` = `0xFFFFFFFF`). All-ones ⇒ the host MISO sits idle-high and **the LAN8651 isn't driving data back** — a **physical-layer** issue, not firmware (our SPI master completes transactions fine). Suspects: chip power, RST not releasing, or a missing wire (MISO/CS/SCK/MOSI/GND). Next: check 3V3 + RST=high + wiring; scope CS/SCK/MOSI/MISO. The boot `ID reg` line is the live indicator — it'll read `0x0007C1Bx` (oui 0x1F0/model 0x1B) once the chip responds.
- **Firmware bring-up bugs fixed along the way (keepers):** `service_pump` reduced to a single `TC6_Service` pass (the old `while(s_need_service)` drain spun forever on non-syncing hardware); init made non-blocking; error/event logging rate-limited (`diag_log`). A stray pre-init `TC6_Reset` + read froze the MCU (faulted in `TC6_Reset` on the freshly-init'd driver) — removed.
- **Root cause: CS and IRQ_N wires were swapped.** Reassigned in MCC (commit `a951110`): `T1S_IRQ_N` → PA13 (EIC_EXTINT13, falling), `T1S_CS` → PA15; firmware EIC callback → `EIC_PIN_13`. After the fix: reg `0x01 = 0x0007C1B4` (oui 0x1F0 / model 0x1B / rev 4), chipRev `2`, and `LAN8651 up … PLCA follower id=2/8` — **G1 PASSED.** Temp lib diag hook reverted (submodule pristine); the `id` CLI command + `on_id_read` kept as a permanent diagnostic.

### 2026-06-16 — CLI added (status / btn / tap) on the debug UART

- Added an operator CLI using the **vendored embedded-cli** (copied into `config.mcc/src/third_party/embedded-cli/`, matching marvin's per-project vendoring; static-allocation mode, ~1 KB `CLI_UINT` buffer). New `cli.{c,h}`; `main.c` calls `CLI_Initialize()` + `CLI_Tasks()`.
- **Bare-metal integration** (mirrors marvin's `console.c` but no FreeRTOS): `CLI_Tasks()` drains the SERCOM1 RX ring each main-loop pass and feeds `embeddedCliReceiveChar`/`embeddedCliProcess`; `writeChar` queues to the SERCOM1 TX ring with a bounded `SYSTICK_DelayMs(1)` retry.
- Commands: **`status`** (link up?, chipRev, rx-cmd count, last applied mask), **`btn <mask hex>`** and **`tap <mask hex> [ms]`** drive the 7 button GPIOs directly — so the Wii-guitar wiring can be exercised *before* the T1S link is up.
- Exposed accessors from the follower (`T1SFollower_ChipRev/LastCmd/RxCount` + `ApplyButtons/ReleaseButtons`); the RX handler now tracks `s_last_cmd` / `s_rx_count`. Build wiring (`user.cmake`) gains `cli.c` + `embedded_cli.c` + the embedded-cli include dir.
- **Built + ran; fixed two no-board bugs (console unusable).** Symptom: a stream of `t1s error:` lines and no CLI activity with no LAN8651 attached. Two causes:
  1. **Flood:** `TC6_Service` raises an error every service pass when nothing answers, and the first cut logged *every* one via `log_str`. Fix: `OnError`/`OnEvent` log through a **rate-limited `diag_log` (≤ ~1/sec)** (mirrors the reference `tc6-noip.c` `PrintRateLimited` the bare-metal port had dropped); errors counted (`T1SFollower_ErrCount`, shown in `status`).
  2. **CLI gated behind init:** `T1SFollower_Initialize()` *blocked* up to 3 s waiting for init-done, so `CLI_Initialize()` (called after it in `main`) was delayed and, combined with the flood, the prompt was lost. Fix: **init is now non-blocking** — it kicks off `TC6Regs_Init` and returns; `T1SFollower_Tasks()` detects init-done and brings the data path up in the background. CLI is responsive from boot regardless of link state. Added a one-time **boot banner** (`guitar: boot - t1s follower + cli`) to confirm the running binary.
- Confirmed SysTick is correctly wired (`interrupts.c` vector → `plib_systick` handler, `SYSTICK_FREQ=24 MHz`, LOAD=24000 → true 1 ms), so `GetTickCounter()` is milliseconds and the rate-limit is effective. A 10 Hz flood ⇒ a pre-fix binary was running (rebuild/reflash).
- Manual `btn`/`tap` actuation is testable now without the board; T1S link bring-up (G1/G2) still needs the LAN8651.

### 2026-06-16 — G1/G2: T1S follower firmware written (bare-metal)

- Wrote the follower glue in `config.mcc/src/` (alongside `main.c`, matching the other projects' MCC layout; added to the build via `user.cmake`, not the MCC fileSet):
  - `t1s_follower.{c,h}` — `T1SFollower_Initialize()` + `T1SFollower_Tasks()`. Bare-metal mirror of marvin's `t1s_link.c`: SysTick 1 ms clock, `T1S_RST` pulse, `SERCOM0_SPI_CallbackRegister`, `TC6_Init` + `TC6Regs_Init(nodeId=2, follower, promiscuous=false)`, service to init-done, then serviced from `main()`. SPI completion + `T1S_IRQ_N` (EIC EXTINT15 callback) set a `s_need_service` flag; `service_pump` runs `TC6_Service` until idle.
  - **GPIO chip-select**: `OnSpiTransaction` drives `T1S_CS` low before `SERCOM0_SPI_WriteRead` and the completion ISR raises it — CS held across the whole chunk (the lib batches all chunks into one transfer call).
  - **RX → actuation**: `OnRxEthernetPacket` validates ethertype `0x88B5` and applies payload byte 0 as the button bitmask — software open-drain on the 7 GPIOs (assert = `Clear`+`OutputEnable`, release = `InputEnable`), bit order Green..Orange/StrumDown/StrumUp. Buttons released at init (they boot `Out/Low`).
  - `tc6-conf.h` — tuned for the PL10: `TC6_CHUNKS_XACT=4` + 64 B rx buffer (this node only moves a ~60 B command frame), keeping SRAM well within 8 KB.
  - Logging via the SERCOM1 debug UART (`log_str` → `SERCOM1_USART_Write`); no status LED (the LEDs became buttons).
- Build wiring: `cmake/guitar/default/user.cmake` adds `t1s_follower.c` + `libtc6/src/tc6.c` + `tc6-regs.c` and the include dirs; `main.c` calls `T1SFollower_Initialize()` + `_Tasks()`.
- **Status: builds, programs, and runs on the PL10** — the SERCOM1 debug UART produces output. Without the T1S board connected, the firmware hits the 3 s init timeout and logs `MAC-PHY not responding` (expected). Remaining gate (needs the LAN8651 wired): `guitar: LAN8651 up - chipRev=… PLCA follower id=2/8` (G1); then marvin (built `MARVIN_FRETBOARD_TRANSPORT=1`) sends a command and the addressed Wii button asserts (G2). CLI (`status`/`btn`/`tap`) is the agreed next follow-up. If RX shows nothing once wired, try `promiscuous=true` as a debug step.

### 2026-06-16 — G0: guitar MCC project generated + hardware review

- Greg created the guitar MPLAB/MCC project (PIC32CM6408PL10048) and configured the hardware; reviewed and confirmed complete (commit `285248a`):
  - **SERCOM0 SPI master, Mode 0** (CPOL=0/CPHA=0, MSB, 8-bit) for the LAN8651 — MOSI=PA04, SCK=PA05, MISO=PA07. **CS = PA13 GPIO** (driven low across a full TC6 chunk — resolves the CS-across-chunk open item), **RST = PA14** (idle high).
  - **EIC EXTINT15 = falling edge** on `T1S_IRQ_N` = PA15 (NVIC EIC enabled, `EIC_CallbackRegister` API) — the IRQ_N → service hook. (First MCC pass had only the pin mux + NVIC with no EIC driver; adding the EIC component fixed it — analog of marvin's PIO-interrupt enable.)
  - **7 Wii button GPIOs** (software open-drain via `Set/Clear` + `OutputEnable/InputEnable`): FRET_GREEN=PA22 (bit0), FRET_RED=PA21 (1), FRET_YELLOW=PA08 (2), FRET_BLUE=PA09 (3), FRET_ORANGE=PB02 (4), STRUM_DOWN=PA18 (5), STRUM_UP=PB03 (6).
  - SERCOM1 USART (PB00/PB01) as a debug console.
- **Two firmware-init notes for G1/G2** (not MCC gaps): (1) the button pins boot `Out/Low` = asserted, so the actuator init must release all 7 (`*_InputEnable()`) first; (2) no TC/SysTick in the MCC init — the glue will set up the Cortex-M0+ **SysTick** at 1 ms for `TC6Regs_CheckTimers`/`GetTicksMs`.

### 2026-06-16 — Subproject created (skeleton + design)

- Created `firmware/guitar/` (SPEC.md, this journal, README) as part of the T1S node-class restructuring. Recorded the actuator-node design: PIC32CM PL10, PLCA follower id 2, command RX → Wii GPIO (port of fretboard `cmd_receive.c`), reusing `oa-tc6-lib` + the shared L2 framing.
- Registered in top-level [`SPEC.md`](../../SPEC.md) (§2 node classes, §3 registry, §4 map, §5 hardware) and [`CLAUDE.md`](../../CLAUDE.md); node id 2 / MAC `02:..:02` added to [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1.
- **No firmware code yet.** Next: G0 (MCC project) then G1 (follower bring-up).
