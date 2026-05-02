# marvin HDMI → BGRX32 Capture Pipeline

Authoritative configuration reference for the 720p60 HDMI capture path on SAM9X75. Every stage, every register bit that matters, with datasheet citations. When something breaks, start here before grepping code.

**Status:** working as of 2026-05-02. Validated at **1280×720p60** and **720×480p60**, native BGRX32 in DDR, no CPU post-processing. The pipeline is resolution-agnostic — PFE crop, DMA size, and framebuffer offsets all scale from the TC358743-detected `{width, height}` automatically.

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
  VPCFGR: DT=0x24, VC=0, PA=0, RMS=0
  → demux_data = 0x00_00RRGGBB, one pixel per VP word
        │
        ▼                          <── 40-bit vp_data bus
ISC — Parallel Front End (PFE)
  PFE_CFG0: MIPI=1, BPS=FORTY, MODE=PROGRESSIVE, CONT=1, COLEN=1, ROWEN=1
  PFE_CFG1: COLMIN=0, COLMAX=W-1   (1279)
  PFE_CFG2: ROWMIN=0, ROWMAX=H-1   (719)
        │
        ▼                          <── sub420_data[39:0] (unmodified through ISC)
ISC — Rounding/Limiting/Packing (RLP)
  RLP_CFG: MODE=BYPASS (15)
  → rlp_data[31:0] = sub420_data[31:0] = 0x00RRGGBB
        │
        ▼                          <── rlp_data[31:0]
ISC — DMA Host
  DCFG: IMODE=PACKED32, YMBSIZE=BEATS32, CMBSIZE=BEATS32
  → stores 32-bit words to DDR; little-endian = B G R 00 = BGRX32
        │
        ▼
DDR3 framebuffer  (1280 × 720 × 4 = 3,686,400 B/frame)
  B G R 00  B G R 00  B G R 00  ...
```

---

## 2. Stage-by-stage configuration

### 2.1 HDMI source

- 1280×720p @ 60 Hz, RGB (not YCbCr), progressive.
- Any stable DVI/HDMI source works. Raspberry Pi with `/boot/firmware/config.txt` forced to `hdmi_group=1 hdmi_mode=4 hdmi_force_hotplug=1 hdmi_drive=2` is the known-good reference.
- Pixel rate: 74.25 MHz (standard CEA-861 VIC 4 / 720p60).

Full-range vs limited-range is sender-controlled. TC358743's AVI InfoFrame carries whatever the source advertises; currently Pi sends limited-range, so peak values in DDR are `0xFE` not `0xFF`. EDID colorimetry overrides are a separate future task.

### 2.2 TC358743 HDMI-to-CSI-2 bridge

Driver: `firmware/marvin/default/src/tc358743.c`. Register values derived from mainline Linux `tc358743.c`.

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
// isc_capture.c:25
#define ISC_CAP_CSI_BITRATE  0x0Au
```

This is the `HSFREQRANGE` DWC D-PHY test-mode value. SAM9X75 is **DWC Gen3** (verified from Linux DT: `snps,dw-dphy-rx` with `snps,phy_type=<0>`). The Gen3 table entry for the 950–1000 Mbps band is `0x0A`.

- **Wrong** (used during earlier debug): `0x1A` — that's a Gen2 value. At 972 Mbps Gen2 bands diverge from Gen3, symptom is persistent `PHY_FATAL.SOT_SYNC_ERR = 0x3`.
- **Correct** for 972 Mbps Gen3: `0x0A`.
- Changes with bitrate. If bitrate moves, look up Gen3 table (DesignWare MIPI D-PHY RX Gen3 Databook), don't copy values from other SoC examples.

Other CSI settings:
- `CSI_NUM_LANES = 2` (matches TC358743).
- `csiObj->csiFps = 60u` (informational; drives some timing in the driver).

### 2.4 CSI2DC — the RMS=0 lever

**This is the key decision that enables in-pipeline BGRX32.** DS60001813 §49.6.54:

| VPCFGR Field | Value | Reason |
|---|---|---|
| DT (bits 5:0) | 0x24 | RGB888 (matches TC358743 emission) |
| VC (bits 7:6) | 0 | Virtual channel 0 |
| PA (bit 14) | **0** | LSB-aligned. PA=1 is for 10/12-bit Bayer sensors being packed onto ISC's 12-bit internal bus. Irrelevant for MIPI RGB888 bypass. |
| **RMS (bit 13)** | **0** | **Critical.** See below. |
| RGB36MAP (bit 15) | 0 | Use Table 49.25 pixel mapping |

