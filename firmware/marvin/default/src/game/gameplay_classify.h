#ifndef MARVIN_GAMEPLAY_CLASSIFY_H
#define MARVIN_GAMEPLAY_CLASSIFY_H

#include <stdint.h>

/* Pure screen-classification math, ported from the host prototype
 * (tools/gameplay) and driven by the generated game/gameplay_metadata.h tables.
 * Deliberately free of FreeRTOS / video dependencies so it compiles and can be
 * numerically cross-checked against the Python classifier on a host, then links
 * unchanged into the firmware game_task (gameplay_engine.c). Not reentrant — it
 * uses a static fingerprint scratch; only the single observer task calls it. */

/* Classify a BGR888 frame (3 B/pixel, width*height). Returns a GP_SCREEN_*
 * index (see gameplay_metadata.h) or GP_SCREEN_UNKNOWN. The frame must be at the
 * canonical GP_CANON_W × GP_CANON_H (no resize on-target — the Wii is 720×480);
 * any other size returns GP_SCREEN_UNKNOWN. When non-NULL, *out_best_dist and
 * *out_margin receive the nearest-centroid L1 distance and the second-best−best
 * margin. */
uint8_t gp_classify(const uint8_t *frame, int width, int height,
                    int32_t *out_best_dist, int32_t *out_margin);

/* Compute the raw fingerprint (GP_FP_LEN uint8 values) for a canonical frame,
 * without classifying — exposed for the host cross-validation test. Returns 0
 * on success, non-zero if the frame isn't the canonical size. */
int gp_fingerprint(const uint8_t *frame, int width, int height, uint8_t *out);

#endif
