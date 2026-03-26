#include "fret_scan.h"
#include "definitions.h"

/* AIN mapping — order matches fret_channel_t */
static const ADC_POSINPUT channel_ain[FRET_COUNT] = {
    ADC_POSINPUT_AIN28,   /* FRET_GREEN  — PA28 */
    ADC_POSINPUT_AIN16,   /* FRET_RED    — PA16 */
    ADC_POSINPUT_AIN19,   /* FRET_YELLOW — PA19 */
    ADC_POSINPUT_AIN27,   /* FRET_BLUE   — PA27 */
    ADC_POSINPUT_AIN26,   /* FRET_ORANGE — PA26 */
};

typedef enum {
    SCAN_SELECT,
    SCAN_WAIT,
} scan_state_t;

static scan_state_t  state;
static uint8_t       ch_idx;
static uint16_t      results[FRET_COUNT];
static uint32_t      sweeps;

void fret_scan_init(void)
{
    ADC0_Enable();
    state  = SCAN_SELECT;
    ch_idx = 0;
    sweeps = 0;
    for (uint8_t i = 0; i < FRET_COUNT; i++)
        results[i] = 0;
}

bool fret_scan_task(void)
{
    bool sweep_done = false;

    switch (state) {
    case SCAN_SELECT:
        ADC0_ChannelSelect(channel_ain[ch_idx], ADC_NEGINPUT_GND);
        ADC0_ConversionStart();
        state = SCAN_WAIT;
        break;

    case SCAN_WAIT:
        if (ADC0_ResultReadyStatusGet()) {
            results[ch_idx] = (uint16_t)ADC0_ConversionResultGet();

            ch_idx++;
            if (ch_idx >= FRET_COUNT) {
                ch_idx = 0;
                sweeps++;
                sweep_done = true;
            }
            state = SCAN_SELECT;
        }
        break;
    }

    return sweep_done;
}

uint16_t fret_scan_result(fret_channel_t ch)
{
    return results[ch];
}

uint32_t fret_scan_sweep_count(void)
{
    return sweeps;
}
