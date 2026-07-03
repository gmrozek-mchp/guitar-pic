# guitar

Wii-guitar **actuator node** for the Guitar Hero bot's T1S bus. A PIC32CM PL10
(10BASE-T1S PLCA follower) that receives a 1-byte button bitmask from
[marvin](../marvin/) and drives a real Wii guitar controller via open-drain GPIO.

It is the actuation half of the original [fretboard](../fretboard/) firmware, split
onto its own node so sensing (detector nodes) and actuation (guitar nodes) are
separate node classes on one PLCA bus.

- **What it is / firmware design:** [`SPEC.md`](SPEC.md)
- **Decisions + progress:** [`docs/journal.md`](docs/journal.md)
- **System context:** top-level [`SPEC.md`](../../SPEC.md), [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md)

Status: working on hardware — the T1S PLCA follower is up, receives and actuates
marvin's command over T1S, and runs a presence heartbeat plus a debug-UART CLI.
See [`SPEC.md`](SPEC.md) for the current milestone state.
