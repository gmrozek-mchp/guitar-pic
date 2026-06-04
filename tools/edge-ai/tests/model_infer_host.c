/* Host harness for the on-device inference, used by test_bitexact.py.
 *
 * Reads whitespace-separated rows of 5 raw ADC values from stdin, pushes each
 * through model_infer, and prints the resulting command bitmask (one per line).
 * Compile against a generated model_weights.h plus firmware/fretboard. */
#include <stdio.h>
#include "model_infer.h"

int main(void)
{
    model_infer_init();
    uint16_t adc[5];
    while (scanf("%hu %hu %hu %hu %hu",
                 &adc[0], &adc[1], &adc[2], &adc[3], &adc[4]) == 5)
    {
        model_infer_push(adc);
        printf("%u\n", (unsigned)model_infer_run());
    }
    return 0;
}
