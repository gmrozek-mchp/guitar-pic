#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "definitions.h"
#include "fret_scan.h"
#include "fret_detect.h"
#include "fret_button.h"
#include "data_stream.h"

#define PROCESS_INTERVAL_MS  2   /* 500 Hz */

int main(void)
{
    SYS_Initialize(NULL);
    SYSTICK_TimerStart();
    fret_scan_init();
    fret_detect_init();
    fret_button_init();
    data_stream_init();

    uint32_t next_ms = SYSTICK_GetTickCounter();

    while (true) {
        uint32_t now = SYSTICK_GetTickCounter();
        if ((int32_t)(now - next_ms) >= 0) {
            next_ms += PROCESS_INTERVAL_MS;

            fret_scan_all();
            fret_detect_update();
            fret_button_update();
            data_stream_send();
        }
    }

    return EXIT_FAILURE;
}
