# fretboard — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the fretboard firmware. Newest entries at the top. For *what fretboard is* (purpose, hardware, modules, frame format), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Pure I/O bridge.** Loop runs at 240 Hz from a TC0 timer callback: scan five ADCs, emit a 17-byte data frame on SERCOM1 TX, drain SERCOM1 RX and apply the latest button bitmask. The frame now carries a monotonic sample-sequence counter and the currently-applied actuator bitmask alongside the ADC values (for edge-ai training-data sync — see decision log). All chord / strum / SW0 / LED logic lives off-board (marvin or fret-tuner). Baud is 500 000.

Today's session added [`tools/ds_monitor.py`](../tools/ds_monitor.py), a host-side framing/rate sanity-check, and rewrote [`../SPEC.md`](../SPEC.md) to match the new I/O-bridge architecture (the old version still described 500 Hz + 17-byte frames + on-device `fret_detect` / `fret_button`).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-03 | Data-stream frame grows 12→17 bytes: `0x03 \| g r y b o (5×u16 LE) \| sample_seq (u32 LE) \| applied_mask (u8) \| 0xFC`. `sample_seq` is a monotonic counter incremented once per tick in `data_stream_send` (before the TX-buffer check, so a dropped send shows as a gap); `applied_mask` comes from a new `cmd_receive_current_mask()` getter. Resolves open-questions #3 and #4. | Closes the edge-ai data-sync hole (edge-ai journal 2026-06-03). Pairing the actuator state with the ADC scan *in the same frame* makes label↔feature alignment atomic at the source, instead of marvin reconstructing it across its bursty USB RX and separate TX clocks — which Phase-2 training showed floored strum timing at ~20 ms. The seq counter lets the host reconstruct true 240 Hz ordering and detect dropped frames. Callback order is scan→send→receive, so `current_mask` at send time is exactly the state driven *during* this scan. Wire-format break: requires marvin's RX parser + `perf_rec_fretboard_raw_t` (schema v4) to update in lockstep. 17 B × 240 Hz = 4.08 KB/s, still ~12× under the 500 000-baud budget. |
| 2026-06-02 | Leave the orphaned `fret_detect.*` and `fret_button.*` modules in place (still in MPLAB fileSet, still building, no callers). Don't delete, don't move to a `fallback/` directory. | Marvin's 2026-05-20 decision keeps a "fretboard-takeover" fallback mode in scope — `fret_button.c` is the existing implementation of that mode and is cheap to keep around. Cost is small (a few KB of flash + the stale `#include "fret_detect.h"` in `data_stream.c`). Revisit if the takeover mode is formally dropped or if these files start drifting against a refactor. |
| 2026-06-02 | Loop period moved to 240 Hz, driven by TC0 callback (was 500 Hz from SYSTICK). | Game logic now runs on marvin; the host is the rate-setter and 240 Hz comfortably covers Guitar Hero note-onset timing while leaving SAM9X75 RX-side budget. TC0 callback removes any drift from a polled-SYSTICK loop. |
| 2026-06-02 | SERCOM1 baud raised to 500 000 (was lower). | At 240 Hz × 12-byte TX frames + sporadic RX command bytes, 500 000 baud (≈ 50 000 B/s usable) gives ~17× headroom over the 2 880 B/s steady-state — plenty for jitter and back-pressure without flow control. |
| 2026-06-02 | Data-stream frame is 12 bytes: `0x03 | g | r | y | b | o | 0xFC` (5×u16 LE). | Pressed-state booleans removed; with detection on the host, raw ADC values are the only payload that matters. Smaller frame ⇒ more TX headroom, simpler host parser. |
| 2026-06-02 | Command receive: drain RX each tick, apply only the **last byte** as a 7-bit button bitmask. | The host is expected to send commands at ≤ tick rate; coalescing avoids working through stale bitmasks if anything backs up. Single-byte format means no framing/CRC overhead in either direction — corruption window is one tick at worst. |

---

## Open questions

1. **No corruption signalling on the wire.** A bit-flip inside the 5 uint16 payload bytes is silently accepted — only out-of-range start/end bytes are caught. At 500 000 baud over EDBG-CDC this is probably fine, but if we ever see suspect detector behaviour, adding a Fletcher-16 byte (matching marvin's wire format) is a one-byte frame growth.

2. **No host→firmware framing.** Command stream is raw bitmask bytes with no start byte. A spurious byte (e.g. line glitch on RX) becomes a button command. Acceptable for now because the line is short and runs over the same EDBG-CDC pair as TX, but worth revisiting if we see ghost presses.

> Resolved 2026-06-03 (see decision log): #3 "no sample timestamp" and #4 "no applied actuator state" — both fixed by growing the frame to 17 bytes with `sample_seq` + `applied_mask`.

---

## Session log

### 2026-06-03 — 17-byte frame: sample_seq + applied_mask for edge-ai sync

- Grew the data frame 12→17 B (decision log). `data_stream.c`: added `sample_seq` (file-static `s_sample_seq`, incremented per tick before the TX-buffer guard) and `applied_mask` (from the new `cmd_receive_current_mask()` getter); `_Static_assert` 17; included `cmd_receive.h`. `cmd_receive.{c,h}`: exposed `current_mask` via the getter.
- Lockstep partners updated the same day: marvin RX parser (`fretboard_link.c`, `DS_FRAME_LEN` 12→17, extract seq+mask) and `perf_rec_fretboard_raw_t` (perf-log schema **v4** — see marvin journal), host `marvin-perf` decoder, and the `--labels=actuator-fb` exporter + edge-ai loader (windows within contiguous `fb_seq` runs).
- **Not yet built/flashed** — needs MPLAB + the rig. Deploy fretboard + marvin together (wire-format break). Then re-capture the Expert corpus and A/B the new atomic labels against the old cross-stream join (edge-ai journal).

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
