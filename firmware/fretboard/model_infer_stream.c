/* Streaming inference. Compiled only when streaming is ON, so when recompute is
 * selected this translation unit is empty and allocates no static buffers. */
#include "fretboard_config.h"
#if MODEL_INFER_STREAMING

#include "model_infer_stream.h"
#include "model_weights.h"

#include <string.h>

/* Streaming cascade is written for the fixed 3-layer, kernel-5 arch. Ring depths
 * below assume dilations (1, 4, 16); a different arch needs them resized. */
#if MODEL_N_LAYERS != 3 || MODEL_KERNEL != 5 || MODEL_N_IN != 5 || MODEL_N_OUT != 6
#error "model_infer_stream.c assumes 3 layers, kernel 5, 5 inputs, 6 outputs"
#endif

/* Streaming computes a continuous causal conv, bit-exact only with a model whose
 * last timestep needs no zero-padding — i.e. trained at window >= receptive field
 * (1 + (K-1)*(1+4+16) = 85 for this arch). Guard against deploying a shorter one. */
_Static_assert(MODEL_WINDOW >= 85, "streaming needs a model trained at window >= 85 (the receptive field)");

#define QMAX 127

/* Each layer's ring must hold the deepest tap its consumer reads:
 *   q_in feeds L0 (dil 1):  (K-1)*1  = 4  -> 5
 *   L0   feeds L1 (dil 4):  (K-1)*4  = 16 -> 17
 *   L1   feeds L2 (dil 16): (K-1)*16 = 64 -> 65 */
#define Q_RING  5
#define L0_RING 17
#define L1_RING 65

static const model_def_t *s_model;
static uint32_t s_rf;       /* receptive field; output valid once s_count >= s_rf */
static uint32_t s_count;    /* samples processed (for warm-up tap zeroing) */

static int8_t s_qin[Q_RING][MODEL_N_IN];
static int8_t s_l0[L0_RING][MODEL_CHANNELS];
static int8_t s_l1[L1_RING][MODEL_CHANNELS];
static int s_qh, s_l0h, s_l1h;   /* index of the newest column in each ring */

/* strum monostable (advances once per sample == 240 Hz) */
static uint32_t s_tick, s_hold_until, s_block_until;
static uint8_t  s_prev_strum;

void model_infer_stream_set_model(const model_def_t *m)
{
    s_model = m;
    if (m == NULL) { return; }
    s_rf = 1u;
    for (int l = 0; l < MODEL_N_LAYERS; l++)
    {
        s_rf += (uint32_t)(MODEL_KERNEL - 1) * m->dilation[l];
    }
}

void model_infer_stream_init(void)
{
    s_count = 0;
    s_qh = s_l0h = s_l1h = 0;
    s_tick = 0;
    s_hold_until = 0;
    s_block_until = 0;
    s_prev_strum = 0;
    memset(s_qin, 0, sizeof(s_qin));
    memset(s_l0, 0, sizeof(s_l0));
    memset(s_l1, 0, sizeof(s_l1));
    model_infer_stream_set_model(MODEL_DEFAULT);
}

/* One output column (all MODEL_CHANNELS) at the newest position, reading the
 * previous layer's ring. Tap k reads offset (K-1-k)*dil columns back from `head`;
 * offsets >= s_count fall before the stream start and are zero (warm-up). Matches
 * the causal-left-pad convention of edge_ai.quantize.int8_sim. */
