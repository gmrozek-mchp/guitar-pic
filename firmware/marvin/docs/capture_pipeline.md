# marvin HDMI → BGR888 Capture Pipeline

Authoritative configuration reference for the 720p60 HDMI capture path on SAM9X75. Every stage, every register bit that matters, with datasheet citations. When something breaks, start here before grepping code.

**Status:** working as of 2026-05-02. Validated at **1280×720p60** and **720×480p60**, native BGR888 packed (3 B/pixel) in DDR, no CPU post-processing. The pipeline is resolution-agnostic — PFE crop, DMA size, and framebuffer offsets all scale from the TC358743-detected `{width, height}` automatically.

Datasheet references are to **SAM9X7 Series DS60001813** (sections 48 = MIPI CSI / D-PHY, 49 = CSI2DC, 50 = ISC). TC358743 refs are to the Toshiba datasheet + mainline Linux `drivers/media/i2c/tc358743.c`.

---

## 1. Pipeline overview

```
HDMI source (Pi 720p60 RGB)
        │
        ▼
TC358743 HDMI-to-CSI-2 bridge
  DT=0x24 RGB888, 2 lanes, 972 Mbps/lane, continuous-clock mode
        │
        ▼                          <── MIPI CSI-2 D-PHY
SAM9X75 DWC D-PHY RX Gen3
  HSFREQRANGE=0x0A (950–1000 Mbps band)
        │
        ▼                          <── 40-bit isc_data bus
CSI2DC (Demultiplexer Controller)
  VPCFGR: DT=0x24, VC=0, PA=0, RMS=1  (byte-stream mode)
  → 4 BGR pixels packed into 12 bytes per CSI-2 RMS spec (Table 49.27)
        │
        ▼                          <── byte-stream on vp_data bus
ISC — Parallel Front End (PFE)
  PFE_CFG0: MIPI=1, BPS=FORTY, MODE=PROGRESSIVE, CONT=1, COLEN=1, ROWEN=1
  PFE_CFG1: COLMIN=0, COLMAX=(W×3/4)-1   (e.g. 959 at 1280px)
  PFE_CFG2: ROWMIN=0, ROWMAX=H-1   (719)
        │
        ▼                          <── sub420_data[39:0]
ISC — Rounding/Limiting/Packing (RLP)
  RLP_CFG: MODE=BYPASS (15)
  → rlp_data[31:0] = sub420_data[31:0] (pass-through)
        │
        ▼                          <── rlp_data[31:0]
ISC — DMA Host
  DCFG: IMODE=PACKED32, YMBSIZE=BEATS32, CMBSIZE=BEATS32
  → stores 32-bit words to DDR; RMS=1 byte-stream → dense BGR888
        │
        ▼
DDR3 framebuffer  (1280 × 720 × 3 = 2,764,800 B/frame)
  B G R  B G R  B G R  ...  (dense, no padding byte)
```

---

## 2. Stage-by-stage configuration

### 2.1 HDMI source

- 1280×720p @ 60 Hz, RGB (not YCbCr), progressive.
- Any stable DVI/HDMI source works. Raspberry Pi with `/boot/firmware/config.txt` forced to `hdmi_group=1 hdmi_mode=4 hdmi_force_hotplug=1 hdmi_drive=2` is the known-good reference.
- Pixel rate: 74.25 MHz (standard CEA-861 VIC 4 / 720p60).

Full-range vs limited-range is sender-controlled. TC358743's AVI InfoFrame carries whatever the source advertises; currently Pi sends limited-range, so peak values in DDR are `0xFE` not `0xFF`. EDID colorimetry overrides are a separate future task.

### 2.2 TC358743 HDMI-to-CSI-2 bridge

Driver: `firmware/marvin/default/src/video/tc358743.c`. Register values derived from mainline Linux `tc358743.c`.

