#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "definitions.h"
#include "fret_scan.h"
#include "cmd_receive.h"
#include "data_stream.h"


void Callback_TC0 (TC_TIMER_STATUS status, uintptr_t context)
{
    fret_scan_all();
    data_stream_send();
    cmd_receive_update();
}

int main(void)
{
    SYS_Initialize(NULL);

    fret_scan_init();
    cmd_receive_init();
    data_stream_init();

    TC0_TimerCallbackRegister( Callback_TC0, NULL );
    TC0_TimerStart();

    while (true) {
    }

    return EXIT_FAILURE;
}
