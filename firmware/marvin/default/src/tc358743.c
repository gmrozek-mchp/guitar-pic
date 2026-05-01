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

typedef enum
{
    TC_STATE_INIT = 0,
    TC_STATE_SRESET_ASSERT,
    TC_STATE_SRESET_ASSERT_WAIT,
    TC_STATE_SRESET_ASSERT_DELAY,
    TC_STATE_SRESET_RELEASE,
    TC_STATE_SRESET_RELEASE_WAIT,
    TC_STATE_SRESET_RELEASE_DELAY,
    TC_STATE_READ_CHIPID,
    TC_STATE_READ_CHIPID_WAIT,
    TC_STATE_REPORT,
    TC_STATE_DONE,
    TC_STATE_ERROR,
} TC_State;

static TC_State                        state;
static DRV_HANDLE                      i2cHandle;
static uint8_t                         txBuf[4];
static uint8_t                         rxBuf[2];
static volatile bool                   xferReady;
static volatile DRV_I2C_TRANSFER_EVENT xferEvent;
static SYS_TIME_HANDLE                 delayHandle = SYS_TIME_HANDLE_INVALID;

static void TransferEventHandler(DRV_I2C_TRANSFER_EVENT event,
                                 DRV_I2C_TRANSFER_HANDLE transferHandle,
                                 uintptr_t context)
{
    (void)transferHandle;
    (void)context;
    xferEvent = event;
    xferReady = true;
}

static bool StartRegWrite16(uint16_t reg, uint16_t val)
{
    DRV_I2C_TRANSFER_HANDLE th = DRV_I2C_TRANSFER_HANDLE_INVALID;

    txBuf[0] = (uint8_t)(reg >> 8);
    txBuf[1] = (uint8_t)(reg & 0xFFu);
    txBuf[2] = (uint8_t)(val & 0xFFu);
    txBuf[3] = (uint8_t)((val >> 8) & 0xFFu);

    xferReady = false;
    DRV_I2C_WriteTransferAdd(i2cHandle, TC358743_I2C_ADDR, txBuf, 4, &th);
    return th != DRV_I2C_TRANSFER_HANDLE_INVALID;
}

static bool StartRegRead16(uint16_t reg)
{
    DRV_I2C_TRANSFER_HANDLE th = DRV_I2C_TRANSFER_HANDLE_INVALID;

    txBuf[0] = (uint8_t)(reg >> 8);
    txBuf[1] = (uint8_t)(reg & 0xFFu);

    xferReady = false;
    DRV_I2C_WriteReadTransferAdd(i2cHandle, TC358743_I2C_ADDR,
                                 txBuf, 2, rxBuf, 2, &th);
    return th != DRV_I2C_TRANSFER_HANDLE_INVALID;
}

static bool StartDelayMs(uint32_t ms)
{
    delayHandle = SYS_TIME_HANDLE_INVALID;
    return SYS_TIME_DelayMS(ms, &delayHandle) == SYS_TIME_SUCCESS;
}

void TC358743_Initialize(void)
{
    i2cHandle = DRV_I2C_Open(DRV_I2C_INDEX_0, DRV_IO_INTENT_READWRITE);
    if (i2cHandle == DRV_HANDLE_INVALID)
    {
        printf("TC358743: DRV_I2C_Open failed\r\n");
        state = TC_STATE_ERROR;
        return;
    }

    DRV_I2C_TransferEventHandlerSet(i2cHandle, TransferEventHandler, 0);
    printf("TC358743: probe starting\r\n");
    state = TC_STATE_SRESET_ASSERT;
}

void TC358743_Tasks(void)
{
    switch (state)
    {
    case TC_STATE_INIT:
        break;

    case TC_STATE_SRESET_ASSERT:
        if (!StartRegWrite16(TC358743_REG_SYSCTL, TC358743_SYSCTL_SRESET))
        {
            printf("TC358743: SRESET assert queue failed\r\n");
            state = TC_STATE_ERROR;
            break;
        }
        state = TC_STATE_SRESET_ASSERT_WAIT;
        break;

    case TC_STATE_SRESET_ASSERT_WAIT:
        if (!xferReady) { break; }
        if (xferEvent != DRV_I2C_TRANSFER_EVENT_COMPLETE)
        {
            printf("TC358743: SRESET assert error (evt=%d)\r\n", (int)xferEvent);
            state = TC_STATE_ERROR;
            break;
        }
        if (!StartDelayMs(TC358743_RESET_HOLD_MS))
        {
            state = TC_STATE_ERROR;
            break;
        }
        state = TC_STATE_SRESET_ASSERT_DELAY;
        break;

    case TC_STATE_SRESET_ASSERT_DELAY:
        if (SYS_TIME_DelayIsComplete(delayHandle))
        {
            state = TC_STATE_SRESET_RELEASE;
        }
        break;

    case TC_STATE_SRESET_RELEASE:
        if (!StartRegWrite16(TC358743_REG_SYSCTL, 0x0000u))
        {
            printf("TC358743: SRESET release queue failed\r\n");
            state = TC_STATE_ERROR;
            break;
        }
        state = TC_STATE_SRESET_RELEASE_WAIT;
        break;

    case TC_STATE_SRESET_RELEASE_WAIT:
        if (!xferReady) { break; }
        if (xferEvent != DRV_I2C_TRANSFER_EVENT_COMPLETE)
        {
            printf("TC358743: SRESET release error (evt=%d)\r\n", (int)xferEvent);
            state = TC_STATE_ERROR;
            break;
        }
        if (!StartDelayMs(TC358743_RESET_HOLD_MS))
        {
            state = TC_STATE_ERROR;
            break;
        }
        state = TC_STATE_SRESET_RELEASE_DELAY;
        break;

    case TC_STATE_SRESET_RELEASE_DELAY:
        if (SYS_TIME_DelayIsComplete(delayHandle))
        {
            state = TC_STATE_READ_CHIPID;
        }
        break;

    case TC_STATE_READ_CHIPID:
        if (!StartRegRead16(TC358743_REG_CHIPID))
        {
            printf("TC358743: CHIPID read queue failed\r\n");
            state = TC_STATE_ERROR;
            break;
        }
        state = TC_STATE_READ_CHIPID_WAIT;
        break;

    case TC_STATE_READ_CHIPID_WAIT:
        if (!xferReady) { break; }
        if (xferEvent != DRV_I2C_TRANSFER_EVENT_COMPLETE)
        {
            printf("TC358743: CHIPID read error (evt=%d)\r\n", (int)xferEvent);
            state = TC_STATE_ERROR;
            break;
        }
        state = TC_STATE_REPORT;
        break;

    case TC_STATE_REPORT:
    {
        uint8_t chipHigh = rxBuf[1];
        uint8_t rev      = rxBuf[0];
        if (chipHigh == 0x00u)
        {
            printf("TC358743: present (chipid=0x%02X%02X)\r\n", chipHigh, rev);
            state = TC_STATE_DONE;
        }
        else
        {
            printf("TC358743: unexpected chipid=0x%02X%02X\r\n", chipHigh, rev);
            state = TC_STATE_ERROR;
        }
        break;
    }

    case TC_STATE_DONE:
    case TC_STATE_ERROR:
    default:
        break;
    }
}
