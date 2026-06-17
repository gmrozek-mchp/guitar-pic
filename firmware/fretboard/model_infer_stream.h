#ifndef MODEL_INFER_STREAM_H
#define MODEL_INFER_STREAM_H

#include <stdint.h>
#include "model_infer.h"   /* model_def_t */

/*
 * Streaming variant of the on-device inference. Instead of recomputing the whole
 * receptive field every tick (model_infer.c), it caches each layer's output
 * column in a small ring and advances ONE new column per sample — ~1 conv column
 * per layer per tick (~4x fewer MACs), so inference clears 240 Hz.
 *
 * Stateful and per-sample: call model_infer_stream_step() exactly once per ADC
 * scan, in order. It computes a CONTINUOUS causal conv, which is bit-exact with a
 * model TRAINED AT window >= receptive field (85) — at a shorter window the
 * windowed model's moving zero-pad boundary would not match (see runtime.md /
 * edge-ai journal). Output is 0 until the receptive field has filled.
 *
 * Output bitmask: bits 0..4 frets (G/R/Y/B/O), bit 5 strum-down, bit 6 strum-up.
 */

void model_infer_stream_init(void);

/* Select the active model (per-difficulty swap); recomputes the receptive field. */
void model_infer_stream_set_model(const model_def_t *m);

/* Process one 240 Hz scan and return the command bitmask. Advances the stream by
 * one sample (do not skip or repeat samples). */
uint8_t model_infer_stream_step(const uint16_t adc[5]);

#endif