| Register | Value | Meaning |
|---|---|---|
| `PLLCTL` PRD=4, FBD=144 | PLL out = 972 MHz | `hsck = (27 MHz / PRD) × FBD` |
| `CSI_CONFW` lanes | 2 | 2 data lanes × 972 Mbps = 1.944 Gbps total (plenty for 720p60 RGB888's ~1.33 Gbps) |
| `CONFCTL` | 0x0007 | PCLK divider off, 3-byte pixel pack, no audio |
| `FIFOCTL` | 0x0176 | FIFO level 374 (kernel hardcodes at all rates) |
| `VI_MODE` | 0xE6 | RGB888 input, 16-bit HDMI RX side |
| `VI_REP`  | 0x00 | **No pixel replication / doubling** (critical — was 0x80 in earlier 480p test and caused 16-row phase drift) |
| `VOUT_SET2` / `VOUT_SET3` | 0x01 / 0x08 | Output color format / range handling |
| CSI-2 DT | 0x24 | RGB888 per CSI-2 spec |

D-PHY timing (for 972 Mbps):
```
LINEINITCNT  = 0x1B58
LPTXTIMECNT  = 0x0007
TCLK_HEADER  = 0x2806    TCLK_TRAIL  = 0x0000    TCLK_POST = 0x0008
THS_HEADER   = 0x0806    THS_TRAIL   = 0x0005
TWAKEUP      = 0x4268    HSTXVREGCNT = 0x0000
```

Operating mode: **continuous clock** (`TXOPTIONCNTRL = CONTCLKMODE`). This is the required-match with the SAM9X75 RX side — if RX expects gated but TX sends continuous (or vice versa), `CSI2DC.GSR.ARSTIP` hangs permanently.

### 2.3 SAM9X75 MIPI CSI-2 D-PHY RX

Driver: `firmware/marvin/default/src/config/default/vision/drivers/csi/` (MCC-generated, slightly tweaked).

Key setting — **the one parameter that took us the longest to get right**:

```c
// video/isc_capture.c:29
#define ISC_CAP_CSI_BITRATE  0x0Au
```

This is the `HSFREQRANGE` DWC D-PHY test-mode value. SAM9X75 is **DWC Gen3** (verified from Linux DT: `snps,dw-dphy-rx` with `snps,phy_type=<0>`). The Gen3 table entry for the 950–1000 Mbps band is `0x0A`.

- **Wrong** (used during earlier debug): `0x1A` — that's a Gen2 value. At 972 Mbps Gen2 bands diverge from Gen3, symptom is persistent `PHY_FATAL.SOT_SYNC_ERR = 0x3`.
- **Correct** for 972 Mbps Gen3: `0x0A`.
- Changes with bitrate. If bitrate moves, look up Gen3 table (DesignWare MIPI D-PHY RX Gen3 Databook), don't copy values from other SoC examples.

Other CSI settings:
- `CSI_NUM_LANES = 2` (matches TC358743).
- `csiObj->csiFps = 60u` (informational; drives some timing in the driver).

### 2.4 CSI2DC — the RMS=1 lever

**This is the key decision that enables in-pipeline BGR888 packed.** DS60001813 §49.6.54:

| VPCFGR Field | Value | Reason |
|---|---|---|
| DT (bits 5:0) | 0x24 | RGB888 (matches TC358743 emission) |
| VC (bits 7:6) | 0 | Virtual channel 0 |
| PA (bit 14) | **0** | LSB-aligned. PA=1 is for 10/12-bit Bayer sensors being packed onto ISC's 12-bit internal bus. Irrelevant for MIPI RGB888 bypass. |
| **RMS (bit 13)** | **1** | **Critical.** See below. |
| RGB36MAP (bit 15) | 0 | Use Table 49.27 pixel mapping |

`RMS` (Recommended Memory Storage) per datasheet:

- `RMS=0` — "CSI2DC outputs 1 pixel per component per clock cycle, compliant with the ISC processing engine." → Table 49.25: `demux_data[39:24]=0`, `[23:16]=R`, `[15:8]=G`, `[7:0]=B`. One pixel per 40-bit VP word — gives BGRX32 (4 B/pixel, 25% extra DDR bandwidth, X byte always 0x00).
- `RMS=1` — "CSI2DC generates a byte stream compliant with the CSI-2 specification memory format." → Table 49.27: 4 BGR pixels packed into 12 bytes. Results in **dense 3 B/pixel BGR888** — 25% less DDR bandwidth, and XLCDC HEO reads natively in `RGB_888_PACKED` mode.

**MCC's default omits RMS=1.** We add it (re-apply patch #3):

```c
// firmware/marvin/default/src/config/default/vision/drivers/csi2dc/plib_csi2dc.c
void CSI2DC_Configure_VideoPipe(uint32_t dt, uint32_t vc, uint32_t align_isc) {
    CSI2DC_REGS->CSI2DC_VPCFGR = CSI2DC_VPCFGR_DT(dt)
                               | CSI2DC_VPCFGR_VC(vc)
                               | (align_isc ? CSI2DC_VPCFGR_PA_1 : 0)
                               | CSI2DC_VPCFGR_RMS_1;  /* PATCH #3: byte-stream mode */
}
```

Other CSI2DC config:
- `GCFGR.MIPIFRN = 0` (free-running, matches TC358743's continuous-clock mode). MCC default is gated (`MIPIFRN=1`); mismatch leaves `CSI2DC.GSR.ARSTIP` stuck on reset. Override via `csi2dcObj->enableMIPIFreeRun = true;` in `video/isc_capture.c`.
- `VPER = 1` (video pipe enabled).

### 2.5 ISC — Parallel Front End (PFE)

DS60001813 §50.6.6, §50.7.4–50.7.6.

Key settings configured by `DRV_ISC_Configure()`:

| PFE_CFG0 field | Value | Why |
|---|---|---|
| REP | 0 | N/A for BPS=FORTY |
| BPS (30:28) | 5 = FORTY | "40-bit input (used for MIPI formats up to forty bits per pixel)." Matches CSI2DC's 40-bit VP bus. |
| MIPI (bit 14) | 1 | Input from MIPI interface |
| GATED (bit 8) | 0 | Free-running clock |
| CCIR656 (bit 9) | 0 | Not embedded-sync — using MIPI |
| CONT (bit 7) | 1 | Continuous acquisition (vs single shot) |
| MODE (6:4) | 0 = PROGRESSIVE | Source is progressive |
| VPOL / HPOL | 0 | Active-high sync (standard) |

**Two bits that MCC leaves OFF but we must set:**

| PFE_CFG0 field | Value | Why MCC misses it |
|---|---|---|
| **COLEN (bit 12)** | **1** | Enables the column crop window |
| **ROWEN (bit 13)** | **1** | Enables the row crop window |

MCC's `ISC_PFE_Crop_Area()` function is defined in `plib_isc.c` but **never called**. Without COLEN+ROWEN and valid COLMAX/ROWMAX, the PFE expects a 1×1 frame and fires HDTO (Horizontal Detection Timeout) on the first real line. The Linux `mchp-isc` driver sets both bits; we match that.

We program these directly after `DRV_ISC_Configure()`. With RMS=1, COLMAX is in ISC sample units (32-bit byte-stream words), so the formula is `(width × 3 / 4) - 1`:

```c
// video/isc_capture.c
ISC_REGS->ISC_PFE_CFG1 = ISC_PFE_CFG1_COLMIN(0u)
                       | ISC_PFE_CFG1_COLMAX((width * 3u / 4u) - 1u);  // 959 at 1280px
ISC_REGS->ISC_PFE_CFG2 = ISC_PFE_CFG2_ROWMIN(0u)
                       | ISC_PFE_CFG2_ROWMAX((uint32_t)height - 1u);   // 719 at 720p
ISC_REGS->ISC_PFE_CFG0 |= ISC_PFE_CFG0_COLEN_1 | ISC_PFE_CFG0_ROWEN_1;
```

With RMS=1, COLMAX is in **byte-stream word units** (4 bytes per ISC sample), so COLMAX = `(W × 3) / 4 - 1`. Under the previous RMS=0 path, COLMAX was in pixel units (`W - 1`).

### 2.6 ISC — Rounding, Limiting, Packing (RLP)

DS60001813 §50.6.19, §50.7.67.

| RLP_CFG field | Value | Reason |
|---|---|---|
| **MODE (3:0)** | **15 = BYPASS** | "32-bit input is sampled and written to the rlp output port. Select this mode for MIPI RMS mode." Passes `sub420_data[31:0]` through unchanged. With our RMS=1 byte-stream input each 32-bit word carries 4 dense BGR bytes. |
| ALPHA (15:8) | — | Ignored in BYPASS mode (only used by ARGB444/ARGB555/ARGB32 modes) |
| YMODE / LSH / REP | 0 | Not applicable to BYPASS |

**Why not ARGB32 RLP (Mode 10)?** Per §50.6.19 Table row for RGB32/ARGB32:
```
rlp_data[31:24] = A = ALPHA[7:0]       (register)
rlp_data[23:16] = R = sub420_data[29:22]
rlp_data[15:8]  = G = sub420_data[19:12]
rlp_data[7:0]   = B = sub420_data[9:2]
```

These bit positions are the output format of the ISC **CSC module** (Color Space Conversion, §50.6.15), not raw MIPI RGB888. On our bypass path, R/G/B sit at `[23:16]/[15:8]/[7:0]` — a 6/4/2-bit shift off what ARGB32 RLP wants. Attempting it yields mangled 3-shifted-copies-of-garbage per 12 bytes. **ARGB32 RLP is structurally unusable for MIPI RGB888 bypass;** it would require enabling the full demosaic pipeline (for Bayer sources) or CSC (for YCbCr).

Consequence: under RMS=1 the framebuffer is dense BGR888 (3 B/pixel) with **no alpha byte at all** — there is nothing to fill. A 4-byte ARGB layout is only relevant to the RMS=0 pixel-per-word path (see §7 for why that path is structurally unsuited to in-pipeline ARGB32).

**Driver hint:** `iscObj->rlpMode = ISC_RLP_CFG_MODE_BYPASS` (value 15 in the MCC enum). `drv_isc.c` writes this into RLP_CFG during `DRV_ISC_Configure`.

### 2.7 ISC — DMA Host

DS60001813 §50.6.20, §50.7.70.

| DCFG field | Value | Reason |
|---|---|---|
| **IMODE (2:0)** | **2 = PACKED32** | "32 bits, single channel packed." Stores each `rlp_data[31:0]` as 4 contiguous bytes in memory. With RMS=1 byte-stream, this writes dense BGR888 — every 3rd byte is the start of the next pixel, no padding. |
| YMBSIZE (6:4) | 4 = BEATS32 | 32-beat AXI burst (sama7g5 / SAM9X7 — MCC's PACKED8 default is BEATS8 which is the sama5d2 value, undersized for this SoC). |
| CMBSIZE (10:8) | 4 = BEATS32 | Same rationale as YMBSIZE. |
| ARQOS / AWQOS | 0 | Dynamic QoS based on FIFO level (recommended default) |

MCC's `DRV_ISC_Configure` sets PACKED8 + BEATS8 if `iscObj->layout = ISC_LAYOUT_PACKED8`. We override after `DRV_ISC_Configure`:

```c
// video/isc_capture.c:263-265
ISC_REGS->ISC_DCFG = ISC_DCFG_IMODE_PACKED32
                   | ISC_DCFG_YMBSIZE_BEATS32
                   | ISC_DCFG_CMBSIZE_BEATS32;
```

The write is safe because nothing in MCC writes DCFG again after this point (the redundant-call to `DRV_ISC_Configure_DMA` was removed). `DRV_ISC_Configure` already calls it internally at `drv_isc.c:381` — don't add a second call.

**Descriptor view:** `DCTRL.DVIEW = 0` (PACKED / single channel). `DAD0` holds the current buffer base address. `DST0` = 0 (contiguous frame, no row stride gap).

### 2.8 Framebuffer layout in DDR

```c
// video/isc_capture.c
#define ISC_CAP_MAX_W        1280u
#define ISC_CAP_MAX_H        720u
#define ISC_CAP_BPP          3u      /* BGR888 packed */
#define ISC_CAP_NUM_BUFFERS  4u

static __attribute__((__section__(".region_nocache")))
       __attribute__((__aligned__(32)))
       uint8_t g_framebuffer[ISC_CAP_MAX_W * ISC_CAP_MAX_H * ISC_CAP_BPP * ISC_CAP_NUM_BUFFERS];
```

- Pool size: 1280×720×3×4 = ~11 MB, in the **uncached** DDR region (`.region_nocache`).
- Uncached so CPU vision consumers (cv_marvin_v1) see DMA-fresh bytes without D-cache invalidation.
- 32-byte aligned (cache line boundary).
- At 480p: only 720×480×3×4 = ~4.1 MB actually used.
- 4-deep ring gives slow consumers up to ~50 ms read window before lapping.

Pixel access pattern (0-indexed). The buffer is dense BGR888, 3 B/pixel, so the row stride is `width × 3` (`video.c` uses `stride = w × VIDEO_BYTES_PER_PIXEL`, `VIDEO_BYTES_PER_PIXEL = 3`):

```c
uint8_t *px = &g_framebuffer[y * (W * 3) + x * 3];
uint8_t B = px[0];
uint8_t G = px[1];
uint8_t R = px[2];
// no 4th byte — the next pixel starts at px[3]
```

There is no padding/alpha byte: pixel *n+1* begins immediately after pixel *n*'s R.

### Sizing strategy

The buffer pool is statically allocated for the **maximum supported resolution** (`ISC_CAP_MAX_W × ISC_CAP_MAX_H × ISC_CAP_BPP × ISC_CAP_NUM_BUFFERS` = 1280 × 720 × 3 × 4 ≈ 11 MB), reserved once at link time in `.region_nocache`. Per-capture the ISC DMA descriptor is programmed for only `width × height × 3` bytes; the rest of the pool sits idle.

| Resolution | In use / frame | 4-buffer total | Idle |
|---|---|---|---|
| 480p60 (720×480) | 1.04 MB | 4.15 MB | ~6.9 MB |
| 720p60 (1280×720) | 2.76 MB | 11.06 MB | 0 |

Trade-off: idle DDR at low resolutions (trivial on this platform) in exchange for zero reallocation on source-resolution change — buffer base pointers stay constant, the consumer never needs to re-bind. `ISC_Capture_Configure` rejects any `width > MAX_W || height > MAX_H` so the guarantee is enforced.

---

## 3. Verified output — color sweep

Pi source, 720p60 RGB limited-range (so peak = 0xFE). Probe reports memory at representative points across the frame:

| Source color | Memory (`b0 b1 b2`) | Decode |
|---|---|---|
| Red   | `00 00 FE` | B=0, G=0, R=0xFE |
| Green | `00 FE 00` | B=0, G=0xFE, R=0 |
| Blue  | `FE 00 00` | B=0xFE, G=0, R=0 |
| White | `FE FE FE` | B=G=R=0xFE |
| Black | `00 00 00` | — |

All five match expectation across the full 1280×720 extent (rows 0..719 completely written, no cliff). Framerate sustained at 70 fps probe-window average (≈60 fps real + diagnostic ticks).

---

## 4. Where each override lives in code

| Stage | File | Line | Change |
|---|---|---|---|
| TC358743 D-PHY timing | `default/src/video/tc358743.c` | 27–39 | 972 Mbps timing constants (kernel-derived) |
| TC358743 PLL | `default/src/video/tc358743.c` | 14–22 | `PLL_PRD=4`, `PLL_FBD=144` |
| CSI HSFREQRANGE | `default/src/video/isc_capture.c` | 29 | `0x0A` for DWC Gen3 @ 972 Mbps |
| CSI bitrate override | `default/src/video/isc_capture.c` | 115 | `csiObj->csiBitRate = ISC_CAP_CSI_BITRATE` (MCC default is `0x16`) |
| CSI2DC RMS=1 | `default/src/config/default/vision/drivers/csi2dc/plib_csi2dc.c` | 78–81 | added `CSI2DC_VPCFGR_RMS_1` to VPCFGR write (byte-stream mode) |
| CSI2DC free-run | `default/src/video/isc_capture.c` | 120 | `enableMIPIFreeRun = true` |
| ISC PFE crop | `default/src/video/isc_capture.c` | 249–253 | direct `PFE_CFG1/2` + `COLEN`/`ROWEN` writes |
| ISC DMA config | `default/src/video/isc_capture.c` | 263–265 | direct `DCFG` write (PACKED32 + BEATS32×2) |
| Framebuffer BPP | `default/src/video/isc_capture.c` | 21 | `ISC_CAP_BPP = 3` (dense BGR888) |

---

## 5. Debug / diagnostics

Runtime log is intentionally quiet — initialization, format detection, and a one-shot pre-start register snapshot only. No periodic dump.

What lands on the DBGU during normal operation:

1. **TC358743 init + lock sequence** — I2C presence, init, SYS_STATUS transitions as HDMI negotiates, final `detected WxH @ FPS, RGB ...` with raster dimensions.
2. **`ISC_Capture: configured WxH BGR888 packed (N bytes/frame)`** — printed by `ISC_Capture_Configure` when TC358743 reports a locked format.
3. **`ISC_Capture diag (pre-start):` block** — one-shot register snapshot printed from `diag_dump_rx()` right before `DRV_ISC_Start_Capture`. Contains CSI / CSI2DC / ISC state at the moment capture is armed. When diagnosing new sources or regressions, this is the first place to look.
4. **`ISC_Capture: capture started`** — green light. Quiet from here.
5. **TC358743 format-change events** — if the source switches resolution mid-run, the watcher prints a new detection line and the capture module re-configures.

If deeper inspection is needed (byte-level probes, phase scanners, content bounding box), resurrect `ISC_Capture_ProbeFrame()` from git history (commit `29a21bf` has the last full version) and wire it into a temporary 1-Hz tick.

---

## 6. Known carry-forwards

- **`INTSR.HDTO` always set** after capture start — informational-only residual, not a stop signal. Also present under working RMS=1 config. Safe to ignore.
- **DDONE interrupt unreliable** — `iscObj->frameIndex` advances rarely or not at all; `g_frame_count` (incremented from the frame-done callback, driven by VD) counts correctly. DMA itself works (buffers update on schedule). Investigate before building consumer logic that relies on DDONE firing per frame.
- **Pixel range 0x00–0xFE** because TC358743 passes through source's limited-range signaling. EDID overrides or TC358743 colorimetry registers can force full-range; the display stage expands levels via the HEO gamma CLUT (see `ui_compositor.md` §15.1).
- **No alpha byte.** The RMS=1 byte-stream is dense BGR888 (3 B/pixel); there is no 4th/alpha byte in the framebuffer. Consumers that need per-pixel alpha must convert to a 4-byte layout.

---

## 7. Datasheet-anchored reasoning for ARGB/RGBA aspirations

If someone revisits this and asks "can we get ARGB32 directly in-pipeline?":

1. **ARGB32 RLP mode** requires upstream data at `sub420_data[29:22]/[19:12]/[9:2]`. That's the CSC output format (§50.6.15). CSC is a module for converting to/from YCbCr — it's not a pass-through. Routing RGB888 through CSC would work only if CSC has an identity-RGB mode, which per §50.6.15 it doesn't (the CSC input is YCbCr by design).
2. **Demosaic pipeline** (WB → CFA → CC → GAM → …) expects 10/12-bit Bayer mosaic input, not 24-bit RGB.
3. **CSI2DC has no alpha-insertion register** — only RMS mode and the RGB36MAP packing variant. Neither affects the upper byte.

Therefore the cleanest path to ARGB32 is:
- **Software post-pass:** single `memset` of byte 3 at framebuffer init (if DMA stride matches) or a per-frame byte-smearing pass (expensive).
- **GFX2D blit:** use the 2D graphics engine to copy BGR888 → ARGB32 with alpha fill. Zero CPU cost if the engine supports it.
- **Display IP native BGRX support:** SAM9X75 LCDC supports multiple BGRX/ARGB layouts — pick one that doesn't care about alpha. Probably the right answer.
