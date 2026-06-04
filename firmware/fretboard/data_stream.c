#include "data_stream.h"
#include "fret_detect.h"
#include "cmd_receive.h"
#include "definitions.h"

#define DS_START_BYTE  0x03
#define DS_END_BYTE    ((uint8_t)~DS_START_BYTE)   /* 0xFC */

typedef struct __attribute__((packed)) {
    uint8_t  start;
    uint16_t green;
    uint16_t red;
    uint16_t yellow;
    uint16_t blue;
    uint16_t orange;
    uint32_t sample_seq;    /* monotonic, one per tick — host detects gaps */
    uint8_t  applied_mask;  /* actuator bitmask driven during this scan */
    uint8_t  end;
} ds_frame_t;

_Static_assert(sizeof(ds_frame_t) == 17, "frame must be 17 bytes");

/* MODEL_DRIVEN variant: same fields plus the running inference count, so the
 * host can measure the actual model_infer_run() rate (it runs in the main loop,
 * not the ISR, so it may be slower than the 240 Hz sample rate). Distinct length
 * (21 B) from the 17 B marvin frame; the standalone host parser keys on it. */
typedef struct __attribute__((packed)) {
    uint8_t  start;
    uint16_t green;
    uint16_t red;
    uint16_t yellow;
    uint16_t blue;
    uint16_t orange;
    uint32_t sample_seq;
    uint32_t infer_count;   /* total model inferences so far (main loop) */
    uint8_t  applied_mask;
    uint8_t  end;
} ds_model_frame_t;

_Static_assert(sizeof(ds_model_frame_t) == 21, "model frame must be 21 bytes");

static uint32_t s_sample_seq;

void data_stream_init(void)
{
}

bool data_stream_send(void)
{
    /* Advance per tick (before the buffer check) so a dropped send leaves a
     * gap in the transmitted sequence the host can detect. */
    uint32_t seq = s_sample_seq++;

    if (SERCOM1_USART_WriteFreeBufferCountGet() < sizeof(ds_frame_t))
        return false;

    ds_frame_t frame = {
        .start          = DS_START_BYTE,
        .green          = fret_scan_result(FRET_GREEN),
        .red            = fret_scan_result(FRET_RED),
        .yellow         = fret_scan_result(FRET_YELLOW),
        .blue           = fret_scan_result(FRET_BLUE),
        .orange         = fret_scan_result(FRET_ORANGE),
        .sample_seq     = seq,
        .applied_mask   = cmd_receive_current_mask(),
        .end            = DS_END_BYTE,
    };

    SERCOM1_USART_Write((uint8_t *)&frame, sizeof(frame));
    return true;
}

bool data_stream_send_model(uint32_t infer_count)
{
    uint32_t seq = s_sample_seq++;

    if (SERCOM1_USART_WriteFreeBufferCountGet() < sizeof(ds_model_frame_t))
        return false;

    ds_model_frame_t frame = {
        .start          = DS_START_BYTE,
        .green          = fret_scan_result(FRET_GREEN),
        .red            = fret_scan_result(FRET_RED),
        .yellow         = fret_scan_result(FRET_YELLOW),
        .blue           = fret_scan_result(FRET_BLUE),
        .orange         = fret_scan_result(FRET_ORANGE),
        .sample_seq     = seq,
        .infer_count    = infer_count,
        .applied_mask   = cmd_receive_current_mask(),
        .end            = DS_END_BYTE,
    };

    SERCOM1_USART_Write((uint8_t *)&frame, sizeof(frame));
    return true;
}
