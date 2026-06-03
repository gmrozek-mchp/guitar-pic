# edge-ai — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the edge-ai distillation effort. Newest entries at the top. For *what edge-ai is* (purpose, the distillation shape, phased rollout), read [`SPEC.md`](SPEC.md) and the doc map there — this journal does not duplicate it.

---

## Current focus

**Phase 1 — Data pipeline.** Get distillation labels available end-to-end: a `--labels=actuator` export mode in [`tools/marvin-perf`](../../marvin-perf/) that turns a marvin self-play perf-log capture into a SensiML CSV of `5 ADC + 5 fret + 1 collapsed-strum` per row. See [`rollout.md`](rollout.md) for the phase definitions and verification gates.

Exporter code + tests landed 2026-06-03 (offline, no firmware change). The remaining Phase-1 gate is the *verification* step: run the new mode against a real self-play capture and confirm row count ≈ 240 × duration, strum bits sparse, fret bits dense and matching difficulty. That needs a real recording (see open questions).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-03 | Phase 1 exporter implemented as a `--labels={detector,actuator}` switch on the existing `export-ml` command rather than a new subcommand. Default stays `detector` (back-compat); `actuator` emits the `fret_*,strum` schema. | The two modes share the whole capture-read / timestamp / forward-fill loop; only the label-source record (`Detector` vs `Actuator`) and the output columns differ. A flag keeps one code path and one set of edge-case handling (missing SESSION, pre-label rows, strict mode). The detector path's CSV bytes are unchanged, so the existing corpus/tooling keeps working. |
| 2026-06-03 | `n_strum_events` stat counts **rising edges** (0→1 transitions) of the collapsed strum bit, not strum-active rows. | "Events" = distinct strums, which is the quantity to sanity-check against a song's note count. Per-row density ("a few %") is still eyeballable from the CSV; the rising-edge count is the more information-dense single number and doesn't conflate hold-duration with strum count. |
| 2026-06-03 | Collapsed strum bit computed from `Actuator.intended_mask` bits 5\|6 (the doc formula), not from the separate `strum_dir` field. | `intended_mask` is the actual 7-bit wire byte marvin sent (`FretboardLink_Send` value at [fretboard_link.c:430](../../../firmware/marvin/default/src/actuator/fretboard_link.c#L430)); distilling against the byte-on-the-wire is the whole point. `strum_dir` is a redundant convenience enum. Using the mask keeps the label = exactly what fretboard saw. |
| 2026-06-03 | ACTUATOR producer wiring confirmed present in marvin firmware before building the exporter. | `PerfLog_EmitActuator` is called unconditionally inside `FretboardLink_Send` ([fretboard_link.c:430](../../../firmware/marvin/default/src/actuator/fretboard_link.c#L430)), which both `timing_pipeline` and `manual_control` drive. So any self-play capture with the `ACTUATOR` (0x0A) mask bit set carries `intended_mask` + `strum_dir` per Send — no firmware work needed for Phase 1. |
| 2026-06-03 | Phase 2 windowing slices by **row index** (60 consecutive rows = one 250 ms window), **not** by the CSV `timestamp` column. Proceed assuming a uniform 240 Hz cadence. | The first real capture revealed `FretboardRaw` timestamps are bursty (stamped at marvin's RX/emit time, not at fretboard sample time) — see the open-questions entry. Each row is still exactly one true fretboard sample at the fixed 240 Hz rate, so counting rows gives correct, jitter-free windows; slicing by wall-clock timestamp would not. The `timestamp` column stays useful for plotting/duration only. |

(Higher-level locked decisions — output shape, distillation supervision, deployment target — live in [`SPEC.md` §3](SPEC.md).)

---

## Open questions

> Phase-1-blocking items first; the design forks for phase 2+ live in [`review.md`](review.md).

- **Need a real self-play capture to close the Phase-1 gate.** The exporter is verified against synthetic records, but the rollout's Phase-1 "done when" requires inspecting one real captured-gameplay CSV. Required record types in the capture's `PERF_CMD_SET_TYPE_MASK`: `SESSION` + `ACTUATOR` + `FRETBOARD_RAW` (the exporter strictly needs only these three); `DETECTOR` recommended for cross-checking `pressed_mask` vs `intended_mask`. The existing files in [`tools/marvin-perf/captures/`](../../marvin-perf/captures/) are *already-exported detector CSVs*, not raw `.bin` captures, so they can't be re-exported through the actuator path — a fresh raw capture is needed.
- **Corpus variety for Phase 2.** Easy/Medium/Expert + song variety, with one held-out song reserved and reused across iterations. Not blocking Phase 1.

- **[Deferred — not fixing now] Fretboard ADC samples carry no true sample timestamp.** The first real self-play capture ([`tools/marvin-perf/captures/web-20260603-104406.csv`](../../marvin-perf/captures/web-20260603-104406.csv), 251.5 s, 59 958 rows) shows the per-row `timestamp` is **bursty, not uniform 240 Hz**: ~2.9 samples cluster within <0.05 ms, then a >10 ms gap (≈12 ms burst period), with *no* rows near the expected 4.17 ms spacing. Cause: `FretboardRaw.hdr.ts_counter` is stamped at marvin's USB-CDC RX/emit time, not at the fretboard's physical sample instant; the host receives the stream in bursts and the RX task drains buffered frames back-to-back.
  - **Consequences:** (1) row-index windowing is mandatory (see decision log); (2) forward-filled command labels can skew by up to one burst (~12 ms / ~3 samples) near command transitions — same effect as the "CDC + RTOS jitter in labels" risk in [`review.md`](review.md); measure its magnitude in Phase 2.
  - **Candidate fix (deferred):** have the fretboard MCU stamp each 12-byte data frame with its own sample-time counter and carry it on the wire, so marvin records the *absolute sample-time truth* instead of RX time. That's a fretboard wire-format + firmware change (and a `FretboardRaw` schema add on marvin) — out of scope for now. **For the moment we proceed assuming regular 240 Hz intervals and window by row count.** Revisit if Phase 2 shows the label skew hurts strum-timing accuracy.

---

## Session log

### 2026-06-03 — Phase 1 exporter built

- Confirmed ACTUATOR records are wired in marvin firmware (see decision log) — Phase 1 has no firmware dependency.
- Added `--labels=actuator` mode to [`marvin_perf/exporters/sensiml_csv.py`](../../marvin-perf/marvin_perf/exporters/sensiml_csv.py): new `fret_green..fret_orange,strum` schema, forward-fills `Actuator.intended_mask`, collapses strum-down\|strum-up into one bit, reports `n_actuator_records` + `n_strum_events`. Default `detector` mode unchanged.
- Added `build_actuator_payload` to [`tests/conftest.py`](../../marvin-perf/tests/conftest.py) and a suite of actuator-path tests (schema, fret+strum labels, rising-edge event count, strict/non-strict pre-label handling, unknown-mode error, detector back-compat). Full suite: 90 passed (the 2 viewer tests need the optional `fastapi` dep group and were excluded).
- CLI smoke test confirmed end-to-end export + summary line + exit codes.
- **First real capture exported and inspected** ([`captures/web-20260603-104406.csv`](../../marvin-perf/captures/web-20260603-104406.csv)): 251.5 s, 59 958 rows (~238 Hz), 551 strum events / 3 496 strum-active rows (~5.8 % — sparse as expected), frets pressed in 41 135 rows (dense). First strum at t≈6.73 s (the intro is strum-free, which initially read as "no strums" when only the top of the file was visible). **Phase-1 verification checklist passes.**
- Found and logged the fretboard-timestamp burstiness issue (see open questions + decision log) — deferred; proceeding with row-index windowing at an assumed uniform 240 Hz.
- **Next:** Phase 2 — host-side baseline training. Gather corpus variety (Easy/Medium/Expert, multiple songs, one held-out song), then train per [`training.md`](training.md) §5 and hit the Phase-2 gate (≥95 % per-bit accuracy, strum p95 ≤ 20 ms) in [`rollout.md`](rollout.md).
