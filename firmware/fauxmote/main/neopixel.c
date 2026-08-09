#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"

#include "neopixel.h"

static const char *TAG = "fauxmote.px";

#define PX_GPIO        GPIO_NUM_0    /* NeoPixel data */
#define PX_PWR_GPIO    GPIO_NUM_2    /* NeoPixel + STEMMA QT power, active high */

/* 10 MHz RMT resolution = 100 ns per tick, so the WS2812 bit cells are whole
 * ticks: a 0 is 0.3 us high + 0.9 us low, a 1 is 0.9 us high + 0.3 us low. The
 * >50 us reset gap comes for free — status updates are milliseconds apart. */
#define PX_RESOLUTION_HZ  10000000u
#define PX_T0H_TICKS      3
#define PX_T0L_TICKS      9
#define PX_T1H_TICKS      9
#define PX_T1L_TICKS      3

#define PX_SLOTS  4      /* in-flight colour buffers; matches trans_queue_depth */

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;
static uint8_t s_grb[PX_SLOTS][3];
static unsigned s_slot;
static bool s_tx_failing;

void Neopixel_Init(void)
{
    gpio_reset_pin(PX_PWR_GPIO);
    gpio_set_direction(PX_PWR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PX_PWR_GPIO, 1);

    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = PX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = PX_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = PX_SLOTS,
    };
    esp_err_t err = rmt_new_tx_channel(&chan_cfg, &s_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt channel: %s", esp_err_to_name(err));
        return;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .level0 = 1, .duration0 = PX_T0H_TICKS, .level1 = 0, .duration1 = PX_T0L_TICKS },
        .bit1 = { .level0 = 1, .duration0 = PX_T1H_TICKS, .level1 = 0, .duration1 = PX_T1L_TICKS },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&enc_cfg, &s_enc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt encoder: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(rmt_enable(s_chan));
    Neopixel_Set(0, 0, 0);
}

void Neopixel_Set(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_chan == NULL || s_enc == NULL) {
        return;
    }
    /* The buffer must stay valid until the transmit completes, and we don't wait for
     * that, so rotate through a few of them rather than rewriting one in place. */
    uint8_t *grb = s_grb[s_slot];
    s_slot = (s_slot + 1u) % PX_SLOTS;
    grb[0] = g; grb[1] = r; grb[2] = b;     /* WS2812 wire order is G, R, B */

    /* Queue and move on. rmt_transmit recycles completed descriptors itself, and
     * nonblocking keeps a busy queue from parking this task; a dropped update just
     * leaves the previous colour up for another tick. */
    rmt_transmit_config_t tx_cfg = { .loop_count = 0, .flags.queue_nonblocking = 1 };
    esp_err_t err = rmt_transmit(s_chan, s_enc, grb, sizeof(s_grb[0]), &tx_cfg);
    if (err != ESP_OK && !s_tx_failing) {
        ESP_LOGW(TAG, "pixel update dropped: %s", esp_err_to_name(err));
    }
    s_tx_failing = (err != ESP_OK);         /* log the edge, not every tick */
}
