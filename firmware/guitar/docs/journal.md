# guitar — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the guitar (Wii-guitar actuator node) firmware. Newest entries at the top. For *what guitar is* (purpose, hardware, link, firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Skeleton + design only — no firmware written yet.** guitar is a new subproject created when the
marvin↔node T1S link split sensing from actuation into separate node classes. It is the **Wii-guitar
actuator node**: a PIC32CM PL10 T1S PLCA *follower* (node id 2) that receives marvin's 1-byte button
bitmask and drives a Wii guitar controller via open-drain GPIO — the actuation half of today's
[fretboard](../../fretboard/SPEC.md) firmware, on its own node.

**G0 done** (MCC project generated + hardware reviewed; commit `285248a`). Next (G1/G2): the T1S-follower
glue — the bare-metal mirror of marvin's coordinator glue in
[`firmware/marvin/default/src/net/t1s/t1s_link.c`](../../marvin/default/src/net/t1s/t1s_link.c),
reusing [`third_party/oa-tc6-lib`](../../../third_party/oa-tc6-lib) and the shared L2 framing: reset pulse,
SERCOM0 SPI (CS driven across each transfer), `IRQ_N`→service via the EIC callback, `TC6_Init` +
`TC6Regs_Init(nodeId=2, follower)`, RX command → the 7 button GPIOs (port of fretboard `cmd_receive`),
SysTick at 1 ms, serviced from `main()`.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-16 | **Wii-guitar actuation becomes its own subproject (`guitar`), split out of fretboard.** PIC32CM PL10, T1S PLCA **follower** node id 2 (MAC `02:00:00:00:00:02`), receives a 1-byte bitmask over ethertype `0x88B5` and drives 7 open-drain Wii-guitar GPIOs. Sensing stays on `fretboard` (detector node). Actuation logic is a verbatim port of fretboard's `cmd_receive.c`. The link reuses `oa-tc6-lib` + the shared L2 framing as the mirror of marvin's coordinator glue. **Bare-metal** (no FreeRTOS on PL10): TC6 serviced from the main loop / tick; `IRQ_N` on a SERCOM-EIC pin. | The T1S bus was built for multiple node classes; separating detector from actuator lets multiple guitar/detector variants coexist on one PLCA pair with marvin selecting the active of each. Same MCU family as fretboard keeps the "OA SPI driver scales across the family" demo and minimizes bring-up. Greg spins up a *fresh* guitar MCC project (not a fork of fretboard). |
| 2026-06-16 | **Edge-ai / on-device model is out of scope; fretboard keeps actuating until guitar is proven.** This subproject covers only the marvin-driven actuation path. fretboard's MODEL_DRIVEN/standalone modes stay untouched; re-homing the model (detector infers → T1S → guitar) and the `applied_mask` training-label coupling are deferred. marvin flips its command target from the fretboard node to the guitar node only once G3 passes. | Contains the blast radius — the playing system stays up throughout, mirroring the UART/T1S parallel-coexistence approach. |

---

## Open questions

- **Active-guitar selection on marvin** — how marvin picks which guitar node is active (mirror of `Detector_SetActive`) and flips its command target from the fretboard node to the guitar node; designed on the marvin side, tracked there.

*(Resolved at G0: MCC pinout fixed — see session log; CS is a GPIO driven across the chunk, not hardware SS.)*

---

## Session log

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
