# guitar — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the guitar (Wii-guitar actuator node) firmware. Newest entries at the top. For *what guitar is* (purpose, hardware, link, firmware design, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Skeleton + design only — no firmware written yet.** guitar is a new subproject created when the
marvin↔node T1S link split sensing from actuation into separate node classes. It is the **Wii-guitar
actuator node**: a PIC32CM PL10 T1S PLCA *follower* (node id 2) that receives marvin's 1-byte button
bitmask and drives a Wii guitar controller via open-drain GPIO — the actuation half of today's
[fretboard](../../fretboard/SPEC.md) firmware, on its own node.

Next step (G0/G1): Greg generates the guitar MCC project (a SERCOM in SPI-master mode + `IRQ_N`/`RST`
GPIO for the LAN8651, and the 7 button GPIOs), then the T1S-follower glue lands — the mirror of marvin's
coordinator glue in [`firmware/marvin/default/src/net/t1s/t1s_link.c`](../../marvin/default/src/net/t1s/t1s_link.c),
reusing [`third_party/oa-tc6-lib`](../../../third_party/oa-tc6-lib) and the shared L2 framing.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-16 | **Wii-guitar actuation becomes its own subproject (`guitar`), split out of fretboard.** PIC32CM PL10, T1S PLCA **follower** node id 2 (MAC `02:00:00:00:00:02`), receives a 1-byte bitmask over ethertype `0x88B5` and drives 7 open-drain Wii-guitar GPIOs. Sensing stays on `fretboard` (detector node). Actuation logic is a verbatim port of fretboard's `cmd_receive.c`. The link reuses `oa-tc6-lib` + the shared L2 framing as the mirror of marvin's coordinator glue. **Bare-metal** (no FreeRTOS on PL10): TC6 serviced from the main loop / tick; `IRQ_N` on a SERCOM-EIC pin. | The T1S bus was built for multiple node classes; separating detector from actuator lets multiple guitar/detector variants coexist on one PLCA pair with marvin selecting the active of each. Same MCU family as fretboard keeps the "OA SPI driver scales across the family" demo and minimizes bring-up. Greg spins up a *fresh* guitar MCC project (not a fork of fretboard). |
| 2026-06-16 | **Edge-ai / on-device model is out of scope; fretboard keeps actuating until guitar is proven.** This subproject covers only the marvin-driven actuation path. fretboard's MODEL_DRIVEN/standalone modes stay untouched; re-homing the model (detector infers → T1S → guitar) and the `applied_mask` training-label coupling are deferred. marvin flips its command target from the fretboard node to the guitar node only once G3 passes. | Contains the blast radius — the playing system stays up throughout, mirroring the UART/T1S parallel-coexistence approach. |

---

## Open questions

- **Guitar MCC pinout** — which SERCOM for the LAN8651 SPI, the `IRQ_N`/`RST` pins, and the 7 button GPIOs on the PL10 board. Settle when the MCC project is generated (G0).
- **CS handling** — whether the chosen SERCOM holds SS low across a full TC6 chunk (like marvin's FLEXCOM CSAAT) or `CS_N` must be bit-banged as GPIO. Confirm at G1 bring-up.
- **Active-guitar selection on marvin** — how marvin picks which guitar node is active (mirror of `Detector_SetActive`); designed on the marvin side, tracked there.

---

## Session log

### 2026-06-16 — Subproject created (skeleton + design)

- Created `firmware/guitar/` (SPEC.md, this journal, README) as part of the T1S node-class restructuring. Recorded the actuator-node design: PIC32CM PL10, PLCA follower id 2, command RX → Wii GPIO (port of fretboard `cmd_receive.c`), reusing `oa-tc6-lib` + the shared L2 framing.
- Registered in top-level [`SPEC.md`](../../SPEC.md) (§2 node classes, §3 registry, §4 map, §5 hardware) and [`CLAUDE.md`](../../CLAUDE.md); node id 2 / MAC `02:..:02` added to [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1.
- **No firmware code yet.** Next: G0 (MCC project) then G1 (follower bring-up).
