#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "wiimote.h"
#include "wiimote_ext.h"
#include "ext_crypto.h"

static const char *TAG = "fauxmote.wm";

#define EEPROM_SIZE       0x1700u   /* readable EEPROM range is 0x0000..0x16FF */
#define EEPROM_READ_MAX   0x16FFu
#define SENDER_PERIOD_MS  15
#define SENDER_STACK      3072
#define ACCEL_NEUTRAL     0x85      /* zero-g raw value (matches the EEPROM calibration) */

/* IR pointer calibration in camera coords (1024x768), from on-hardware edge
 * measurement. Pointer (0,0) = top-left, (1,1) = bottom-right maps to screen edges. */
#define IR_X_CENTER  512
#define IR_X_HALF    296
#define IR_Y_CENTER  487
#define IR_Y_HALF    165

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
static float   s_point_x, s_point_y; /* IR pointer position, 0..1 (0,0 = top-left) */
static bool    s_point_active;       /* false = no IR dots reported */
static uint8_t s_eeprom[EEPROM_SIZE];

static const wiimote_extension_t *s_ext;   /* registered extension, or NULL */
static bool    s_ext_connected;            /* report the extension as attached */

static ext_crypto_t s_crypt;               /* extension cipher tables (when on) */
static bool    s_crypt_on;                 /* host enabled extension encryption */
static bool    s_crypt_armed;              /* host wrote 0xAA→0xf0; awaiting key */

#define EXT_DATA_OFFSET  0x08              /* register addr the streamed ext bytes map to */

static StaticTask_t s_sender_tcb;
static StackType_t  s_sender_stack[SENDER_STACK];

/* Accelerometer calibration the Wii reads from EEPROM 0x16 (mirrored at 0x20).
 * Canonical zero-G 0x85 / 1G 0xA0 per axis + checksum (from a real RVL-CNT-01). */
static const uint8_t k_accel_cal[10] = {
    0x85, 0x85, 0x85, 0x00, 0xA0, 0xA0, 0xA0, 0x00, 0x40, 0x04,
};

/* Core button names → (byte index, bit mask) in the 2-byte core button field. */
static const struct { const char *name; uint8_t byte; uint8_t mask; } k_buttons[] = {
    { "left",  0, 0x01 }, { "right", 0, 0x02 }, { "down", 0, 0x04 }, { "up", 0, 0x08 },
    { "plus",  0, 0x10 },
    { "two",   1, 0x01 }, { "one",   1, 0x02 }, { "b",    1, 0x04 }, { "a",  1, 0x08 },
    { "minus", 1, 0x10 }, { "home",  1, 0x80 },
};

/* Momentary taps: Wiimote_TapButton presses now and schedules a release (by name,
 * so it works for core and extension buttons alike) that the sender task applies. */
#define TAP_SLOTS  4
#define TAP_MS     120
static struct { char name[16]; TickType_t release_at; bool active; } s_taps[TAP_SLOTS];

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
    p[2] = (uint8_t)((s_leds << 4) | ((s_ext && s_ext_connected) ? 0x02 : 0x00));  /* LEDs 4..7, ext bit 1 */
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

/* Register reads/writes. Only the extension space (0xa4xxxx) is backed (by the
 * registered extension's bank); other spaces (IR 0xb0, speaker 0xa2) read as zeros
 * and writes are accepted but ignored. */
static void read_register(int fd, uint32_t offset, uint16_t size)
{
    bool ext = (s_ext && ((offset >> 16) & 0xFE) == 0xA4);
    uint32_t addr = offset;
    uint16_t left = size;
    while (left > 0) {
        uint8_t idx = (uint8_t)(addr & 0xFF);
        uint8_t chunk = left > 16 ? 16 : (uint8_t)left;
        if ((int)idx + chunk > 256) chunk = (uint8_t)(256 - idx);
        if (ext) {
            uint8_t buf[16];
            memcpy(buf, &s_ext->regs[idx], chunk);
            if (s_crypt_on) {
                ExtCrypto_Encrypt(&s_crypt, buf, idx, chunk);  /* data is read encrypted */
            }
            send_read_data(fd, chunk - 1, 0x00, (uint16_t)addr, buf);
        } else {
            send_read_data(fd, chunk - 1, 0x00, (uint16_t)addr, NULL);
        }
        addr += chunk;
        left -= chunk;
        if (idx + chunk >= 256) break;
    }
}

