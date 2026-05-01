#include "tc358743.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "definitions.h"

#define TC358743_I2C_ADDR       0x0Fu
#define TC358743_REG_CHIPID     0x0000u
#define TC358743_REG_SYSCTL     0x0002u
#define TC358743_SYSCTL_SRESET  0x0001u
#define TC358743_RESET_HOLD_MS  1u

#define TC358743_TX_BUF_SIZE    8u
#define TC358743_RX_BUF_SIZE    4u

static DRV_HANDLE       i2cHandle = DRV_HANDLE_INVALID;
static uint8_t          txBuf[TC358743_TX_BUF_SIZE];
static uint8_t          rxBuf[TC358743_RX_BUF_SIZE];
static volatile bool    xferDone;
static volatile bool    xferErr;

static void TransferEventHandler(DRV_I2C_TRANSFER_EVENT event,
                                 DRV_I2C_TRANSFER_HANDLE transferHandle,
                                 uintptr_t context)
{
    (void)transferHandle;
    (void)context;

    if (event == DRV_I2C_TRANSFER_EVENT_COMPLETE)
    {
        xferDone = true;
    }
    else
    {
        xferErr = true;
    }
}

static bool wait_xfer(void)
{
    while (!xferDone && !xferErr) { /* ISR flips the flags */ }
    return xferDone;
}

static bool delay_ms(uint32_t ms)
{
    SYS_TIME_HANDLE h = SYS_TIME_HANDLE_INVALID;
    if (SYS_TIME_DelayMS(ms, &h) != SYS_TIME_SUCCESS) { return false; }
    while (!SYS_TIME_DelayIsComplete(h)) { }
    return true;
}

static bool tc358743_wr(uint16_t reg, const uint8_t *vals, size_t n)
{
    DRV_I2C_TRANSFER_HANDLE th = DRV_I2C_TRANSFER_HANDLE_INVALID;

    if (n + 2u > TC358743_TX_BUF_SIZE) { return false; }

    txBuf[0] = (uint8_t)(reg >> 8);
    txBuf[1] = (uint8_t)(reg & 0xFFu);
    for (size_t i = 0; i < n; i++) { txBuf[2u + i] = vals[i]; }

    xferDone = false;
    xferErr  = false;
    DRV_I2C_WriteTransferAdd(i2cHandle, TC358743_I2C_ADDR, txBuf, n + 2u, &th);
    if (th == DRV_I2C_TRANSFER_HANDLE_INVALID) { return false; }

    return wait_xfer();
}

static bool tc358743_rd(uint16_t reg, uint8_t *vals, size_t n)
{
    DRV_I2C_TRANSFER_HANDLE th = DRV_I2C_TRANSFER_HANDLE_INVALID;

    if (n > TC358743_RX_BUF_SIZE) { return false; }

    txBuf[0] = (uint8_t)(reg >> 8);
    txBuf[1] = (uint8_t)(reg & 0xFFu);

    xferDone = false;
    xferErr  = false;
    DRV_I2C_WriteReadTransferAdd(i2cHandle, TC358743_I2C_ADDR,
                                 txBuf, 2u, rxBuf, n, &th);
    if (th == DRV_I2C_TRANSFER_HANDLE_INVALID) { return false; }

    if (!wait_xfer()) { return false; }

    for (size_t i = 0; i < n; i++) { vals[i] = rxBuf[i]; }
    return true;
}

static bool tc358743_wr8(uint16_t reg, uint8_t val)
{
    return tc358743_wr(reg, &val, 1u);
}

static bool tc358743_wr16(uint16_t reg, uint16_t val)
{
    uint8_t b[2] = { (uint8_t)(val & 0xFFu), (uint8_t)((val >> 8) & 0xFFu) };
    return tc358743_wr(reg, b, 2u);
}

static bool tc358743_wr32(uint16_t reg, uint32_t val)
{
    uint8_t b[4] = {
        (uint8_t)(val         & 0xFFu),
        (uint8_t)((val >>  8) & 0xFFu),
        (uint8_t)((val >> 16) & 0xFFu),
        (uint8_t)((val >> 24) & 0xFFu),
    };
    return tc358743_wr(reg, b, 4u);
}

static bool tc358743_rd8(uint16_t reg, uint8_t *val)
{
    return tc358743_rd(reg, val, 1u);
}

static bool tc358743_rd16(uint16_t reg, uint16_t *val)
{
    uint8_t b[2];
    if (!tc358743_rd(reg, b, 2u)) { return false; }
    *val = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool tc358743_rd32(uint16_t reg, uint32_t *val)
{
    uint8_t b[4];
    if (!tc358743_rd(reg, b, 4u)) { return false; }
    *val = (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
    return true;
}

static bool tc358743_wr16_and_or(uint16_t reg, uint16_t mask, uint16_t val)
{
    uint16_t cur;
    if (!tc358743_rd16(reg, &cur)) { return false; }
    return tc358743_wr16(reg, (uint16_t)((cur & mask) | val));
}

void TC358743_Initialize(void)
{
    uint16_t chipid;

    i2cHandle = DRV_I2C_Open(DRV_I2C_INDEX_0, DRV_IO_INTENT_READWRITE);
    if (i2cHandle == DRV_HANDLE_INVALID)
    {
        printf("TC358743: DRV_I2C_Open failed\r\n");
        return;
    }
    DRV_I2C_TransferEventHandlerSet(i2cHandle, TransferEventHandler, 0);

    printf("TC358743: probe starting\r\n");

    if (!tc358743_wr16(TC358743_REG_SYSCTL, TC358743_SYSCTL_SRESET)
        || !delay_ms(TC358743_RESET_HOLD_MS)
        || !tc358743_wr16(TC358743_REG_SYSCTL, 0x0000u)
        || !delay_ms(TC358743_RESET_HOLD_MS))
    {
        printf("TC358743: software reset failed\r\n");
        return;
    }

    if (!tc358743_rd16(TC358743_REG_CHIPID, &chipid))
    {
        printf("TC358743: CHIPID read failed\r\n");
        return;
    }

    if ((chipid & 0xFF00u) == 0x0000u)
    {
        printf("TC358743: present (chipid=0x%04X)\r\n", chipid);
    }
    else
    {
        printf("TC358743: unexpected chipid=0x%04X\r\n", chipid);
    }
}

void TC358743_Tasks(void)
{
}
