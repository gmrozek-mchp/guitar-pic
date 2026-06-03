# fretboard — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the fretboard firmware. Newest entries at the top. For *what fretboard is* (purpose, hardware, modules, frame format), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Pure I/O bridge.** Loop runs at 240 Hz from a TC0 timer callback: scan five ADCs, emit a 12-byte data frame on SERCOM1 TX, drain SERCOM1 RX and apply the latest button bitmask. All chord / strum / SW0 / LED logic lives off-board (marvin or fret-tuner). Baud bumped to 500 000 to keep TX headroom comfortable at the new tick rate.

Today's session added [`tools/ds_monitor.py`](../tools/ds_monitor.py), a host-side framing/rate sanity-check, and rewrote [`../SPEC.md`](../SPEC.md) to match the new I/O-bridge architecture (the old version still described 500 Hz + 17-byte frames + on-device `fret_detect` / `fret_button`).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-02 | Leave the orphaned `fret_detect.*` and `fret_button.*` modules in place (still in MPLAB fileSet, still building, no callers). Don't delete, don't move to a `fallback/` directory. | Marvin's 2026-05-20 decision keeps a "fretboard-takeover" fallback mode in scope — `fret_button.c` is the existing implementation of that mode and is cheap to keep around. Cost is small (a few KB of flash + the stale `#include "fret_detect.h"` in `data_stream.c`). Revisit if the takeover mode is formally dropped or if these files start drifting against a refactor. |
| 2026-06-02 | Loop period moved to 240 Hz, driven by TC0 callback (was 500 Hz from SYSTICK). | Game logic now runs on marvin; the host is the rate-setter and 240 Hz comfortably covers Guitar Hero note-onset timing while leaving SAM9X75 RX-side budget. TC0 callback removes any drift from a polled-SYSTICK loop. |
| 2026-06-02 | SERCOM1 baud raised to 500 000 (was lower). | At 240 Hz × 12-byte TX frames + sporadic RX command bytes, 500 000 baud (≈ 50 000 B/s usable) gives ~17× headroom over the 2 880 B/s steady-state — plenty for jitter and back-pressure without flow control. |
| 2026-06-02 | Data-stream frame is 12 bytes: `0x03 | g | r | y | b | o | 0xFC` (5×u16 LE). | Pressed-state booleans removed; with detection on the host, raw ADC values are the only payload that matters. Smaller frame ⇒ more TX headroom, simpler host parser. |
| 2026-06-02 | Command receive: drain RX each tick, apply only the **last byte** as a 7-bit button bitmask. | The host is expected to send commands at ≤ tick rate; coalescing avoids working through stale bitmasks if anything backs up. Single-byte format means no framing/CRC overhead in either direction — corruption window is one tick at worst. |

---

## Open questions

