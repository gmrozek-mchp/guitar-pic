#include "model_infer.h"
#include "model_weights.h"

#include <stddef.h>
#include <string.h>

/* Architecture is fixed by the generated header; guard against a mismatched
 * regeneration that the hand-written cascade below wouldn't honour. */
#if MODEL_N_IN != 5 || MODEL_N_OUT != 6
#error "model_infer.c expects 5 ADC inputs and 6 outputs"
#endif

#define QMAX 127

/* The active model. Swappable at runtime (e.g. per difficulty); all candidate
 * models share the compile-time dims used to size the scratch below. */
static const model_def_t *s_model;

/* Input ring: WINDOW newest scans, oldest at window position 0. */
static uint16_t s_ring[MODEL_WINDOW][MODEL_N_IN];
static uint16_t s_pos;      /* next write slot (== oldest when full) */
static uint16_t s_count;    /* samples buffered, saturates at WINDOW */

/* Which positions each layer must evaluate to produce the last timestep only
 * (recomputed by set_model). Skips the dense per-position conv that would blow
 * the 4.17 ms tick. */
static uint8_t s_need[MODEL_N_LAYERS][MODEL_WINDOW];

/* Scratch: standardised int8 input, and per-layer int8 activations. */
static int8_t s_qin[MODEL_WINDOW][MODEL_N_IN];
static int8_t s_act[MODEL_N_LAYERS][MODEL_WINDOW][MODEL_CHANNELS];

/* Strum monostable state (deploy-time one-shot, model->strum_hold ticks). */
static uint32_t s_tick;
static uint32_t s_hold_until;
static uint32_t s_block_until;
static uint8_t  s_prev_strum;

static int8_t clamp_i8(int64_t v)
{
    if (v > QMAX)  return QMAX;
    if (v < -QMAX) return -QMAX;
    return (int8_t)v;
}

void model_infer_set_model(const model_def_t *m)
{
    s_model = m;
    if (m == NULL) { return; }

    /* Back-propagate the needed positions: the top layer needs only the last
     * timestep; each lower layer needs the taps its consumer reads. */
    memset(s_need, 0, sizeof(s_need));
    s_need[MODEL_N_LAYERS - 1][MODEL_WINDOW - 1] = 1u;
    for (int l = MODEL_N_LAYERS - 1; l >= 1; l--)
    {
        int dil = m->dilation[l];
        for (int p = 0; p < MODEL_WINDOW; p++)
        {
            if (!s_need[l][p]) { continue; }
            for (int j = 0; j < MODEL_KERNEL; j++)
            {
                int pos = p - j * dil;
                if (pos >= 0) { s_need[l - 1][pos] = 1u; }
            }
        }
    }
}

void model_infer_init(void)
{
    s_pos = 0;
    s_count = 0;
    s_tick = 0;
    s_hold_until = 0;
    s_block_until = 0;
    s_prev_strum = 0;
    model_infer_set_model(MODEL_DEFAULT);
}

void model_infer_push(const uint16_t adc[5])
{
    for (int c = 0; c < MODEL_N_IN; c++)
    {
        s_ring[s_pos][c] = adc[c];
    }
    s_pos = (uint16_t)((s_pos + 1u) % MODEL_WINDOW);
    if (s_count < MODEL_WINDOW) { s_count++; }
}

/* Causal int8 conv at output position p: out[o] = requant(relu(bias + Σ w·in)).
 * Tap k reads input at p-(K-1-k)*dil (the PyTorch causal-left-pad convention).
 * `in` is row-major [position][in_stride]. */
static void conv_at(const int8_t *w, const int32_t *b, int cin, int dil,
                    int32_t mult, int shift, const int8_t *in, int in_stride,
                    int p, int8_t out[MODEL_CHANNELS])
{
    for (int o = 0; o < MODEL_CHANNELS; o++)
    {
        int32_t acc = b[o];
        for (int i = 0; i < cin; i++)
        {
            const int8_t *wo = &w[(o * cin + i) * MODEL_KERNEL];
            for (int k = 0; k < MODEL_KERNEL; k++)
            {
                int pos = p - (MODEL_KERNEL - 1 - k) * dil;
                if (pos < 0) { continue; }
                acc += (int32_t)wo[k] * (int32_t)in[pos * in_stride + i];
            }
        }
        if (acc < 0) { acc = 0; }   /* ReLU */
        int64_t q = ((int64_t)acc * mult + ((int64_t)1 << (shift - 1))) >> shift;
        if (q > QMAX) { q = QMAX; }
        out[o] = (int8_t)q;
    }
}

uint8_t model_infer_run(void)
{
    const model_def_t *m = s_model;
    /* Snapshot the ring cursor: when inference runs in the main loop the TC0 ISR
     * may push() concurrently. Using a fixed `pos` keeps the window mapping
     * stable for this inference; at worst the oldest slot is overwritten mid-run
     * (60 samples back — negligible). */
    uint16_t pos = s_pos;
    if (m == NULL || s_count < MODEL_WINDOW) { return 0u; }

    /* Standardise the whole window into int8 (folded mean/std + input scale). */
    for (int wpos = 0; wpos < MODEL_WINDOW; wpos++)
    {
        int ring_idx = (pos + wpos) % MODEL_WINDOW;
        for (int c = 0; c < MODEL_N_IN; c++)
        {
            int64_t v = (int64_t)s_ring[ring_idx][c] * m->m_in[c] - m->b_in[c];
            int64_t q = (v + ((int64_t)1 << (m->s_in_shift - 1))) >> m->s_in_shift;
            s_qin[wpos][c] = clamp_i8(q);
        }
    }

    /* Conv layers, only at the needed positions. */
    for (int l = 0; l < MODEL_N_LAYERS; l++)
    {
        const int8_t *in = (l == 0) ? &s_qin[0][0] : &s_act[l - 1][0][0];
        int in_stride = (l == 0) ? MODEL_N_IN : MODEL_CHANNELS;
        for (int p = 0; p < MODEL_WINDOW; p++)
        {
            if (!s_need[l][p]) { continue; }
            conv_at(m->w[l], m->b[l], m->cin[l], m->dilation[l],
                    m->req_mult[l], m->req_shift[l], in, in_stride,
                    p, s_act[l][p]);
        }
    }

    /* Head on the last timestep: bit = (logit >= threshold), no sigmoid. */
    const int8_t *last = &s_act[MODEL_N_LAYERS - 1][MODEL_WINDOW - 1][0];
    uint8_t mask = 0u;
    int raw_strum = 0;
    for (int o = 0; o < MODEL_N_OUT; o++)
    {
        int32_t acc = m->head_b[o];
        for (int i = 0; i < MODEL_CHANNELS; i++)
        {
            acc += (int32_t)m->head_w[o * MODEL_CHANNELS + i] * (int32_t)last[i];
        }
        int bit = (acc >= m->head_tq[o]) ? 1 : 0;
        if (o < 5) { if (bit) { mask |= (uint8_t)(1u << o); } }
        else       { raw_strum = bit; }
    }

    /* Strum monostable: one pulse per rising edge, model->strum_hold ticks. */
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
