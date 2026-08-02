#ifndef MODEL_INFER_H
#define MODEL_INFER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * On-device int8 inference: 5-channel ADC window -> command bitmask.
 *
 * Drives the fretboard's actuation: the inferred bitmask is sent to the guitar
 * node over T1S (see t1s_detector.c). Integer-only (no FPU); weights and quant
 * params live in the generated model_weights.h as one or more `model_def_t`.
 * Bit-exact with the host reference edge_ai.quantize.int8_sim — see
 * tools/edge-ai/docs/journal.md.
 *
 * Output bitmask: bits 0..4 frets (G/R/Y/B/O), bit 5 strum-down, bit 6 strum-up
 * (the layout the guitar node applies).
 *
 * Multiple models (e.g. one per difficulty) can be linked at once and selected
 * at runtime with model_infer_set_model(); they must share the architecture
 * dims (window / channels / kernel / layer count) — model_weights.h _Static_asserts
 * this. Only the weights, scales, thresholds and dilations vary per model.
 */

/* All per-model int8 parameters. Arrays live in model_weights.h; this just
 * points at them so the active model is a single swappable pointer. Sizes are
 * the shared MODEL_* dims (see model_weights.h). */
typedef struct {
    const int32_t        *m_in;        /* [N_IN]  input-affine multipliers */
    const int32_t        *b_in;        /* [N_IN]  input-affine offsets */
    uint8_t               s_in_shift;
    const int8_t  *const *w;           /* [N_LAYERS] flat conv weights [Cout*Cin*K] */
    const int32_t *const *b;           /* [N_LAYERS] conv biases [Cout] */
    const uint8_t        *cin;         /* [N_LAYERS] input channels per layer */
    const uint8_t        *dilation;    /* [N_LAYERS] */
    const int32_t        *req_mult;    /* [N_LAYERS] requant multipliers */
    const uint8_t        *req_shift;   /* [N_LAYERS] requant shifts */
    const int8_t         *head_w;      /* [N_OUT*CHANNELS] */
    const int32_t        *head_b;      /* [N_OUT] */
    const int32_t        *head_tq;     /* [N_OUT] integer output thresholds */
    uint8_t               strum_hold;
    uint8_t               strum_refractory;
} model_def_t;

/* Runtime model selection: four Guitar Hero difficulties plus a reserved AUTO
 * slot (an adaptive-selection policy — currently resolves to hard). The concrete
 * difficulties occupy indices 0..MODEL_SEL_DIFFICULTIES-1; AUTO is last. The
 * difficulty->model registry and the AUTO policy live in models.h. */
enum {
    MODEL_SEL_EASY = 0,
    MODEL_SEL_MEDIUM,
    MODEL_SEL_HARD,
    MODEL_SEL_EXPERT,
    MODEL_SEL_DIFFICULTIES,             /* count of concrete difficulties (4) */
    MODEL_SEL_AUTO = MODEL_SEL_DIFFICULTIES,
    MODEL_SEL_COUNT,                    /* total selectable slots (5) */
};
#define MODEL_SEL_DEFAULT  MODEL_SEL_HARD   /* boots hard (the one trained model) */

void model_infer_init(void);

/* Select the active model (e.g. per difficulty). Recomputes the needed-position
 * table; keeps the input ring so a mid-stream swap doesn't re-warm. */
void model_infer_set_model(const model_def_t *m);

/* Runtime selection wrapper (resolves a MODEL_SEL_* index to a model via models.h
 * and applies it to the active engine). Set by the local CLI or marvin's control
 * channel; boots MODEL_SEL_DEFAULT. */
void        model_infer_set_sel(uint8_t sel);
uint8_t     model_infer_get_sel(void);          /* current selection index */
uint8_t     model_infer_effective(void);        /* concrete difficulty in effect (0..3) */
const char *model_infer_sel_name(uint8_t sel);  /* name for an index (NULL if out of range) */
bool        model_infer_sel_trained(uint8_t sel);/* false = placeholder (aliased to hard) */

/* Push one 240 Hz scan (5 channels, raw 12-bit ADC) into the input ring. */
void model_infer_push(const uint16_t adc[5]);

/* Run inference on the current window and return the command bitmask. Returns 0
 * (all released) until the ring has filled with MODEL_WINDOW samples. */
uint8_t model_infer_run(void);

#endif
