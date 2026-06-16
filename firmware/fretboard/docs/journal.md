# fretboard — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the fretboard firmware. Newest entries at the top. For *what fretboard is* (purpose, hardware, modules, frame format), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Two build-time modes (`FRETBOARD_MODE` in `main.c`):**

- **`MARVIN_DRIVEN`** (I/O bridge) — 240 Hz TC0 callback: scan five ADCs, emit a 17-byte frame on SERCOM1 TX, drain RX and apply the latest button bitmask. Frame carries `sample_seq` + the applied actuator bitmask (edge-ai training-data sync). Chord/strum logic off-board.
- **`MODEL_DRIVEN`** (new, current default — standalone edge-ai inference) — the on-device int8 model ([`model_infer.c`](../model_infer.c), weights in [`model_weights.h`](../model_weights.h)) reads the ADC window and drives the buttons itself; marvin disconnected. **SW0 (PB03)** toggles model control, **LED0 (PB02)** shows enabled (boots disabled). The 240 Hz TC0 ISR only samples + applies the latest command + streams; `model_infer_run()` runs in the **main loop** (it overruns the tick if put in the ISR — see decision log + edge-ai `runtime.md` §3). Build at `-O2`/`-O3`.

Baud is 500 000. Host checks: [`tools/ds_monitor.py`](../tools/ds_monitor.py) (framing) and [`../../../tools/marvin-perf/fretboard_rate.py`](../../../tools/marvin-perf/fretboard_rate.py) (true tick rate from `sample_seq` + strums/s).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-04 | **Added `MODEL_DRIVEN` mode: on-device int8 inference (`model_infer.c/.h` + generated `model_weights.h`), SW0/LED0 enable toggle, and inference run in the main loop — NOT the TC0 ISR.** Active model is a swappable `model_def_t` pointer (`model_infer_set_model`). Build at `-O2`/`-O3`. | Standalone bring-up of the edge-ai model (edge-ai journal 2026-06-04). **Inference must not run in the 240 Hz ISR**: measured on hardware, doing so dropped the callback rate to ~150–190 Hz (so the model's 250 ms window/lag went wrong) *and* starved the interrupt-driven SERCOM TX (recv ~22 fps, heavy drops). Moving `model_infer_run()` to the main loop and having the ISR only sample + apply a `volatile uint8_t` latest-command restored clean 240/240 Hz. The pointer-swap model_def lets per-difficulty weights be selected at runtime and keeps the door open for serial-loaded weights (RAM headroom confirmed; 8 KB SRAM). SW0 (PB03 active-low momentary, debounced) toggles control; LED0 (PB02) shows state; boots disabled (outputs released). `MARVIN_DRIVEN` stays the alternate build for the I/O-bridge path. |
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

3. **T1S link transport — direction, not yet built.** Plan to move the marvin link from the SERCOM1 UART to **10BASE-T1S single-pair Ethernet + dumb PoDL** via a LAN8651B1 MAC-PHY (system spec §6 + [`../../../docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md)). Fretboard-side cost is an OA TC6 SPI driver + minimal L2 framing (~6–10 KB flash / ~1–2 KB SRAM, one free SERCOM in SPI mode + CS_N/IRQ_N/reset GPIO); the 17-byte data frame + 1-byte command formats ride unchanged inside the Ethernet payload, so `data_stream.c`/`cmd_receive.c` need no logic change — only the transport under them swaps. Scope settled on the marvin side 2026-06-16 (marvin journal): a **multi-node PLCA bus** (marvin = coordinator ID 0; fretboard = ID 1, MAC `02:00:00:00:00:01`), adapt `oa-tc6-lib` as a **shared portable TC6 + L2 layer** (the M0+ side reuses what marvin builds), one custom ethertype (~`0x88B5`, TBC), and the UART kept in parallel behind a build flag. The PIC32CM-side work follows once marvin's link is proven; `data_stream.c`/`cmd_receive.c` still need no logic change. PoDL is transparent to the MCU (zero firmware footprint). Remaining open: final ethertype/MAC values, PoDL BOM.

> Resolved 2026-06-03 (see decision log): #3 "no sample timestamp" and #4 "no applied actuator state" — both fixed by growing the frame to 17 bytes with `sample_seq` + `applied_mask`.

---

## Session log

### 2026-06-09 — T1S + PoDL link direction documented

- Evaluated moving the marvin link from SERCOM1 UART to **10BASE-T1S + dumb PoDL** (LAN8651B1 MAC-PHY each end). Feasible on the PIC32CM PL10: no IP stack, just SPI + the OPEN Alliance TC6 chunk protocol + a 14-byte L2 header; the existing 17-byte/1-byte frame formats ride inside the Ethernet payload unchanged. Est. ~6–10 KB flash / ~1–2 KB SRAM, one free SERCOM (SPI) + CS_N/IRQ_N/reset. Motivation: PoDL (power+data on one pair), noise/cable tolerance, PLCA multidrop, and a Microchip T1S+PoDL system demonstration — *not* bandwidth (UART has ~12× headroom).
- Documented system-level in [`../../../SPEC.md`](../../../SPEC.md) §5/§6/§7 and low-level in new [`../../../docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md). Added open-question #3 here; mirrored a note in the marvin journal (it gets its own bare-metal TC6 driver — no free netdev).
- **No code changes.** Direction only; revisit open sub-items (PLCA vs p2p, ethertype/MACs, UART fallback) before implementation.

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
