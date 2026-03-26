#include "data_stream.h"
#include "fret_detect.h"
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
    uint8_t  green_pressed;
    uint8_t  red_pressed;
    uint8_t  yellow_pressed;
    uint8_t  blue_pressed;
    uint8_t  orange_pressed;
    uint8_t  end;
} ds_frame_t;

_Static_assert(sizeof(ds_frame_t) == 17, "frame must be 17 bytes");

void data_stream_init(void)
{
}

bool data_stream_send(void)
{
    if (SERCOM1_USART_WriteFreeBufferCountGet() < sizeof(ds_frame_t))
        return false;

    ds_frame_t frame = {
        .start          = DS_START_BYTE,
        .green          = fret_scan_result(FRET_GREEN),
        .red            = fret_scan_result(FRET_RED),
        .yellow         = fret_scan_result(FRET_YELLOW),
        .blue           = fret_scan_result(FRET_BLUE),
        .orange         = fret_scan_result(FRET_ORANGE),
        .green_pressed  = fret_is_pressed(FRET_GREEN),
        .red_pressed    = fret_is_pressed(FRET_RED),
        .yellow_pressed = fret_is_pressed(FRET_YELLOW),
        .blue_pressed   = fret_is_pressed(FRET_BLUE),
        .orange_pressed = fret_is_pressed(FRET_ORANGE),
        .end            = DS_END_BYTE,
    };

    SERCOM1_USART_Write((uint8_t *)&frame, sizeof(frame));
    return true;
}
