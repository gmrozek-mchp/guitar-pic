#ifndef FRETBOARD_CONFIG_H
#define FRETBOARD_CONFIG_H

/*
 * Build-time configuration shared by main.c and the inference modules.
 *
 * Operating mode:
 *   MARVIN_DRIVEN — apply the command byte streamed from marvin over SERCOM1.
 *   MODEL_DRIVEN  — run the on-device model standalone (marvin disconnected).
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

#endif /* FRETBOARD_CONFIG_H */
