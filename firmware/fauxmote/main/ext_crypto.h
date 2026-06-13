#pragma once

#include <stdint.h>

/* Wii extension data cipher. The base Wiimote owns this: it captures the host's
 * key write and encrypts the extension's plaintext bytes on the way out. */
typedef struct {
    uint8_t ft[8];
    uint8_t sb[8];
} ext_crypto_t;

/* Derive the ft/sb tables from the 16-byte key the host wrote to register 0x40. */
void ExtCrypto_GenTables(ext_crypto_t *st, const uint8_t key[16]);

/* Encrypt len bytes in place (device->host). addr is the extension-register
 * address of buf[0]; only addr % 8 matters. */
void ExtCrypto_Encrypt(const ext_crypto_t *st, uint8_t *buf, int addr, int len);
