#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A Wiimote extension controller (guitar, nunchuk, classic, …). The base Wiimote
 * owns the BT/HID link, the report sender, and the register/status plumbing; an
 * extension module registers itself and supplies its identity + data.
 *
 * An extension provides a 256-byte register bank (the host reads the ID at 0xfa and
 * may write init/encryption bytes), a function to fill its bytes in the data report,
 * an optional button-by-name setter (so the CLI/Marvin drive it through the same
 * Wiimote_SetButton API), and an optional reset hook for link drops. */
typedef struct {
    const char *name;
    uint8_t    *regs;        /* 256-byte register bank, owned by the extension (ID at 0xfa) */
    uint8_t     report_len;  /* bytes the extension contributes to the data report */
    void (*build_report)(uint8_t *dst);                  /* fill report_len bytes */
    bool (*set_button)(const char *name, bool pressed);  /* ext button by name; false if unknown */
    void (*reset)(void);                                 /* release inputs on link drop (may be NULL) */
} wiimote_extension_t;

/* Attach an extension to the base Wiimote (one at a time). Pass NULL to detach. */
void Wiimote_RegisterExtension(const wiimote_extension_t *ext);
