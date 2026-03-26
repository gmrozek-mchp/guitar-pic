#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "definitions.h"
#include "fret_scan.h"
#include "fret_detect.h"
#include "data_stream.h"

int main(void)
{
    SYS_Initialize(NULL);
    fret_scan_init();
    fret_detect_init();
    data_stream_init();

    while (true) {
        if (fret_scan_task()) {
            fret_detect_update();
            data_stream_send();
        }
    }

    return EXIT_FAILURE;
}