static void conv_col(const int8_t *w, const int32_t *b, int cin, int dil,
                     int32_t mult, int shift, const int8_t *ring, int ring_len,
                     int stride, int head, int8_t out[MODEL_CHANNELS])
{
    /* Tap ring index depends only on k (not the channels) — hoist it out of the
     * hot loop so the (non-power-of-two) modulo runs K times per column, not
     * C*Cin*K times (the M0+ has no hardware divide). -1 = before stream start. */
    int tap[MODEL_KERNEL];
    for (int k = 0; k < MODEL_KERNEL; k++)
    {
        int off = (MODEL_KERNEL - 1 - k) * dil;
        tap[k] = ((uint32_t)off >= s_count)
                     ? -1
                     : ((head - off) % ring_len + ring_len) % ring_len;
    }

    for (int o = 0; o < MODEL_CHANNELS; o++)
    {
        int32_t acc = b[o];
        for (int i = 0; i < cin; i++)
        {
            const int8_t *wo = &w[(o * cin + i) * MODEL_KERNEL];
            for (int k = 0; k < MODEL_KERNEL; k++)
            {
                if (tap[k] < 0) { continue; }
                acc += (int32_t)wo[k] * (int32_t)ring[tap[k] * stride + i];
            }
        }
        if (acc < 0) { acc = 0; }   /* ReLU */
        int64_t q = ((int64_t)acc * mult + ((int64_t)1 << (shift - 1))) >> shift;
        if (q > QMAX) { q = QMAX; }
        out[o] = (int8_t)q;
    }
}

uint8_t model_infer_stream_step(const uint16_t adc[5])
{
    const model_def_t *m = s_model;
    if (m == NULL) { return 0u; }

    s_count++;

    /* input affine -> new q_in column */
    s_qh = (s_qh + 1) % Q_RING;
    for (int c = 0; c < MODEL_N_IN; c++)
    {
        int64_t v = (int64_t)adc[c] * m->m_in[c] - m->b_in[c];
        int64_t q = (v + ((int64_t)1 << (m->s_in_shift - 1))) >> m->s_in_shift;
        if (q > QMAX) q = QMAX; else if (q < -QMAX) q = -QMAX;
        s_qin[s_qh][c] = (int8_t)q;
    }

    /* layer 0: reads q_in ring */
    s_l0h = (s_l0h + 1) % L0_RING;
    conv_col(m->w[0], m->b[0], m->cin[0], m->dilation[0], m->req_mult[0],
             m->req_shift[0], &s_qin[0][0], Q_RING, MODEL_N_IN, s_qh, s_l0[s_l0h]);

    /* layer 1: reads L0 ring */
    s_l1h = (s_l1h + 1) % L1_RING;
    conv_col(m->w[1], m->b[1], m->cin[1], m->dilation[1], m->req_mult[1],
             m->req_shift[1], &s_l0[0][0], L0_RING, MODEL_CHANNELS, s_l0h, s_l1[s_l1h]);

    /* layer 2: reads L1 ring -> newest column only */
    int8_t l2[MODEL_CHANNELS];
    conv_col(m->w[2], m->b[2], m->cin[2], m->dilation[2], m->req_mult[2],
             m->req_shift[2], &s_l1[0][0], L1_RING, MODEL_CHANNELS, s_l1h, l2);

    if (s_count < s_rf) { return 0u; }   /* receptive field not yet filled */

    /* head: bit = (logit >= threshold) */
    uint8_t mask = 0u;
    int raw_strum = 0;
    for (int o = 0; o < MODEL_N_OUT; o++)
    {
        int32_t acc = m->head_b[o];
        for (int i = 0; i < MODEL_CHANNELS; i++)
        {
            acc += (int32_t)m->head_w[o * MODEL_CHANNELS + i] * (int32_t)l2[i];
        }
        int bit = (acc >= m->head_tq[o]) ? 1 : 0;
        if (o < 5) { if (bit) { mask |= (uint8_t)(1u << o); } }
        else       { raw_strum = bit; }
    }

    /* strum monostable (one pulse per rising edge), advanced once per sample */
    int strum_out;
    if (m->strum_hold > 0)
    {
        uint32_t i = s_tick;
        if (raw_strum && !s_prev_strum && i >= s_block_until)
        {
            s_hold_until  = i + m->strum_hold;
            s_block_until = i + m->strum_hold + m->strum_refractory;
        }
        strum_out = (i < s_hold_until) ? 1 : 0;
        s_prev_strum = (uint8_t)raw_strum;
        s_tick++;
    }
    else
    {
        strum_out = raw_strum;
    }
    if (strum_out) { mask |= (uint8_t)(1u << 5); }

    return mask;
}

#endif /* MODEL_INFER_STREAMING */
