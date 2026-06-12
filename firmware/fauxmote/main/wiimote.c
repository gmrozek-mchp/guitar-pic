#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "wiimote.h"

static const char *TAG = "fauxmote.wm";

#define EEPROM_SIZE       0x1700u   /* readable EEPROM range is 0x0000..0x16FF */
#define EEPROM_READ_MAX   0x16FFu
#define SENDER_PERIOD_MS  15
#define SENDER_STACK      3072

/* HID transaction prefixes (HIDP): host->device output, device->host input. */
#define HIDP_OUTPUT  0xA2
#define HIDP_INPUT   0xA1

static int     s_data_fd = -1;       /* the HID data channel (carries 0xa2 output reports) */
static uint8_t s_leds;               /* player LEDs (nibble), set by the Wii via report 0x11 */
static uint8_t s_rumble;
static bool    s_streaming;          /* true once the Wii has set a reporting mode (0x12) */
static uint8_t s_btn0, s_btn1;       /* core button state (0 = nothing pressed) */
static uint8_t s_eeprom[EEPROM_SIZE];

static StaticTask_t s_sender_tcb;
static StackType_t  s_sender_stack[SENDER_STACK];

/* Accelerometer calibration the Wii reads from EEPROM 0x16 (mirrored at 0x20).
 * Canonical zero-G 0x85 / 1G 0xA0 per axis + checksum (from a real RVL-CNT-01). */
static const uint8_t k_accel_cal[10] = {
    0x85, 0x85, 0x85, 0x00, 0xA0, 0xA0, 0xA0, 0x00, 0x40, 0x04,
};

static void wm_send(int fd, uint8_t report_id, const uint8_t *payload, int paylen)
{
    uint8_t frame[2 + 21];
    frame[0] = HIDP_INPUT;
    frame[1] = report_id;
    if (paylen > 0) {
        memcpy(frame + 2, payload, paylen);
    }
    write(fd, frame, 2 + paylen);
}

static void send_status(int fd)
{
    uint8_t p[6] = {0};
    p[0] = s_btn0;
    p[1] = s_btn1;
    p[2] = (uint8_t)(s_leds << 4);   /* flags: LEDs in 4..7; no extension/speaker/IR */
    p[5] = 0xC0;                     /* battery level (near full) */
    wm_send(fd, 0x20, p, sizeof(p));
}

static void send_ack(int fd, uint8_t report_id, uint8_t result)
{
    uint8_t p[4] = { s_btn0, s_btn1, report_id, result };
    wm_send(fd, 0x22, p, sizeof(p));
}

/* 0x21 read-data: size_minus1 in 0..15, error in low nibble, 16-bit addr, data. */
static void send_read_data(int fd, uint8_t size_minus1, uint8_t error, uint16_t addr,
                           const uint8_t *data)
{
    uint8_t p[21] = {0};
    p[0] = s_btn0;
    p[1] = s_btn1;
    p[2] = (uint8_t)((size_minus1 << 4) | (error & 0x0F));
    p[3] = (uint8_t)(addr >> 8);
    p[4] = (uint8_t)(addr & 0xFF);
    if (data) {
        memcpy(p + 5, data, size_minus1 + 1);
    }
    wm_send(fd, 0x21, p, sizeof(p));
}

static void read_eeprom(int fd, uint32_t offset, uint16_t size)
{
    if (size == 0 || offset + size > EEPROM_READ_MAX) {
        send_read_data(fd, 0x0F, 0x08, (uint16_t)offset, NULL);   /* error: invalid address */
        return;
    }
    uint32_t addr = offset;
    uint16_t left = size;
    while (left > 0) {
        uint8_t chunk = left > 16 ? 16 : (uint8_t)left;
        send_read_data(fd, chunk - 1, 0x00, (uint16_t)addr, &s_eeprom[addr]);
        addr += chunk;
        left -= chunk;
    }
}

void Wiimote_HandleRx(int fd, const uint8_t *data, int len)
{
    if (len < 2 || data[0] != HIDP_OUTPUT) {
        return;   /* not a HID output report (e.g. control-channel HIDP transaction) */
    }
    s_data_fd = fd;                 /* this fd is the data channel — reply / stream here */
    uint8_t report = data[1];
    const uint8_t *p = data + 2;
    int plen = len - 2;
    if (plen >= 1) {
        s_rumble = p[0] & 0x01;     /* rumble bit rides the first payload byte of every report */
    }

    switch (report) {
    case 0x11:                      /* player LEDs */
        if (plen >= 1) s_leds = (p[0] >> 4) & 0x0F;
        send_ack(fd, report, 0x00);
        break;
    case 0x12:                      /* data reporting mode */
        s_streaming = true;
        send_ack(fd, report, 0x00);
        break;
    case 0x10:                      /* rumble only */
    case 0x13: case 0x1A:           /* IR camera enable */
    case 0x14: case 0x19:           /* speaker enable / mute */
        send_ack(fd, report, 0x00);
        break;
    case 0x15:                      /* status request */
        send_status(fd);
        break;
    case 0x16:                      /* write memory/registers */
        send_ack(fd, report, 0x00);
        break;
    case 0x17:                      /* read memory/registers */
        if (plen >= 6) {
            uint32_t offset = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            uint16_t size = (uint16_t)((p[4] << 8) | p[5]);
            if (p[0] & 0x04) {      /* register space — no extension yet, report error */
                send_read_data(fd, 0x0F, 0x08, (uint16_t)offset, NULL);
            } else {
                read_eeprom(fd, offset, size);
            }
        }
        break;
    default:
        send_ack(fd, report, 0x00);
        break;
    }
}

void Wiimote_NotifyFdClosed(int fd)
{
    if (fd == s_data_fd) {
        s_data_fd = -1;
        s_streaming = false;
    }
}

bool Wiimote_IsConnected(void)
{
    return s_data_fd >= 0;
}

bool Wiimote_IsAssigned(void)
{
    return s_leds != 0;
}

static void sender_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SENDER_PERIOD_MS));
        if (s_streaming && s_data_fd >= 0) {
            uint8_t p[2] = { s_btn0, s_btn1 };
            wm_send(s_data_fd, 0x30, p, sizeof(p));   /* core-buttons input report */
        }
    }
}

void Wiimote_Start(void)
{
    memcpy(&s_eeprom[0x16], k_accel_cal, sizeof(k_accel_cal));
    memcpy(&s_eeprom[0x20], k_accel_cal, sizeof(k_accel_cal));
    xTaskCreateStatic(sender_task, "wm_tx", SENDER_STACK, NULL, 6,
                      s_sender_stack, &s_sender_tcb);
    ESP_LOGI(TAG, "Wiimote report state machine started");
}
