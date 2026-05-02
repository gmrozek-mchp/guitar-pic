#include "tc358743.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "definitions.h"

#define TC358743_I2C_ADDR       0x0Fu

#define REFCLK_HZ               27000000u
#define CSI_LANES               2u
#define PLL_PRD                 4u
/* FBD=144 → hsck = (27M/4)*144 = 972 Mbps/lane. Pairs with the SAM9X75
 * D-PHY HSFREQRANGE band 0x0A (DWC Gen3 table, 950-1000 Mbps).
 * Kernel tabulates this as the rate for 720p60 RGB888 / 1080p50 YUV422
 * over 2 lanes. 2 lanes × 972 Mbps = 1.944 Gbps: fits 720p60 RGB888
 * (1.33 Gbps) with headroom, plus 1080p30 RGB888 (1.49 Gbps). */
#define PLL_FBD                 144u
#define CSI_BPS_PER_LANE        ((REFCLK_HZ / PLL_PRD) * PLL_FBD)
#define FIFO_LEVEL              374u   /* kernel hardcodes this at all rates. */

/* D-PHY timing counts for 972 Mbps from kernel driver tc358743.c case 972000000 */
#define LINEINITCNT_VAL         0x00001B58u
#define LPTXTIMECNT_VAL         0x00000007u
#define TCLK_HEADERCNT_VAL      0x00002806u
#define TCLK_TRAILCNT_VAL       0x00000000u
#define THS_HEADERCNT_VAL       0x00000806u
#define TWAKEUP_VAL             0x00004268u
#define TCLK_POSTCNT_VAL        0x00000008u
#define THS_TRAILCNT_VAL        0x00000005u
#define HSTXVREGCNT_VAL         0x00000000u

#define CHIPID                  0x0000u
#define SYSCTL                  0x0002u
#define CONFCTL                 0x0004u
#define FIFOCTL                 0x0006u
#define PLLCTL0                 0x0020u
#define PLLCTL1                 0x0022u
#define CECHCLK                 0x0028u
#define CECLCLK                 0x002Au
#define CLW_CNTRL               0x0140u
#define D0W_CNTRL               0x0144u
#define D1W_CNTRL               0x0148u
#define D2W_CNTRL               0x014Cu
#define D3W_CNTRL               0x0150u
#define STARTCNTRL              0x0204u
#define LINEINITCNT             0x0210u
#define LPTXTIMECNT             0x0214u
#define TCLK_HEADERCNT          0x0218u
#define TCLK_TRAILCNT           0x021Cu
#define THS_HEADERCNT           0x0220u
#define TWAKEUP                 0x0224u
#define TCLK_POSTCNT            0x0228u
#define THS_TRAILCNT            0x022Cu
#define HSTXVREGCNT             0x0230u
#define HSTXVREGEN              0x0234u
#define TXOPTIONCNTRL           0x0238u
#define CSI_STATUS              0x0410u
#define CSI_ERR                 0x044Cu
#define CSI_CONFW               0x0500u
#define CSI_START               0x0518u
#define SYS_STATUS              0x8520u
#define VI_STATUS1              0x8522u
#define VI_STATUS3              0x8528u
#define DE_WIDTH_H_LO           0x8582u
#define DE_WIDTH_H_HI           0x8583u
#define DE_WIDTH_V_LO           0x8588u
#define DE_WIDTH_V_HI           0x8589u
#define H_SIZE_LO               0x858Au
#define H_SIZE_HI               0x858Bu
#define V_SIZE_LO               0x858Cu
#define V_SIZE_HI               0x858Du
#define FV_CNT_LO               0x85A1u
#define FV_CNT_HI               0x85A2u
#define PHY_CTL0                0x8531u
#define PHY_CTL1                0x8532u
#define PHY_CTL2                0x8533u
#define PHY_EN                  0x8534u
#define PHY_BIAS                0x8536u
#define PHY_CSQ                 0x853Fu
#define SYS_FREQ0               0x8540u
#define SYS_FREQ1               0x8541u
#define DDC_CTL                 0x8543u
#define HPD_CTL                 0x8544u
#define AVM_CTL                 0x8546u
#define HDMI_DET                0x8552u
#define VI_MODE                 0x8570u
#define VOUT_SET2               0x8573u
#define VOUT_SET3               0x8574u
#define VI_REP                  0x8576u
#define VI_MUTE                 0x857Fu
#define FH_MIN0                 0x85AAu
#define FH_MIN1                 0x85ABu
#define FH_MAX0                 0x85ACu
#define FH_MAX1                 0x85ADu
#define HV_RST                  0x85AFu
#define EDID_MODE               0x85C7u
#define EDID_LEN1               0x85CAu
#define EDID_LEN2               0x85CBu
#define LOCKDET_REF0            0x8630u
#define LOCKDET_REF1            0x8631u
#define LOCKDET_REF2            0x8632u
#define NCO_F0_MOD              0x8670u
#define EDID_RAM                0x8C00u

