#include "fret_scan.h"
#include "definitions.h"

static const ADC_POSINPUT channel_ain[FRET_COUNT] = {
    ADC_POSINPUT_AIN28,   /* FRET_GREEN  — PA28 */
    ADC_POSINPUT_AIN16,   /* FRET_RED    — PA16 */
    ADC_POSINPUT_AIN19,   /* FRET_YELLOW — PA19 */
    ADC_POSINPUT_AIN27,   /* FRET_BLUE   — PA27 */
    ADC_POSINPUT_AIN26,   /* FRET_ORANGE — PA26 */
};

static uint16_t results[FRET_COUNT];

void fret_scan_init(void)
{
    ADC0_Enable();
    for (uint8_t i = 0; i < FRET_COUNT; i++)
        results[i] = 0;
}

void fret_scan_all(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        ADC0_ChannelSelect(channel_ain[i], ADC_NEGINPUT_GND);
        ADC0_ConversionStart();
        while (!ADC0_ResultReadyStatusGet())
            ;
        results[i] = (uint16_t)ADC0_ConversionResultGet();
    }
}

uint16_t fret_scan_result(fret_channel_t ch)
{
    return results[ch];
}
