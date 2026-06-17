#ifndef FRETBOARD_CONFIG_H
#define FRETBOARD_CONFIG_H

/*
 * Build-time configuration shared by main.c and the inference modules.
 *
 * The fretboard is a detector-only node — it scans the 5 phototransistors and
 * streams them; it has no Wii-guitar outputs (those moved to the `guitar` node).
 *
 * Operating mode:
 *   MARVIN_DRIVEN — stream the raw scan only (no on-device model). [legacy name]
 *   MODEL_DRIVEN  — also run the on-device model and stream its inferred bitmask
 *                   as telemetry (it does NOT actuate; Stage 2 routes the mask to
 *                   a guitar node over T1S).
 *
 * Inference implementation (MODEL_DRIVEN only):
 *   0 = recompute (model_infer.c)        — works at any trained window; ~93 Hz
 *       at 16ch (re-runs the receptive field each tick).
 *   1 = streaming  (model_infer_stream.c) — caches per-layer columns, locks to
 *       the 240 Hz sample rate; REQUIRES a model trained at window >= the
 *       receptive field (85). (default)
 *
 * Both model_infer.c and model_infer_stream.c can stay in the project: each
 * guards its body on MODEL_INFER_STREAMING, so only the active one compiles its
 * static buffers (the inactive TU is empty — no .bss). Flip the mode here (or
 * with -D) — no add/remove of source files.
 */

#define MARVIN_DRIVEN 0
#define MODEL_DRIVEN  1
#ifndef FRETBOARD_MODE
#define FRETBOARD_MODE MODEL_DRIVEN
#endif

#ifndef MODEL_INFER_STREAMING
#define MODEL_INFER_STREAMING 1
#endif

/*
 * Marvin link transport (orthogonal to FRETBOARD_MODE):
 *   UART — the 17/21-byte frame streams out SERCOM1 (today's behaviour).
 *   T1S  — fretboard is a 10BASE-T1S detector node (id 1): it streams the same
 *          17-byte frame to the marvin coordinator over the LAN8651 MAC-PHY and
 *          announces presence with a heartbeat. See t1s_detector.{c,h} and
 *          docs/t1s-podl-link.md.
 *
 * The T1S build also compiles out the on-device model (the freed button GPIOs are
 * the LAN8651 SPI/CS/IRQ/RST pins, and the model is a UART-bench telemetry path for
 * now). Re-homing the model over T1S (infer -> command TX straight to a guitar
 * node) is Stage 2.
 */
#define FRETBOARD_LINK_UART 0
#define FRETBOARD_LINK_T1S  1
#ifndef FRETBOARD_LINK
#define FRETBOARD_LINK FRETBOARD_LINK_UART
#endif

#endif /* FRETBOARD_CONFIG_H */