#define MASK_CTXRST             0x0200u
#define MASK_HDMIRST            0x0100u
#define MASK_SLEEP              0x0001u
#define MASK_IRRST              0x0800u
#define MASK_CECRST             0x0400u
#define MASK_YCBCRFMT           0x00C0u
#define MASK_VBUFEN             0x0001u
#define MASK_ABUFEN             0x0002u
#define MASK_AUTOINDEX          0x0004u
#define MASK_PLL_PRD            0xF000u
#define MASK_PLL_FBD            0x01FFu
#define MASK_PLL_FRS            0x0C00u
#define MASK_CKEN               0x0010u
#define MASK_RESETB             0x0002u
#define MASK_PLL_EN             0x0001u
#define MASK_CLW_LANEDISABLE    0x00000001u
#define MASK_D0W_LANEDISABLE    0x00000001u
#define MASK_D1W_LANEDISABLE    0x00000001u
#define MASK_D2W_LANEDISABLE    0x00000001u
#define MASK_D3W_LANEDISABLE    0x00000001u
#define MASK_CLM_HSTXVREGEN     0x0001u
#define MASK_D0M_HSTXVREGEN     0x0002u
#define MASK_D1M_HSTXVREGEN     0x0004u
#define MASK_D2M_HSTXVREGEN     0x0008u
#define MASK_D3M_HSTXVREGEN     0x0010u
#define MASK_CONTCLKMODE        0x00000001u
#define MASK_START              0x00000001u
#define MASK_STRT               0x00000001u
#define MASK_MODE_SET           0xA0000000u
#define MASK_MODE_CLEAR         0xC0000000u
#define MASK_ADDRESS_CSI_CONTROL        0x03000000u
#define MASK_ADDRESS_CSI_INT_ENA        0x06000000u
#define MASK_ADDRESS_CSI_ERR_INTENA     0x14000000u
#define MASK_ADDRESS_CSI_ERR_HALT       0x15000000u
#define MASK_CSI_MODE           0x8000u
#define MASK_TXHSMD             0x0080u
#define MASK_NOL_2              0x0002u
#define MASK_INTER              0x00000004u
#define MASK_INER               0x00000200u
#define MASK_WCER               0x00000100u
#define MASK_QUNK               0x00000010u
#define MASK_TXBRK              0x00000002u
#define MASK_PHY_SYSCLK_IND     0x02u
#define MASK_NCO_F0_MOD         0x03u
#define MASK_NCO_F0_MOD_27MHZ   0x01u
#define MASK_ENABLE_PHY         0x01u
#define MASK_CSQ_CNT            0x0Fu
#define MASK_PHY_AUTO_RST1      0xF0u
#define MASK_FREQ_RANGE_MODE    0x0Fu
#define MASK_PHY_AUTO_RSTn      0x07u
#define MASK_HDMI_DET_V         0x30u
#define MASK_H_PI_RST           0x20u
#define MASK_V_PI_RST           0x10u
#define MASK_DDC5V_MODE         0x03u
#define MASK_EDID_MODE          0x03u
#define MASK_EDID_MODE_E_DDC    0x02u
#define MASK_RGB_DVI            0x08u
#define MASK_SEL422             0x80u
#define MASK_VOUT_422FIL_100    0x40u
#define MASK_VOUT_COLOR_SEL     0xE0u
#define MASK_IN_REP_HEN         0x10u   /* Input-repeat horizontal enable */
#define MASK_IN_REP             0x0Fu   /* Input-repeat count (low nibble) */
#define MASK_VOUT_COLOR_RGB_FULL 0x00u
#define MASK_VOUT_COLOR_601_YCBCR_LIMITED  0x60u
#define MASK_YCBCRFMT_422_8_BIT 0x00C0u
#define MASK_VOUTCOLORMODE      0x03u
#define MASK_VOUTCOLORMODE_AUTO 0x01u
#define MASK_VOUT_EXTCNT        0x08u
#define MASK_AUTO_MUTE          0xC0u
#define MASK_VI_MUTE            0x10u

#define MASK_S_DDC5V            0x01u
#define MASK_S_TMDS             0x02u
#define MASK_S_PHY_PLL          0x04u
#define MASK_S_PHY_SCDT         0x08u
#define MASK_S_HDMI             0x10u
#define MASK_S_SYNC             0x80u
#define MASK_S_V_INTERLACE      0x01u
#define MASK_S_V_COLOR          0x1Eu
#define MASK_LIMITED            0x01u

#define MASK_S_WSYNC            0x0400u
#define MASK_S_TXACT            0x0200u
#define MASK_S_RXACT            0x0100u
#define MASK_S_HLT              0x0001u

#define STATUS_POLL_MS          100u

#define DDC5V_DELAY_100_MS      2u

#define SYSCTL_SRESET           0x0001u
#define RESET_HOLD_MS           1u
#define PLL_SETTLE_US           10u

#define TC358743_TX_BUF_SIZE    132u
#define TC358743_RX_BUF_SIZE    4u

#define EDID_BLOCK_SIZE         128u
#define EDID_BLOCK_COUNT        2u
#define EDID_TOTAL_SIZE         (EDID_BLOCK_SIZE * EDID_BLOCK_COUNT)
#define EDID_POST_DROP_MS       150u
#define EDID_POST_RISE_MS       200u

#define MASK_HPD_OUT0           0x01u

static DRV_HANDLE       i2cHandle = DRV_HANDLE_INVALID;
static uint8_t          txBuf[TC358743_TX_BUF_SIZE];
static uint8_t          rxBuf[TC358743_RX_BUF_SIZE];
static volatile bool    xferDone;
static volatile bool    xferErr;