static void write_register(uint32_t offset, uint8_t size, const uint8_t *data)
{
    if (!s_ext || ((offset >> 16) & 0xFE) != 0xA4) {
        return;   /* only the extension register space is backed */
    }
    uint8_t idx = (uint8_t)(offset & 0xFF);
    for (int i = 0; i < size && (idx + i) < 256; i++) {
        s_ext->regs[idx + i] = data[i];
    }

    /* Extension encryption handshake. The host writes 0x55→0xf0 to disable, or
     * 0xAA→0xf0 then a 16-byte key to 0x40-0x4f to enable. */
    if (idx == 0xf0 && size >= 1) {
        if (data[0] == 0x55) {
            s_crypt_on = s_crypt_armed = false;
        } else if (data[0] == 0xAA) {
            s_crypt_armed = true;
        }
    }
    if (s_crypt_armed && idx >= 0x40 && idx <= 0x4F) {
        ExtCrypto_GenTables(&s_crypt, &s_ext->regs[0x40]);  /* last key chunk completes it */
        s_crypt_on = true;
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
        if (plen >= 5) {
            uint32_t offset = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            uint8_t wsize = p[4];
            if (p[0] & 0x04) write_register(offset, wsize, &p[5]);
        }
        send_ack(fd, report, 0x00);
        break;
    case 0x17:                      /* read memory/registers */
        if (plen >= 6) {
            uint32_t offset = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            uint16_t size = (uint16_t)((p[4] << 8) | p[5]);
            if (p[0] & 0x04) {
                read_register(fd, offset, size);
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

static void set_accel_level(uint8_t *a)
{
    a[0] = ACCEL_NEUTRAL;   /* X = 0 g */
    a[1] = ACCEL_NEUTRAL;   /* Y = 0 g */
    a[2] = 0xA0;            /* Z = +1 g (held level, no roll) */
}

/* One extended-IR object: X/Y are 10-bit camera coords (1024x768), or x<0 = "not
 * visible". 3rd byte packs Y[9:8], X[9:8], size. */
static void put_ir_object(uint8_t *o, int x, int y, int size)
{
    if (x < 0) {
        o[0] = o[1] = o[2] = 0xFF;
        return;
    }
    o[0] = (uint8_t)(x & 0xFF);
    o[1] = (uint8_t)(y & 0xFF);
    o[2] = (uint8_t)((((y >> 8) & 0x03) << 6) | (((x >> 8) & 0x03) << 4) | (size & 0x0F));
}

/* Synthesize the two sensor-bar dots from the pointer state (camera is 1024x768,
 * mirrored vs the screen). Returns false (dots left untouched) when the pointer is
 * inactive. If an axis comes out inverted on hardware, flip the (… - …) terms. */
static bool ir_dots(int *x0, int *y0, int *x1, int *y1)
{
    if (!s_point_active) {
        return false;
    }
    int midx = IR_X_CENTER + (int)((0.5f - s_point_x) * (2 * IR_X_HALF));
    int midy = IR_Y_CENTER + (int)((s_point_y - 0.5f) * (2 * IR_Y_HALF));
    const int sep = 128;                     /* half sensor-bar separation, camera px */
    if (midy < 0) midy = 0;
    if (midy > 767) midy = 767;
    *x0 = (midx - sep < 0) ? 0 : midx - sep;
    *x1 = (midx + sep > 1023) ? 1023 : midx + sep;
    *y0 = *y1 = midy;
    return true;
}

/* Extended IR (12 bytes = 4 objects): two pointer dots, two not visible. */
static void build_ir_extended(uint8_t *dst)
{
    int x0 = -1, y0 = 0, x1 = -1, y1 = 0;
    ir_dots(&x0, &y0, &x1, &y1);
    put_ir_object(&dst[0], x0, y0, 4);
    put_ir_object(&dst[3], x1, y1, 4);
    put_ir_object(&dst[6], -1, 0, 0);
    put_ir_object(&dst[9], -1, 0, 0);
}

/* Basic IR packs two objects into 5 bytes (X/Y high bits share byte 2). x<0 = not
 * visible (max coord). */
static void put_ir_basic_pair(uint8_t *o, int xa, int ya, int xb, int yb)
{
    if (xa < 0) { xa = ya = 0x3FF; }
    if (xb < 0) { xb = yb = 0x3FF; }
    o[0] = (uint8_t)(xa & 0xFF);
    o[1] = (uint8_t)(ya & 0xFF);
    o[2] = (uint8_t)((((ya >> 8) & 3) << 6) | (((xa >> 8) & 3) << 4)
                   | (((yb >> 8) & 3) << 2) | ((xb >> 8) & 3));
    o[3] = (uint8_t)(xb & 0xFF);
    o[4] = (uint8_t)(yb & 0xFF);
}

/* Basic IR (10 bytes = 4 objects, packed in pairs): same two dots as extended. */
static void build_ir_basic(uint8_t *dst)
{
    int x0 = -1, y0 = 0, x1 = -1, y1 = 0;
    ir_dots(&x0, &y0, &x1, &y1);
    put_ir_basic_pair(&dst[0], x0, y0, x1, y1);
    put_ir_basic_pair(&dst[5], -1, 0, -1, 0);
}

/* Fill the registered extension's bytes at dst, if an extension is attached. */
static void build_extension(uint8_t *dst)
{
    if (s_ext && s_ext_connected && s_ext->build_report) {
        s_ext->build_report(dst);
        if (s_crypt_on) {
            ExtCrypto_Encrypt(&s_crypt, dst, EXT_DATA_OFFSET, s_ext->report_len);
        }
    }
}

/* Build the input report the Wii's current mode expects: core buttons in the first
 * two bytes (except 0x3d), level accelerometer where present, extended IR from the
 * pointer state, and the extension's bytes in the extension field. Returns the
 * payload length, or 0 for an unknown mode. */
static int build_report(uint8_t mode, uint8_t *p)
{
    memset(p, 0, 21);
    if (mode != 0x3d) {
        p[0] = s_btn0;
        p[1] = s_btn1;
    }
    switch (mode) {
    case 0x30: return 2;
    case 0x31: set_accel_level(&p[2]); return 5;
    case 0x32: build_extension(&p[2]); return 10;
    case 0x33: set_accel_level(&p[2]); build_ir_extended(&p[5]); return 17;
    case 0x34: build_extension(&p[2]); return 21;
    case 0x35: set_accel_level(&p[2]); build_extension(&p[5]); return 21;
    case 0x36: build_ir_basic(&p[2]); build_extension(&p[12]); return 21;   /* btn + 10 IR + 9 ext */
    case 0x37: set_accel_level(&p[2]); build_ir_basic(&p[5]); build_extension(&p[15]); return 21;  /* + 10 IR + 6 ext */
    case 0x3d: build_extension(&p[0]); return 21;
    case 0x3e: case 0x3f: return 21;
    default:   return 0;
    }
}

void Wiimote_RegisterExtension(const wiimote_extension_t *ext)
{
    s_ext = ext;
    s_ext_connected = (ext != NULL);
}

bool Wiimote_SetButton(const char *name, bool pressed)
{
    for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
        if (strcmp(name, k_buttons[i].name) == 0) {
            uint8_t *b = (k_buttons[i].byte == 0) ? &s_btn0 : &s_btn1;
            if (pressed) *b |= k_buttons[i].mask; else *b &= (uint8_t)~k_buttons[i].mask;
            return true;
        }
    }
    if (s_ext && s_ext->set_button) {
        return s_ext->set_button(name, pressed);
    }
    return false;
}

bool Wiimote_TapButton(const char *name)
{
    if (!Wiimote_SetButton(name, true)) {
        return false;
    }
    int slot = -1;
    for (int i = 0; i < TAP_SLOTS; i++) {
        if (!s_taps[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;     /* all busy: reuse the first */
    strncpy(s_taps[slot].name, name, sizeof(s_taps[slot].name) - 1);
    s_taps[slot].name[sizeof(s_taps[slot].name) - 1] = '\0';
    s_taps[slot].release_at = xTaskGetTickCount() + pdMS_TO_TICKS(TAP_MS);
    s_taps[slot].active = true;
    return true;
}

void Wiimote_SetExtension(bool connected)
{
    s_ext_connected = connected;
}

void Wiimote_NotifyDisconnected(void)
{
    s_data_fd = -1;          /* mark not-connected at once (don't wait for the reader to exit) */
    s_streaming = false;
    s_leds = 0;
    s_btn0 = s_btn1 = 0;
    s_crypt_on = s_crypt_armed = false;   /* host re-inits encryption on reconnect */
    for (int i = 0; i < TAP_SLOTS; i++) {
        s_taps[i].active = false;
    }
    if (s_ext && s_ext->reset) {
        s_ext->reset();
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

void Wiimote_SetPointer(float x, float y)
{
    if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
    if (y < 0.0f) y = 0.0f; else if (y > 1.0f) y = 1.0f;
    s_point_x = x;
    s_point_y = y;
    s_point_active = true;
}

void Wiimote_ClearPointer(void)
{
    s_point_active = false;
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
                Wiimote_SetButton(s_taps[i].name, false);
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