`RMS` (Recommended Memory Storage) per datasheet:

- `RMS=0` — "CSI2DC outputs 1 pixel per component per clock cycle, compliant with the ISC processing engine." → Table 49.25: `demux_data[39:24]=0`, `[23:16]=R`, `[15:8]=G`, `[7:0]=B`. **One pixel per 40-bit VP word.**
- `RMS=1` — "CSI2DC generates a byte stream compliant with the CSI-2 specification memory format." → Table 49.27: 4 BGR pixels packed into 12 bytes. This is what MCC's default config produces — results in **dense 3 B/pixel BGR** (not what we want for direct display).

**MCC hardcodes RMS=1.** We override it:

```c
// firmware/marvin/default/src/config/default/vision/drivers/csi2dc/plib_csi2dc.c
void CSI2DC_Configure_VideoPipe(uint32_t dt, uint32_t vc, uint32_t align_isc) {
    CSI2DC_REGS->CSI2DC_VPCFGR = CSI2DC_VPCFGR_DT(dt)
                               | CSI2DC_VPCFGR_VC(vc)
                               | (align_isc ? CSI2DC_VPCFGR_PA_1 : 0);
    /* NOTE: do NOT OR in CSI2DC_VPCFGR_RMS_1 — we want RMS=0 so each pixel
     *       occupies a full 40-bit VP word with the low 32 bits = 0x00RRGGBB. */
}
```