static volatile uint8_t  s_sysStatus      = 0u;
static volatile uint16_t s_detectedWidth  = 0u;
static volatile uint16_t s_detectedHeight = 0u;

static void TransferEventHandler(DRV_I2C_TRANSFER_EVENT event,
                                 DRV_I2C_TRANSFER_HANDLE transferHandle,
                                 uintptr_t context)
{
    (void)transferHandle;
    (void)context;
    if (event == DRV_I2C_TRANSFER_EVENT_COMPLETE) { xferDone = true; }
    else { xferErr = true; }
}

static bool wait_xfer(void)
{
    while (!xferDone && !xferErr) { }
    return xferDone;
}

static bool delay_ms(uint32_t ms)
{
    SYS_TIME_HANDLE h = SYS_TIME_HANDLE_INVALID;
    if (SYS_TIME_DelayMS(ms, &h) != SYS_TIME_SUCCESS) { return false; }
    while (!SYS_TIME_DelayIsComplete(h)) { }
    return true;
}

static bool delay_us(uint32_t us)
{
    SYS_TIME_HANDLE h = SYS_TIME_HANDLE_INVALID;
    if (SYS_TIME_DelayUS(us, &h) != SYS_TIME_SUCCESS) { return false; }
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

static bool tc358743_wr8_and_or(uint16_t reg, uint8_t mask, uint8_t val)
{
    uint8_t cur;
    if (!tc358743_rd8(reg, &cur)) { return false; }
    return tc358743_wr8(reg, (uint8_t)((cur & mask) | val));
}

static bool tc358743_wr16_and_or(uint16_t reg, uint16_t mask, uint16_t val)
{
    uint16_t cur;
    if (!tc358743_rd16(reg, &cur)) { return false; }
    return tc358743_wr16(reg, (uint16_t)((cur & mask) | val));
}

static bool tc358743_reset(uint16_t mask)
{
    uint16_t sysctl;
    if (!tc358743_rd16(SYSCTL, &sysctl)) { return false; }
    if (!tc358743_wr16(SYSCTL, (uint16_t)(sysctl | mask))) { return false; }
    return tc358743_wr16(SYSCTL, (uint16_t)(sysctl & ~mask));
}

static bool tc358743_sleep_mode(bool enable)
{
    return tc358743_wr16_and_or(SYSCTL, (uint16_t)~MASK_SLEEP,
                                enable ? MASK_SLEEP : 0u);
}

static bool tc358743_set_ref_clk(void)
{
    uint32_t sys_freq      = REFCLK_HZ / 10000u;
    uint32_t fh_min        = REFCLK_HZ / 100000u;
    uint32_t fh_max        = (fh_min * 66u) / 10u;
    uint32_t lockdet_ref   = REFCLK_HZ / 100u;
    uint32_t cec_freq      = (656u * sys_freq) / 4200u;

    return tc358743_wr8(SYS_FREQ0,    (uint8_t)(sys_freq & 0xFFu))
        && tc358743_wr8(SYS_FREQ1,    (uint8_t)((sys_freq >> 8) & 0xFFu))
        && tc358743_wr8_and_or(PHY_CTL0, (uint8_t)~MASK_PHY_SYSCLK_IND, 0u)
        && tc358743_wr8(FH_MIN0,      (uint8_t)(fh_min & 0xFFu))
        && tc358743_wr8(FH_MIN1,      (uint8_t)((fh_min >> 8) & 0xFFu))
        && tc358743_wr8(FH_MAX0,      (uint8_t)(fh_max & 0xFFu))
        && tc358743_wr8(FH_MAX1,      (uint8_t)((fh_max >> 8) & 0xFFu))
        && tc358743_wr8(LOCKDET_REF0, (uint8_t)(lockdet_ref & 0xFFu))
        && tc358743_wr8(LOCKDET_REF1, (uint8_t)((lockdet_ref >> 8) & 0xFFu))
        && tc358743_wr8(LOCKDET_REF2, (uint8_t)((lockdet_ref >> 16) & 0x0Fu))
        && tc358743_wr8_and_or(NCO_F0_MOD, (uint8_t)~MASK_NCO_F0_MOD,
                               MASK_NCO_F0_MOD_27MHZ)
        && tc358743_wr16(CECHCLK, (uint16_t)cec_freq)
        && tc358743_wr16(CECLCLK, (uint16_t)cec_freq);
}

static bool tc358743_set_hdmi_phy(void)
{
    uint8_t phy_ctl1 = (uint8_t)(((1600u / 200u) << 4) & MASK_PHY_AUTO_RST1)
                     | (uint8_t)((1u - 1u) & MASK_FREQ_RANGE_MODE);

    return tc358743_wr8_and_or(PHY_EN, (uint8_t)~MASK_ENABLE_PHY, 0u)
        && tc358743_wr8(PHY_CTL1, phy_ctl1)
        && tc358743_wr8_and_or(PHY_CTL2, (uint8_t)~MASK_PHY_AUTO_RSTn, 0u)
        && tc358743_wr8(PHY_BIAS, 0x40u)
        && tc358743_wr8(PHY_CSQ, (uint8_t)(0x0Au & MASK_CSQ_CNT))
        && tc358743_wr8(AVM_CTL, 45u)
        && tc358743_wr8_and_or(HDMI_DET, (uint8_t)~MASK_HDMI_DET_V, 0u)
        && tc358743_wr8_and_or(HV_RST,
                               (uint8_t)~(MASK_H_PI_RST | MASK_V_PI_RST), 0u)
        && tc358743_wr8_and_or(PHY_EN, (uint8_t)~MASK_ENABLE_PHY,
                               MASK_ENABLE_PHY);
}

static bool tc358743_set_pll(void)
{
    uint16_t pllctl0_new = (uint16_t)(((PLL_PRD - 1u) << 12) & MASK_PLL_PRD)
                         | (uint16_t)((PLL_FBD - 1u) & MASK_PLL_FBD);
    uint32_t hsck        = CSI_BPS_PER_LANE;
    uint16_t pll_frs;

    if      (hsck > 500000000u) { pll_frs = 0x0u; }
    else if (hsck > 250000000u) { pll_frs = 0x1u; }
    else if (hsck > 125000000u) { pll_frs = 0x2u; }
    else                        { pll_frs = 0x3u; }

    if (!tc358743_sleep_mode(true)) { return false; }
    if (!tc358743_wr16(PLLCTL0, pllctl0_new)) { return false; }
    if (!tc358743_wr16_and_or(PLLCTL1,
                              (uint16_t)~(MASK_PLL_FRS | MASK_RESETB | MASK_PLL_EN),
                              (uint16_t)(((pll_frs << 10) & MASK_PLL_FRS)
                                         | MASK_RESETB | MASK_PLL_EN)))
    {
        return false;
    }
    if (!delay_us(PLL_SETTLE_US)) { return false; }
    if (!tc358743_wr16_and_or(PLLCTL1, (uint16_t)~MASK_CKEN, MASK_CKEN))
    {
        return false;
    }
    return tc358743_sleep_mode(false);
}

static bool tc358743_set_csi(void)
{
    if (!tc358743_reset(MASK_CTXRST)) { return false; }

    if (!tc358743_wr32(D2W_CNTRL, MASK_D2W_LANEDISABLE)) { return false; }
    if (!tc358743_wr32(D3W_CNTRL, MASK_D3W_LANEDISABLE)) { return false; }

    if (!tc358743_wr32(LINEINITCNT,    LINEINITCNT_VAL))    { return false; }
    if (!tc358743_wr32(LPTXTIMECNT,    LPTXTIMECNT_VAL))    { return false; }
    if (!tc358743_wr32(TCLK_HEADERCNT, TCLK_HEADERCNT_VAL)) { return false; }
    if (!tc358743_wr32(TCLK_TRAILCNT,  TCLK_TRAILCNT_VAL))  { return false; }
    if (!tc358743_wr32(THS_HEADERCNT,  THS_HEADERCNT_VAL))  { return false; }
    if (!tc358743_wr32(TWAKEUP,        TWAKEUP_VAL))        { return false; }
    if (!tc358743_wr32(TCLK_POSTCNT,   TCLK_POSTCNT_VAL))   { return false; }
    if (!tc358743_wr32(THS_TRAILCNT,   THS_TRAILCNT_VAL))   { return false; }
    if (!tc358743_wr32(HSTXVREGCNT,    HSTXVREGCNT_VAL))    { return false; }

    if (!tc358743_wr32(HSTXVREGEN,
                       MASK_CLM_HSTXVREGEN
                       | MASK_D0M_HSTXVREGEN
                       | MASK_D1M_HSTXVREGEN))
    {
        return false;
    }

    if (!tc358743_wr32(TXOPTIONCNTRL, MASK_CONTCLKMODE)) { return false; }
    if (!tc358743_wr32(STARTCNTRL,    MASK_START))       { return false; }
    if (!tc358743_wr32(CSI_START,     MASK_STRT))        { return false; }

    if (!tc358743_wr32(CSI_CONFW,
                       MASK_MODE_SET
                       | MASK_ADDRESS_CSI_CONTROL
                       | MASK_CSI_MODE
                       | MASK_TXHSMD
                       | MASK_NOL_2))
    {
        return false;
    }

    if (!tc358743_wr32(CSI_CONFW,
                       MASK_MODE_SET
                       | MASK_ADDRESS_CSI_ERR_INTENA
                       | MASK_TXBRK | MASK_QUNK | MASK_WCER | MASK_INER))
    {
        return false;
    }

    if (!tc358743_wr32(CSI_CONFW,
                       MASK_MODE_CLEAR
                       | MASK_ADDRESS_CSI_ERR_HALT
                       | MASK_TXBRK | MASK_QUNK))
    {
        return false;
    }

    return tc358743_wr32(CSI_CONFW,
                         MASK_MODE_SET
                         | MASK_ADDRESS_CSI_INT_ENA
                         | MASK_INTER);
}

/* EDID: base (VESA 1.3) + CEA-861-D extension.
 * Base block preferred DTD is 1280x720@60p (CEA VIC 4). Extension's CEA
 * Video Data Block lists VIC 4 (native, 720p60 16:9), VIC 19 (720p50 16:9),
 * VIC 34 (1080p30 16:9), VIC 3 (720x480p60 16:9), and VIC 2 (720x480p60 4:3)
 * as accepted modes. 480p is last-preference; native + preferred is still
 * 720p60, so cooperating sources default there. 480p support is here for
 * the Wii/ElectronWarp path which only outputs 480p.
 * Both block checksums (bytes 127 and 255) are patched at runtime. */
static uint8_t edid_block[EDID_TOTAL_SIZE] = {
    /* ===== Block 0: VESA EDID 1.3 ===== */
    /* 0..7:   EDID header */
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    /* 8..9:   Manufacturer ID "LNX" (Linux Foundation virtual vendor) */
    0x31, 0xD8,
    /* 10..11: Product code 0x0001 */
    0x01, 0x00,
    /* 12..15: Serial number 0x00000001 */
    0x01, 0x00, 0x00, 0x00,
    /* 16..17: Week 1, year 2026 (= 36 decimal) */
    0x01, 0x24,
    /* 18..19: EDID 1.3 */
    0x01, 0x03,
    /* 20:     Video input = digital */
    0x80,
    /* 21..22: Max image size 16x9 cm */
    0x10, 0x09,
    /* 23:     Display gamma 2.20 */
    0x78,
    /* 24:     Features = RGB display, sRGB default, preferred timing native */
    0x0E,
    /* 25..34: Chromaticity (sRGB / BT.709 primaries, D65 white) */
    0xDE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54,
    /* 35..37: Established timings — bit 5 of byte 35 = 640x480@60 */
    0x20, 0x00, 0x00,
    /* 38..53: Standard timings (unused, all 0x01) */
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    /* 54..71: Detailed timing 1 = 1280x720p@60Hz (CEA VIC 4, pclk 74.25 MHz,
     *         H: 1280 active / 370 blank / 110 fp / 40 sync,
     *         V: 720 active / 30 blank / 5 fp / 5 sync, pos-H / pos-V) */
    0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
    0x6E, 0x28, 0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00,
    0x00, 0x1E,
    /* 72..89: Detailed timing 2 = monitor range limits
     *         V: 50-75 Hz, H: 30-75 kHz, max pclk 150 MHz */
    0x00, 0x00, 0x00, 0xFD, 0x00, 0x32, 0x4B, 0x1E,
    0x4B, 0x0F, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20,
    /* 90..107: Detailed timing 3 = monitor name "marvin-hdmi" */
    0x00, 0x00, 0x00, 0xFC, 0x00, 'm', 'a', 'r',
    'v', 'i', 'n', '-', 'h', 'd', 'm', 'i',
    0x0A, 0x20,
    /* 108..125: Detailed timing 4 = dummy descriptor (tag 0x10) */
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    /* 126: One extension block follows */
    0x01,
    /* 127: Base-block checksum — patched at runtime */
    0x00,

    /* ===== Block 1: CEA-861-D extension ===== */
    /* 128: CEA extension tag */
    0x02,
    /* 129: CEA-861-D revision */
    0x03,
    /* 130: DTD offset (18 = end of data-block collection) */
    0x12,
    /* 131: Flags — no audio, RGB-only, 1 native format */
    0x01,
    /* 132..137: Video Data Block (tag=2, len=5): VIC 4 native, VIC 19,
     *           VIC 34, VIC 3, VIC 2.
     *           720p60 preferred/native; 720p50, 1080p30, 480p60 16:9,
     *           480p60 4:3 as fallbacks. */
    0x45, 0x84, 0x13, 0x22, 0x03, 0x02,
    /* 138..143: HDMI VSDB (tag=3, len=5): OUI 0x000C03 LE, phys addr 1.0.0.0 */
    0x65, 0x03, 0x0C, 0x00, 0x10, 0x00,
    /* 144..254: Padding (111 bytes) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 255: Extension-block checksum — patched at runtime */
    0x00,
};

static bool tc358743_hpd_set(bool asserted)
{
    return tc358743_wr8_and_or(HPD_CTL, (uint8_t)~MASK_HPD_OUT0,
                               asserted ? MASK_HPD_OUT0 : 0u);
}

static void tc358743_fixup_edid_checksums(void)
{
    for (size_t blk = 0; blk < EDID_BLOCK_COUNT; blk++)
    {
        size_t base = blk * EDID_BLOCK_SIZE;
        uint8_t sum = 0;
        for (size_t i = 0; i < EDID_BLOCK_SIZE - 1u; i++)
        {
            sum = (uint8_t)(sum + edid_block[base + i]);
        }
        edid_block[base + EDID_BLOCK_SIZE - 1u] = (uint8_t)(0u - (uint32_t)sum);
    }
}

static bool tc358743_load_edid(void)
{
    tc358743_fixup_edid_checksums();

    if (!tc358743_hpd_set(false)) { return false; }
    if (!tc358743_wr8(EDID_LEN1, EDID_BLOCK_COUNT)) { return false; }
    if (!tc358743_wr8(EDID_LEN2, 0x00u)) { return false; }

    for (size_t blk = 0; blk < EDID_BLOCK_COUNT; blk++)
    {
        uint16_t addr = (uint16_t)(EDID_RAM + blk * EDID_BLOCK_SIZE);
        if (!tc358743_wr(addr, &edid_block[blk * EDID_BLOCK_SIZE],
                         EDID_BLOCK_SIZE))
        {
            return false;
        }
    }

    if (!delay_ms(EDID_POST_DROP_MS)) { return false; }
    return tc358743_hpd_set(true);
}

static bool tc358743_set_csi_color_space_rgb888(void)
{
    /* Full-range RGB888 (MIPI DT 0x24). Matches kernel driver's RGB888
     * path for MEDIA_BUS_FMT_RGB888_1X24. IN_REP_HEN / IN_REP cleared
     * so the bridge doesn't apply pixel-repetition de-replication
     * (real HDMI sources at 720p/1080p don't carry pixel repetition;
     * leaving IN_REP on would halve horizontal resolution). */
    return tc358743_wr8_and_or(VOUT_SET2,
                               (uint8_t)~(MASK_SEL422 | MASK_VOUT_422FIL_100),
                               0u)
        && tc358743_wr8_and_or(VI_REP,
                               (uint8_t)~(MASK_VOUT_COLOR_SEL
                                          | MASK_IN_REP_HEN
                                          | MASK_IN_REP),
                               MASK_VOUT_COLOR_RGB_FULL)
        && tc358743_wr16_and_or(CONFCTL, (uint16_t)~MASK_YCBCRFMT, 0u);
}

static bool tc358743_do_init(void)
{
    if (!tc358743_wr16_and_or(SYSCTL,
                              (uint16_t)~(MASK_IRRST | MASK_CECRST),
                              (uint16_t)(MASK_IRRST | MASK_CECRST)))
    {
        printf("TC358743: init: IR/CEC reset hold failed\r\n");
        return false;
    }

    if (!tc358743_reset(MASK_CTXRST | MASK_HDMIRST))
    {
        printf("TC358743: init: CTX/HDMI reset failed\r\n");
        return false;
    }

    if (!tc358743_sleep_mode(false))
    {
        printf("TC358743: init: sleep-mode-off failed\r\n");
        return false;
    }

    if (!tc358743_wr16(FIFOCTL, FIFO_LEVEL))
    {
        printf("TC358743: init: FIFOCTL failed\r\n");
        return false;
    }

    if (!tc358743_set_ref_clk())
    {
        printf("TC358743: init: set_ref_clk failed\r\n");
        return false;
    }

    if (!tc358743_wr8_and_or(DDC_CTL, (uint8_t)~MASK_DDC5V_MODE,
                             DDC5V_DELAY_100_MS))
    {
        printf("TC358743: init: DDC_CTL failed\r\n");
        return false;
    }

    if (!tc358743_wr8_and_or(EDID_MODE, (uint8_t)~MASK_EDID_MODE,
                             MASK_EDID_MODE_E_DDC))
    {
        printf("TC358743: init: EDID_MODE failed\r\n");
        return false;
    }

    if (!tc358743_set_hdmi_phy())
    {
        printf("TC358743: init: set_hdmi_phy failed\r\n");
        return false;
    }

    if (!tc358743_wr8_and_or(VI_MODE, (uint8_t)~MASK_RGB_DVI, 0u))
    {
        printf("TC358743: init: VI_MODE failed\r\n");
        return false;
    }

    if (!tc358743_wr8_and_or(VOUT_SET2, (uint8_t)~MASK_VOUTCOLORMODE,
                             MASK_VOUTCOLORMODE_AUTO))
    {
        printf("TC358743: init: VOUT_SET2 failed\r\n");
        return false;
    }

    if (!tc358743_wr8(VOUT_SET3, MASK_VOUT_EXTCNT))
    {
        printf("TC358743: init: VOUT_SET3 failed\r\n");
        return false;
    }

    /* AUTOINDEX enables CSI-TX auto-indexing of packet metadata (line
     * numbers etc.). Kernel driver sets this during audio setup; we need
     * it even without audio so CSI2DC sees monotonically-indexed line
     * packets per frame. Without it, VPROW counts only half the lines. */
    if (!tc358743_wr16_and_or(CONFCTL, 0xFFFFu, MASK_AUTOINDEX))
    {
        printf("TC358743: init: CONFCTL AUTOINDEX failed\r\n");
        return false;
    }

    if (!tc358743_set_pll())
    {
        printf("TC358743: init: set_pll failed\r\n");
        return false;
    }

    if (!tc358743_set_csi())
    {
        printf("TC358743: init: set_csi failed\r\n");
        return false;
    }

    if (!tc358743_set_csi_color_space_rgb888())
    {
        printf("TC358743: init: set_csi_color_space failed\r\n");
        return false;
    }

    if (!tc358743_load_edid())
    {
        printf("TC358743: init: load_edid failed\r\n");
        return false;
    }

    return true;
}

void TC358743_Initialize(void)
{
    uint16_t chipid;
    uint8_t  sys_status;

    i2cHandle = DRV_I2C_Open(DRV_I2C_INDEX_0, DRV_IO_INTENT_READWRITE);
    if (i2cHandle == DRV_HANDLE_INVALID)
    {
        printf("TC358743: DRV_I2C_Open failed\r\n");
        return;
    }
    DRV_I2C_TransferEventHandlerSet(i2cHandle, TransferEventHandler, 0);

    printf("TC358743: probe starting\r\n");

    if (!tc358743_wr16(SYSCTL, SYSCTL_SRESET)
        || !delay_ms(RESET_HOLD_MS)
        || !tc358743_wr16(SYSCTL, 0x0000u)
        || !delay_ms(RESET_HOLD_MS))
    {
        printf("TC358743: software reset failed\r\n");
        return;
    }

    if (!tc358743_rd16(CHIPID, &chipid))
    {
        printf("TC358743: CHIPID read failed\r\n");
        return;
    }

    if ((chipid & 0xFF00u) != 0x0000u)
    {
        printf("TC358743: unexpected chipid=0x%04X\r\n", chipid);
        return;
    }

    printf("TC358743: present (chipid=0x%04X)\r\n", chipid);
    printf("TC358743: init (2 lanes, %u Mbps/lane, RGB888 full-range)\r\n",
           (unsigned)(CSI_BPS_PER_LANE / 1000000u));

    if (!tc358743_do_init())
    {
        printf("TC358743: init aborted\r\n");
        return;
    }

    if (!tc358743_rd8(SYS_STATUS, &sys_status))
    {
        printf("TC358743: init complete; SYS_STATUS read failed\r\n");
        return;
    }

    printf("TC358743: init complete; SYS_STATUS=0x%02X\r\n", sys_status);

    (void)delay_ms(EDID_POST_RISE_MS);

    if (tc358743_rd8(SYS_STATUS, &sys_status))
    {
        printf("TC358743: post-HPD SYS_STATUS=0x%02X\r\n", sys_status);
    }
}

static bool tc358743_enable_stream(bool enable)
{
    if (enable)
    {
        if (!tc358743_wr32(TXOPTIONCNTRL, 0u))                { return false; }
        if (!tc358743_wr32(TXOPTIONCNTRL, MASK_CONTCLKMODE))  { return false; }
        if (!tc358743_wr8(VI_MUTE, MASK_AUTO_MUTE))           { return false; }
    }
    else
    {
        if (!tc358743_wr8(VI_MUTE, MASK_AUTO_MUTE | MASK_VI_MUTE))
        {
            return false;
        }
    }
    return tc358743_wr16_and_or(
        CONFCTL,
        (uint16_t)~(MASK_VBUFEN | MASK_ABUFEN),
        enable ? (uint16_t)(MASK_VBUFEN | MASK_ABUFEN) : 0u);
}

static const char *color_space_name(uint8_t cs)
{
    switch (cs)
    {
    case 0:  return "RGB";
    case 1:  return "YCbCr 601";
    case 2:  return "opRGB";
    case 3:  return "YCbCr 709";
    case 5:  return "xvYCC 601";
    case 7:  return "xvYCC 709";
    case 9:  return "sYCC 601";
    case 13: return "opYCC 601";
    default: return "unknown";
    }
}

static bool read_detected_format(uint16_t *width, uint16_t *height)
{
    uint8_t de_w_lo, de_w_hi, de_v_lo, de_v_hi;
    uint8_t hsz_lo, hsz_hi, vsz_lo, vsz_hi;
    uint8_t fv_lo, fv_hi, vi1, vi3;

    if (!tc358743_rd8(DE_WIDTH_H_LO, &de_w_lo)) { return false; }
    if (!tc358743_rd8(DE_WIDTH_H_HI, &de_w_hi)) { return false; }
    if (!tc358743_rd8(DE_WIDTH_V_LO, &de_v_lo)) { return false; }
    if (!tc358743_rd8(DE_WIDTH_V_HI, &de_v_hi)) { return false; }
    if (!tc358743_rd8(H_SIZE_LO,     &hsz_lo))  { return false; }
    if (!tc358743_rd8(H_SIZE_HI,     &hsz_hi))  { return false; }
    if (!tc358743_rd8(V_SIZE_LO,     &vsz_lo))  { return false; }
    if (!tc358743_rd8(V_SIZE_HI,     &vsz_hi))  { return false; }
    if (!tc358743_rd8(FV_CNT_LO,     &fv_lo))   { return false; }
    if (!tc358743_rd8(FV_CNT_HI,     &fv_hi))   { return false; }
    if (!tc358743_rd8(VI_STATUS1,    &vi1))     { return false; }
    if (!tc358743_rd8(VI_STATUS3,    &vi3))     { return false; }

    uint16_t w    = (uint16_t)(((de_w_hi & 0x1Fu) << 8) | de_w_lo);
    uint16_t h    = (uint16_t)(((de_v_hi & 0x1Fu) << 8) | de_v_lo);
    uint16_t htot = (uint16_t)(((hsz_hi  & 0x1Fu) << 8) | hsz_lo);
    /* V_SIZE is in half-line units per the kernel driver (tc358743.c:370). */
    uint16_t vtot = (uint16_t)((((vsz_hi & 0x3Fu) << 8) | vsz_lo) / 2u);
    uint16_t fv   = (uint16_t)(((fv_hi   & 0x03u) << 8) | fv_lo);
    uint16_t fps  = (fv > 0u) ? (uint16_t)((10000u + fv / 2u) / fv) : 0u;
    uint8_t  cs   = (uint8_t)((vi3 & MASK_S_V_COLOR) >> 1);
    bool     il   = (vi1 & MASK_S_V_INTERLACE) != 0u;
    bool     lr   = (vi3 & MASK_LIMITED)       != 0u;

    printf("TC358743: detected %ux%u%c @ %u Hz, %s %s-range; "
           "raster %ux%u (incl blanking); VI_STATUS1=0x%02X VI_STATUS3=0x%02X\r\n",
           (unsigned)w, (unsigned)h, il ? 'i' : 'p',
           (unsigned)fps, color_space_name(cs),
           lr ? "limited" : "full",
           (unsigned)htot, (unsigned)vtot,
           vi1, vi3);

    /* Bridge CSI-TX + FIFO + HDMI-detect config snapshot — catches
     * bridge-side narrowing (FIFO underrun, HDMI_DET mode, CSI_ERR). */
    uint8_t  vi_mode = 0, hdmi_det = 0;
    uint16_t confctl = 0, fifoctl = 0;
    uint32_t csi_status = 0, csi_err = 0;
    (void)tc358743_rd8(VI_MODE,   &vi_mode);
    (void)tc358743_rd8(HDMI_DET,  &hdmi_det);
    (void)tc358743_rd16(CONFCTL,  &confctl);
    (void)tc358743_rd16(FIFOCTL,  &fifoctl);
    (void)tc358743_rd32(CSI_STATUS, &csi_status);
    (void)tc358743_rd32(CSI_ERR,    &csi_err);
    printf("TC358743:   CONFCTL=0x%04X FIFOCTL=0x%04X VI_MODE=0x%02X HDMI_DET=0x%02X\r\n",
           (unsigned)confctl, (unsigned)fifoctl,
           (unsigned)vi_mode, (unsigned)hdmi_det);
    printf("TC358743:   CSI_STATUS=0x%08lX CSI_ERR=0x%08lX\r\n",
           (unsigned long)csi_status, (unsigned long)csi_err);

    /* VOUT_SET2/SET3/VI_REP governs output format including pixel-repetition
     * de-replication. IN_REP bits in VI_REP low nibble + IN_REP_HEN (bit 4)
     * could be causing horizontal halving if misconfigured. */
    uint8_t vout_set2 = 0, vout_set3 = 0, vi_rep = 0;
    (void)tc358743_rd8(VOUT_SET2, &vout_set2);
    (void)tc358743_rd8(VOUT_SET3, &vout_set3);
    (void)tc358743_rd8(VI_REP,    &vi_rep);
    printf("TC358743:   VOUT_SET2=0x%02X VOUT_SET3=0x%02X VI_REP=0x%02X\r\n",
           (unsigned)vout_set2, (unsigned)vout_set3, (unsigned)vi_rep);

    s_detectedWidth  = w;
    s_detectedHeight = h;

    if (width  != NULL) { *width  = w; }
    if (height != NULL) { *height = h; }
    return true;
}

static void log_status_change(uint8_t prev, uint8_t cur)
{
    printf("TC358743: SYS_STATUS 0x%02X->0x%02X [%s%s%s%s%s%s]\r\n",
           prev, cur,
           (cur & MASK_S_DDC5V)    ? "DDC5V "  : "",
           (cur & MASK_S_TMDS)     ? "TMDS "   : "",
           (cur & MASK_S_PHY_PLL)  ? "PLL "    : "",
           (cur & MASK_S_PHY_SCDT) ? "SCDT "   : "",
           (cur & MASK_S_HDMI)     ? "HDMI "   : "",
           (cur & MASK_S_SYNC)     ? "SYNC"    : "");

    bool sync_now      = (cur  & MASK_S_SYNC) != 0u;
    bool sync_previous = (prev & MASK_S_SYNC) != 0u;
    if (sync_now && !sync_previous)
    {
        (void)read_detected_format(NULL, NULL);
    }
    else if (!sync_now && sync_previous)
    {
        s_detectedWidth  = 0u;
        s_detectedHeight = 0u;
    }
}

void TC358743_Tasks(void)
{
    static SYS_TIME_HANDLE pollHandle = SYS_TIME_HANDLE_INVALID;
    static uint8_t         lastStatus = 0u;
    static bool            firstPoll  = true;

    if (i2cHandle == DRV_HANDLE_INVALID) { return; }

    if (pollHandle != SYS_TIME_HANDLE_INVALID
        && !SYS_TIME_DelayIsComplete(pollHandle))
    {
        return;
    }

    uint8_t sys_status;
    if (tc358743_rd8(SYS_STATUS, &sys_status))
    {
        s_sysStatus = sys_status;

        if (firstPoll)
        {
            firstPoll = false;
            lastStatus = sys_status;
            printf("TC358743: watcher start; SYS_STATUS=0x%02X\r\n", sys_status);
            if (sys_status & MASK_S_SYNC)
            {
                (void)read_detected_format(NULL, NULL);
            }
        }
        else if (sys_status != lastStatus)
        {
            log_status_change(lastStatus, sys_status);
            lastStatus = sys_status;
        }
    }

    pollHandle = SYS_TIME_HANDLE_INVALID;
    (void)SYS_TIME_DelayMS(STATUS_POLL_MS, &pollHandle);
}

bool TC358743_IsLocked(void)
{
    return (s_sysStatus & MASK_S_SYNC) != 0u;
}

bool TC358743_GetDetectedFormat(uint16_t *width, uint16_t *height)
{
    if (s_detectedWidth == 0u || s_detectedHeight == 0u) { return false; }
    if (width  != NULL) { *width  = s_detectedWidth;  }
    if (height != NULL) { *height = s_detectedHeight; }
    return true;
}

bool TC358743_EnableStream(bool enable)
{
    return tc358743_enable_stream(enable);
}
