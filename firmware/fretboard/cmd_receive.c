#include "cmd_receive.h"
#include "definitions.h"

static uint8_t current_mask;

static void apply_output(uint8_t mask, uint8_t bit,
                         void (*assert_fn)(void),
                         void (*release_fn)(void))
{
    if (mask & bit)
        assert_fn();
    else
        release_fn();
}

static void green_assert(void)  { BUTTON_GREEN_Clear();  BUTTON_GREEN_OutputEnable(); }
static void green_release(void) { BUTTON_GREEN_InputEnable(); }

static void red_assert(void)    { BUTTON_RED_Clear();    BUTTON_RED_OutputEnable(); }
static void red_release(void)   { BUTTON_RED_InputEnable(); }

static void yellow_assert(void)  { BUTTON_YELLOW_Clear();  BUTTON_YELLOW_OutputEnable(); }
static void yellow_release(void) { BUTTON_YELLOW_InputEnable(); }

static void blue_assert(void)    { BUTTON_BLUE_Clear();    BUTTON_BLUE_OutputEnable(); }
static void blue_release(void)   { BUTTON_BLUE_InputEnable(); }

static void orange_assert(void)  { BUTTON_ORANGE_Clear();  BUTTON_ORANGE_OutputEnable(); }
static void orange_release(void) { BUTTON_ORANGE_InputEnable(); }

static void strum_down_assert(void)  { BUTTON_STRUM_DOWN_Clear();  BUTTON_STRUM_DOWN_OutputEnable(); }
static void strum_down_release(void) { BUTTON_STRUM_DOWN_InputEnable(); }

static void strum_up_assert(void)    { BUTTON_STRUM_UP_Clear();    BUTTON_STRUM_UP_OutputEnable(); }
static void strum_up_release(void)   { BUTTON_STRUM_UP_InputEnable(); }

static void apply_mask(uint8_t mask)
{
    apply_output(mask, CMD_BIT_GREEN,      green_assert,      green_release);
    apply_output(mask, CMD_BIT_RED,        red_assert,        red_release);
    apply_output(mask, CMD_BIT_YELLOW,     yellow_assert,     yellow_release);
    apply_output(mask, CMD_BIT_BLUE,       blue_assert,       blue_release);
    apply_output(mask, CMD_BIT_ORANGE,     orange_assert,     orange_release);
    apply_output(mask, CMD_BIT_STRUM_DOWN, strum_down_assert, strum_down_release);
    apply_output(mask, CMD_BIT_STRUM_UP,   strum_up_assert,   strum_up_release);
    current_mask = mask;
}

void cmd_receive_init(void)
{
    apply_mask(0);
}

void cmd_receive_update(void)
{
    size_t avail = SERCOM1_USART_ReadCountGet();
    if (avail == 0)
        return;

    uint8_t buf[64];
    size_t n = (avail > sizeof(buf)) ? sizeof(buf) : avail;
    SERCOM1_USART_Read(buf, n);

    apply_mask(buf[n - 1]);
}