1. **No corruption signalling on the wire.** A bit-flip inside the 5 uint16 payload bytes is silently accepted — only out-of-range start/end bytes are caught. At 500 000 baud over EDBG-CDC this is probably fine, but if we ever see suspect detector behaviour, adding a Fletcher-16 byte (matching marvin's wire format) is a one-byte frame growth.

2. **No host→firmware framing.** Command stream is raw bitmask bytes with no start byte. A spurious byte (e.g. line glitch on RX) becomes a button command. Acceptable for now because the line is short and runs over the same EDBG-CDC pair as TX, but worth revisiting if we see ghost presses.

3. **Data frame carries no sample timestamp (request from edge-ai).** The 12-byte frame has no notion of *when* the ADC scan happened — marvin timestamps each frame at USB-CDC RX time, which is bursty (~3 frames arrive together every ~12 ms, not evenly at 4.17 ms). The first edge-ai training capture surfaced this; see the edge-ai journal ([`tools/edge-ai/docs/journal.md`](../../../tools/edge-ai/docs/journal.md), 2026-06-03, "Fretboard ADC samples carry no true sample timestamp"). Candidate fix: stamp each frame with a fretboard-side sample-time counter and carry it on the wire, giving marvin the absolute sample-time truth. Cost: larger frame + a `FretboardRaw` schema add on marvin's perf-log side. Deferred — edge-ai is proceeding with row-index windowing at an assumed uniform 240 Hz; revisit only if that label skew measurably hurts training.

4. **Data frame carries no applied actuator state (request from edge-ai).** The TC0 callback scans the ADCs and applies the latest button bitmask in the same tick, but the outgoing frame reports only the ADC values — so the sensor data and the actuator state that was driven *during that scan* are never paired at the source. marvin currently reconstructs the pairing by joining the ADC stream (received over USB) against its own emitted-command stream (sent over USB) — two opposite directions with independent latency, so the join is skewed. Candidate fix (pairs with #3 — same frame-growth change): include the **currently-applied bitmask** in each data frame so (ADC scan, actuator state) is captured atomically on-device. For edge-ai this is the ideal training label (it's exactly the function a fretboard-resident model would replace). See edge-ai journal 2026-06-03, "Label↔feature pairing crosses two USB directions." Deferred together with #3.

---

## Session log

### 2026-06-03 — Edge-AI design proposal authored, then promoted to its own subproject

- Initial draft landed here as `edge_ai_spec.md`, then moved to its own top-level subproject at [`tools/edge-ai/`](../../../tools/edge-ai/) (docs in [`tools/edge-ai/docs/`](../../../tools/edge-ai/docs/) — SPEC + architecture + training + runtime + rollout + review). Rationale: the work is mostly an offline Python / training-pipeline effort; phases 1–4 don't touch fretboard firmware at all, only phase 5 (cut-over) does. Living under `firmware/fretboard/docs/` framed it as a near-term fretboard plan when it's really its own development effort.
- Locked decisions (preserved across the move): output is 6 bits (5 frets + 1 collapsed strum bit; the wire byte still uses bit 5 for strum-down, bit 6 stays 0 — the up/down collapse is *at export time*, the raw `PERF_REC_ACTUATOR` capture keeps both bits so a future human-trainer corpus can preserve direction). Supervision is distillation against `PERF_REC_ACTUATOR.intended_mask` from marvin self-play. Intended deployment target is the fretboard PIC32CM, but offline phases are MCU-agnostic.
- The shape was picked specifically to dissolve the spatial-mismatch issue between `cv_marvin_v1` (sampling at `y=311`) and the photoxistor row: distilling against marvin's commands means the +220 ms strum delay is baked into the labels, so the model just learns whatever delay maps photo dip → strum from a wide-enough causal window — no mechanical alignment needed.
- No code changes. Open questions surfaced in [`tools/edge-ai/docs/review.md`](../../../tools/edge-ai/docs/review.md) (Q1: hold-vs-edge channel per fret, Q2: AR feedback, Q3: look-ahead vs. now-cast, Q4: strike-line CV detector as alt label source, Q5: photoxistor placement). Awaiting reviewer pushback before any of phase 1 (exporter `--labels=actuator` mode) lands.
- Top-level [`SPEC.md`](../../../SPEC.md) §3 has a new edge-ai row pointing at the subproject.

### 2026-06-02 — I/O-bridge spec rewrite + ds_monitor

- Confirmed actual architecture by reading sources: `main.c` has only `fret_scan_all` / `data_stream_send` / `cmd_receive_update` in the TC0 callback; `fret_detect.c` and `fret_button.c` are present but unreferenced.
- Wrote [`tools/ds_monitor.py`](../tools/ds_monitor.py): pyserial-based monitor that resyncs on `0x03 ... 0xFC` framing, prints actual frame rate (with % drift vs 240 Hz expected), bytes/s, framing-error drops, and current ADC values once per second.
- Rewrote [`../SPEC.md`](../SPEC.md) to reflect the I/O-bridge model: TC0 @ 240 Hz, 12-byte TX frame, 1-byte RX bitmask command, 500 000 baud. Old SPEC described the now-extinct on-device chord/strum pipeline.
- Created this journal (per top-level `CLAUDE.md`).