Other CSI2DC config:
- `GCFGR.MIPIFRN = 0` (free-running, matches TC358743's continuous-clock mode). MCC default is gated (`MIPIFRN=1`); mismatch leaves `CSI2DC.GSR.ARSTIP` stuck on reset. Override via `csi2dcObj->enableMIPIFreeRun = true;` in `isc_capture.c:91`.
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

We program these directly after `DRV_ISC_Configure()`:

```c
// isc_capture.c:204-209
ISC_REGS->ISC_PFE_CFG1 = ISC_PFE_CFG1_COLMIN(0u)
                       | ISC_PFE_CFG1_COLMAX((uint32_t)width - 1u);   // 1279 at 720p
ISC_REGS->ISC_PFE_CFG2 = ISC_PFE_CFG2_ROWMIN(0u)
                       | ISC_PFE_CFG2_ROWMAX((uint32_t)height - 1u);  // 719  at 720p
ISC_REGS->ISC_PFE_CFG0 |= ISC_PFE_CFG0_COLEN_1 | ISC_PFE_CFG0_ROWEN_1;
```

With RMS=0, COLMAX is in **pixel units** (1 sample per pixel). Under the previous RMS=1 path, COLMAX was `(W × 3) / 4 - 1` because the ISC counted 32-bit byte-stream words.

### 2.6 ISC — Rounding, Limiting, Packing (RLP)

DS60001813 §50.6.19, §50.7.67.

| RLP_CFG field | Value | Reason |
|---|---|---|
| **MODE (3:0)** | **15 = BYPASS** | "32-bit input is sampled and written to the rlp output port. Select this mode for MIPI RMS mode." Passes `sub420_data[31:0]` through unchanged. With our RMS=0 input that's `0x00RRGGBB`. |
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

Consequence: we cannot get `alpha = 0xFF` in-pipeline for free. We get `alpha = 0x00` because CSI2DC zeros the upper 16 bits of each 40-bit VP word. If `0xFF` alpha matters, a one-shot CPU pass to set `buf[3::4] = 0xFF` does it cheaply, or stamp 0xFF into the framebuffer once at init (DMA writes never touch byte 3 if pixel stride matches — need to verify that assumption before relying on it).

**Driver hint:** `iscObj->rlpMode = ISC_RLP_CFG_MODE_BYPASS` (value 15 in the MCC enum). `drv_isc.c` writes this into RLP_CFG during `DRV_ISC_Configure`.

### 2.7 ISC — DMA Host

DS60001813 §50.6.20, §50.7.70.

| DCFG field | Value | Reason |
|---|---|---|
| **IMODE (2:0)** | **2 = PACKED32** | "32 bits, single channel packed." Stores each `rlp_data[31:0]` as 4 contiguous bytes in memory. Little-endian → `B G R 00`. |
| YMBSIZE (6:4) | 4 = BEATS32 | 32-beat AXI burst (sama7g5 / SAM9X7 — MCC's PACKED8 default is BEATS8 which is the sama5d2 value, undersized for this SoC). |
| CMBSIZE (10:8) | 4 = BEATS32 | Same rationale as YMBSIZE. |
| ARQOS / AWQOS | 0 | Dynamic QoS based on FIFO level (recommended default) |

MCC's `DRV_ISC_Configure` sets PACKED8 + BEATS8 if `iscObj->layout = ISC_LAYOUT_PACKED8`. We override after `DRV_ISC_Configure`:

```c
// isc_capture.c:219-221
ISC_REGS->ISC_DCFG = ISC_DCFG_IMODE_PACKED32
                   | ISC_DCFG_YMBSIZE_BEATS32
                   | ISC_DCFG_CMBSIZE_BEATS32;
```

The write is safe because nothing in MCC writes DCFG again after this point (the redundant-call to `DRV_ISC_Configure_DMA` was removed). `DRV_ISC_Configure` already calls it internally at `drv_isc.c:381` — don't add a second call.

**Descriptor view:** `DCTRL.DVIEW = 0` (PACKED / single channel). `DAD0` holds the current buffer base address. `DST0` = 0 (contiguous frame, no row stride gap).

### 2.8 Framebuffer layout in DDR

```c
// isc_capture.c:17-29
#define ISC_CAP_MAX_W        1920u
#define ISC_CAP_MAX_H        1080u
#define ISC_CAP_BPP          4u      /* BGRX32 */
#define ISC_CAP_NUM_BUFFERS  2u

static __attribute__((__section__(".region_cache_aligned")))
       __attribute__((__aligned__(32)))
       uint8_t g_framebuffer[ISC_CAP_MAX_W * ISC_CAP_MAX_H * ISC_CAP_BPP * ISC_CAP_NUM_BUFFERS];
```

- Pool size: 1920×1080×4×2 = 16.6 MB, in the cacheable DDR region.
- 32-byte aligned (cache line boundary for ARM926 L1).
- At 720p: only 3,686,400 B × 2 = 7.37 MB actually used.
- Double-buffered for tear-free consumption by the display stage (ISR toggles `frameIndex`; probe reads the *previous* completed buffer).

Pixel access pattern (0-indexed, little-endian):

```c
uint8_t *px = &g_framebuffer[(y * W + x) * 4];
uint8_t B = px[0];
uint8_t G = px[1];
uint8_t R = px[2];
// px[3] = 0x00 (X / unused)
```

As a 32-bit word: `*(uint32_t*)px == 0x00RRGGBB` (native little-endian).

---

## 3. Verified output — color sweep

Pi source, 720p60 RGB limited-range (so peak = 0xFE). Probe reports memory at representative points across the frame:

| Source color | Memory (`b0 b1 b2 b3`) | Decode |
|---|---|---|
| Red   | `00 00 FE 00` | B=0, G=0, R=0xFE, X=0 |
| Green | `00 FE 00 00` | B=0, G=0xFE, R=0, X=0 |
| Blue  | `FE 00 00 00` | B=0xFE, G=0, R=0, X=0 |
| White | `FE FE FE 00` | B=G=R=0xFE, X=0 |
| Black | `00 00 00 00` | — |

All five match expectation across the full 1280×720 extent (rows 0..719 completely written, no cliff). Framerate sustained at 70 fps probe-window average (≈60 fps real + diagnostic ticks).

---

## 4. Where each override lives in code

| Stage | File | Line | Change |
|---|---|---|---|
| TC358743 D-PHY timing | `default/src/tc358743.c` | 25–33 | 972 Mbps timing constants (kernel-derived) |
| TC358743 PLL | `default/src/tc358743.c` | 14–20 | `PLL_PRD=4`, `PLL_FBD=144` |
| CSI HSFREQRANGE | `default/src/isc_capture.c` | 25 | `0x0A` for DWC Gen3 @ 972 Mbps |
| CSI bitrate override | `default/src/isc_capture.c` | 86 | `csiObj->csiBitRate = ISC_CAP_CSI_BITRATE` (MCC default is `0x16`) |
| CSI2DC RMS=0 | `default/src/config/default/vision/drivers/csi2dc/plib_csi2dc.c` | 71–78 | dropped `CSI2DC_VPCFGR_RMS_1` from VPCFGR write |
| CSI2DC free-run | `default/src/isc_capture.c` | 91 | `enableMIPIFreeRun = true` |
| ISC PFE crop | `default/src/isc_capture.c` | 204–209 | direct `PFE_CFG1/2` + `COLEN`/`ROWEN` writes |
| ISC DMA config | `default/src/isc_capture.c` | 219–221 | direct `DCFG` write (PACKED32 + BEATS32×2) |
| Framebuffer BPP | `default/src/isc_capture.c` | 19 | `ISC_CAP_BPP = 4` |

---

## 5. Debug / diagnostics

`ISC_Capture_ProbeFrame()` in `isc_capture.c` dumps once per second:

1. **`cliff:` block** — PFE/DMA register snapshot, INTSR decode with named bits (VD/HD/DDONE/LDONE/HDTO/VDTO/DAOV/BUSERR), per-probe frame-counter deltas.
2. **`IDS[i]:`** — CSI2DC Image Data Snoop per-packet metadata: DT / VC / word-count (bytes per MIPI long packet) / accumulated row count since last snoop reset. Authoritative per-packet evidence of what CSI-2 sent us. `WC=3840` at 720p = 1280 px × 3 MIPI bytes/pixel = correct.
3. **9-point mem dump** — 4-byte samples at `{TL,TM,TR,ML,CT,MR,BL,BM,BR}` of the frame. First diagnostic to check when reasoning about pixel format.
4. **Hex rows** — 48 bytes × 5 row locations for visual inspection of packing.
5. **Byte-range scan** — min/max of each byte position across row 30. Useful for detecting whether data is present at all.
6. **Vertical extent** — first unwritten row (sentinel 0x55 = DMA never touched). Full frame expected = `rows 0..719 written, first sentinel at row 720 (of 720)`.
7. **Phase scan** — reports the mod-BPP byte-lane offset of the first saturated byte per row. Useful legacy diagnostic from the RMS=1 phase-drift days; with BGRX32 it's pretty much constant per solid color.

When something's wrong, read in this order:
- `INTSR` error bits (HDTO/VDTO/DAOV/BUSERR) point at the PFE, PFE, DMA, or bus respectively.
- `IDS[0]` WC and rows tell you if CSI-2 payload is correct at the wire level.
- 9-point dump + row-30 range localize format/byte-order issues.
- `vertical extent` distinguishes a cliff from a format issue.

---

## 6. Known carry-forwards

- **`INTSR.HDTO` always set** after capture start — informational-only residual, not a stop signal. Also present under working RMS=1 config. Safe to ignore.
- **`frameIndex` stuck at 0** in most probe windows — DDONE interrupt fires rarely or not at all, but the DMA itself works (buffers update, `frameCount` counts VDs at correct rate). Investigate before relying on DDONE-based downstream triggers.
- **Pixel range 0x00–0xFE** because TC358743 passes through source's limited-range signaling. EDID overrides or TC358743 colorimetry registers can force full-range; deferred until display stage forces the issue.
- **Alpha channel = 0x00** — not 0xFF. Acceptable for display framebuffers that ignore alpha; requires CPU fix-up if alpha is semantic.

---

## 7. Datasheet-anchored reasoning for ARGB/RGBA aspirations

If someone revisits this and asks "can we get ARGB32 directly in-pipeline?":

1. **ARGB32 RLP mode** requires upstream data at `sub420_data[29:22]/[19:12]/[9:2]`. That's the CSC output format (§50.6.15). CSC is a module for converting to/from YCbCr — it's not a pass-through. Routing RGB888 through CSC would work only if CSC has an identity-RGB mode, which per §50.6.15 it doesn't (the CSC input is YCbCr by design).
2. **Demosaic pipeline** (WB → CFA → CC → GAM → …) expects 10/12-bit Bayer mosaic input, not 24-bit RGB.
3. **CSI2DC has no alpha-insertion register** — only RMS mode and the RGB36MAP packing variant. Neither affects the upper byte.

Therefore the cleanest path to ARGB32 is:
- **Software post-pass:** single `memset` of byte 3 at framebuffer init (if DMA stride matches) or a per-frame byte-smearing pass (expensive).
- **GFX2D blit:** use the 2D graphics engine to copy BGRX32 → ARGB32 with alpha fill. Zero CPU cost if the engine supports it.
- **Display IP native BGRX support:** SAM9X75 LCDC supports multiple BGRX/ARGB layouts — pick one that doesn't care about alpha. Probably the right answer.
