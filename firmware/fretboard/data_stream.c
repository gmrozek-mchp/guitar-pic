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
