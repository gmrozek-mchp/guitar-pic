/* Host harness for the streaming inference (test_stream_bitexact.py).
 * Reads whitespace-separated rows of 5 raw ADC values from stdin, steps the
 * streaming model once per row, and prints the command bitmask per line. */
#include <stdio.h>
#include "model_infer_stream.h"

int main(void)
{
    model_infer_stream_init();
    uint16_t adc[5];
    while (scanf("%hu %hu %hu %hu %hu",
                 &adc[0], &adc[1], &adc[2], &adc[3], &adc[4]) == 5)
    {
        printf("%u\n", (unsigned)model_infer_stream_step(adc));
    }
    return 0;
}
