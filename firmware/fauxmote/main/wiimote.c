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
#define ACCEL_NEUTRAL     0x85      /* zero-g raw value (matches the EEPROM calibration) */

/* HID transaction prefixes (HIDP): host->device output, device->host input. */
#define HIDP_OUTPUT  0xA2
#define HIDP_INPUT   0xA1

static int     s_data_fd = -1;       /* the HID data channel (carries 0xa2 output reports) */
static uint8_t s_leds;               /* player LEDs (nibble), set by the Wii via report 0x11 */
static uint8_t s_rumble;
static bool    s_streaming;          /* true once the Wii has set a reporting mode (0x12) */
static uint8_t s_report_mode = 0x30; /* report ID the Wii told us to send (default core buttons) */
static bool    s_reporting_continuous;
static uint8_t s_btn0, s_btn1;       /* core button state (0 = nothing pressed) */
static uint8_t s_eeprom[EEPROM_SIZE];

static StaticTask_t s_sender_tcb;
static StackType_t  s_sender_stack[SENDER_STACK];

/* Accelerometer calibration the Wii reads from EEPROM 0x16 (mirrored at 0x20).
 * Canonical zero-G 0x85 / 1G 0xA0 per axis + checksum (from a real RVL-CNT-01). */
static const uint8_t k_accel_cal[10] = {
    0x85, 0x85, 0x85, 0x00, 0xA0, 0xA0, 0xA0, 0x00, 0x40, 0x04,
};

typedef struct { const char *name; uint8_t byte; uint8_t mask; } wm_button_t;

/* Core button names → (byte index, bit mask) in the 2-byte button field. */
static const wm_button_t k_buttons[] = {
    { "left",  0, 0x01 }, { "right", 0, 0x02 }, { "down", 0, 0x04 }, { "up", 0, 0x08 },
    { "plus",  0, 0x10 },
    { "two",   1, 0x01 }, { "one",   1, 0x02 }, { "b",    1, 0x04 }, { "a",  1, 0x08 },
    { "minus", 1, 0x10 }, { "home",  1, 0x80 },
};

/* Momentary taps: Wiimote_TapButton presses now and schedules a release that the
 * sender task applies, so callers (CLI, Marvin) don't block. */
#define TAP_SLOTS  4
#define TAP_MS     120
static struct { uint8_t byte; uint8_t mask; TickType_t release_at; bool active; } s_taps[TAP_SLOTS];

static const wm_button_t *find_button(const char *name)
{
    for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
        if (strcmp(name, k_buttons[i].name) == 0) return &k_buttons[i];
    }
    return NULL;
}

static void apply_button(const wm_button_t *b, bool pressed)
{
    uint8_t *p = (b->byte == 0) ? &s_btn0 : &s_btn1;
    if (pressed) *p |= b->mask; else *p &= ~b->mask;
}

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
        if (plen >= 2) {
            s_reporting_continuous = (p[0] & 0x04) != 0;
            s_report_mode = p[1];
        }
        s_streaming = true;
        ESP_LOGI(TAG, "reporting mode 0x%02x (continuous=%d)", s_report_mode, s_reporting_continuous);
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

/* Build the input report the Wii's current mode expects: buttons in the first two
 * bytes (except 0x3d), neutral accelerometer where present, IR/extension zeroed
 * (filled in later phases). Returns the payload length, or 0 for an unknown mode. */
static int build_report(uint8_t mode, uint8_t *p)
{
    memset(p, 0, 21);
    if (mode != 0x3d) {
        p[0] = s_btn0;
        p[1] = s_btn1;
    }
    switch (mode) {
    case 0x30: return 2;
    case 0x31: p[2] = p[3] = p[4] = ACCEL_NEUTRAL; return 5;
    case 0x32: return 10;
    case 0x33: p[2] = p[3] = p[4] = ACCEL_NEUTRAL; return 17;
    case 0x34: return 21;
    case 0x35: p[2] = p[3] = p[4] = ACCEL_NEUTRAL; return 21;
    case 0x36: return 21;
    case 0x37: p[2] = p[3] = p[4] = ACCEL_NEUTRAL; return 21;
    case 0x3d: case 0x3e: case 0x3f: return 21;
    default:   return 0;
    }
}

bool Wiimote_SetButton(const char *name, bool pressed)
{
    const wm_button_t *b = find_button(name);
    if (!b) return false;
    apply_button(b, pressed);
    return true;
}

bool Wiimote_TapButton(const char *name)
{
    const wm_button_t *b = find_button(name);
    if (!b) return false;
    apply_button(b, true);

    int slot = -1;
    for (int i = 0; i < TAP_SLOTS; i++) {
        if (!s_taps[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;     /* all busy: reuse the first */
    s_taps[slot].byte = b->byte;
    s_taps[slot].mask = b->mask;
    s_taps[slot].release_at = xTaskGetTickCount() + pdMS_TO_TICKS(TAP_MS);
    s_taps[slot].active = true;
    return true;
}

void Wiimote_NotifyDisconnected(void)
{
    s_data_fd = -1;          /* mark not-connected at once (don't wait for the reader to exit) */
    s_streaming = false;
    s_leds = 0;
    s_btn0 = s_btn1 = 0;
    for (int i = 0; i < TAP_SLOTS; i++) {
        s_taps[i].active = false;
    }
}

bool Wiimote_IsConnected(void)  { return s_data_fd >= 0; }
bool Wiimote_IsAssigned(void)   { return s_leds != 0; }
uint8_t Wiimote_ReportMode(void) { return s_report_mode; }

int Wiimote_PlayerSlot(void)
{
    for (int i = 0; i < 4; i++) {
        if (s_leds & (1 << i)) return i + 1;
    }
    return 0;
}

static void sender_task(void *arg)
{
    (void)arg;
    uint8_t p[21];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SENDER_PERIOD_MS));

        /* Release any taps whose deadline has passed (tick-wrap safe). */
        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < TAP_SLOTS; i++) {
            if (s_taps[i].active && (int32_t)(now - s_taps[i].release_at) >= 0) {
                uint8_t *b = (s_taps[i].byte == 0) ? &s_btn0 : &s_btn1;
                *b &= ~s_taps[i].mask;
                s_taps[i].active = false;
            }
        }

        if (s_streaming && s_data_fd >= 0) {
            uint8_t mode = s_report_mode;
            int len = build_report(mode, p);
            if (len == 0) {                 /* unknown mode → fall back to core buttons */
                mode = 0x30;
                len = build_report(mode, p);
            }
            wm_send(s_data_fd, mode, p, len);
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
