# marvin BGR888 → LCD Display Path

How the captured video (documented in [`capture_pipeline.md`](capture_pipeline.md)) lands on the 10.1" 1280×800 LVDS panel. Implementation: BGR888 packed from DDR → SAM9X75 XLCDC HEO layer → LVDSC → panel. Zero CPU, zero scaling, pillarboxed/letterboxed, per-frame pointer swap on ISC frame-done ISR.

**Status:** working. Pi 480p (720×480) and Wii/ElectronWarp 480p (720×480) display centered on the 1280×800 panel with 280 px black pillarbox each side and 160 px black letterbox top + bottom. Pi 720p (1280×720) display works too — full panel width, 40 px letterbox top + bottom.

---

## 1. Path overview

```
DDR framebuffer (g_framebuffer in isc_capture.c)
  BGR888 packed, 32-byte aligned, in .region_nocache
        │                     <── XLCDC DMA reads
        ▼
XLCDC HEO layer (High-End Overlay, with hardware scaler)
  RGB_888_PACKED color mode (memory order B, G, R per pixel)
  src_w × src_h window, centered on the 1280×800 panel via lcd_bind()
  alpha=255 global; pointer re-set to just-completed buffer on every ISC ISR
        │
        ▼
XLCDC timing engine  (1280×800 @ 60 Hz, 10.1" panel timings from MCC)
        │
        ▼
LVDSC  (LVDS serializer)
        │
        ▼
10.1" 1280×800 LVDS panel
```

---

## 2. Key decisions, with rationale

### 2.1 HEO, not OVR1 or BASE

The SAM9X75 XLCDC BASE layer has **no window position or size registers** — its window is always the full configured display size. Pointing BASE at a 720-wide framebuffer while the display is 800 wide produces an 80 px/row skew.

Overlays (OVR1, OVR2, HEO) have position/size registers so their window can be smaller than the panel and positioned anywhere. **We use HEO** (High-End Overlay) because:
- It has a built-in hardware scaler (`HEOCFG3`/`HEOCFG4` separate display vs. memory rect) — used when we later need to scale a source smaller than 720p up to fit the panel.
- It natively supports `RGB_888_PACKED` color mode, which matches our BGR888 capture format exactly.
- OVR1 lacks a scaler; adding one later would require migrating the bind call anyway.

### 2.2 `XLCDC_RGB_COLOR_MODE_RGB_888_PACKED`

Our capture format is BGR888 packed — memory byte order per pixel is `B, G, R` (low address to high), dense with no padding byte.

The XLCDC `RGB_888_PACKED` mode reads memory in this byte order (per SAM9X75 LCDC datasheet Table 44.26). Setting this mode on the HEO layer lets LCDC DMA read the capture buffer directly with zero conversion.

### 2.3 Global alpha via `XLCDC_SetLayerOpts(layer, 255, true, false)`

HEO's layer opts take a global alpha (0–255) and an `enable_dma` flag. We pass `alpha = 255` + `enable_dma = true`:
- `enable_dma = true` → pixel data comes from the framebuffer via DMA.
- `alpha = 255` → the layer is fully opaque.

### 2.4 Pillarbox, not scale

Panel is 1280×800. Source dimensions vary; `lcd_bind_capture(w, h)` centers the source via `xpos = (1280-w)/2`, `ypos = (800-h)/2`. Common cases:

| Source | Window | Pillarbox L+R | Letterbox T+B |
|---|---|---|---|
| 720×480 (Wii / Pi 480p) | 720×480 at (280, 160) | 280 px each | 160 px each |
| 1280×720 (Pi 720p) | 1280×720 at (0, 40) | 0 (fills width) | 40 px each |

No scaling engine needed for either source yet. BASE layer (below HEO in z-order) paints the surrounding black region — it's still the MCC-auto-allocated 1280×800 buffer of zeroes.

If the source is larger than 1280×800 (e.g. 1080p), `lcd_bind` bails with a log error and the bind step is skipped (capture pipeline still runs into DDR, just nothing on screen). Downscaling uses the HEO hardware scaler — `HEOCFG3`/`HEOCFG4` set the display rect vs. source-memory rect independently.

### 2.5 Per-frame pointer swap

On every ISC frame-done ISR, `video.c` calls `XLCDC_SetLayerAddress(XLCDC_LAYER_HEO, just_completed_addr, true)`. The `update=true` flag latches the new address at the next panel VSYNC, so LCDC never reads mid-frame and the display tracks every captured frame at the full 60 Hz ISC rate. With a 4-deep capture ring, there is no buffer aliasing — ISC is never writing the same buffer the display is reading.

---

## 3. Cache coherency

`g_framebuffer` lives in `.region_nocache` (uncached DDR). All DMA consumers (ISC write, LCDC read, CPU vision reads) see the same bytes directly from DDR — no cache flush or invalidate calls needed. This is the simplest correct choice for a buffer written by DMA and read by multiple consumers including the CPU.

---

## 4. Call sequence

All wiring is in `firmware/marvin/default/src/app.c`:

### 4.1 Boot-time (`APP_Initialize`)

```c
XLCDC_EnableBacklight();
```

MCC's auto-initialization (`SYS_Initialize`) already ran:
- `XLCDC_Initialize()` — clocks, timing engine, all 4 layers (BASE/OVR1/HEO/OVR2) set up at 1280×800 with auto-allocated buffers in `.region_nocache` (now in RGB565, ~2 MB per layer).
- `XLCDC_Start()` — panel clock, sync, disp enable, SD enable (all called from inside `Initialize`).
- `LVDSC_Initialize()` — LVDS serializer.
- `DRV_GFX2D_Initialize()` — 2D graphics engine (available for later work).

Backlight is *not* turned on by auto-init. We do it ourselves.

### 4.2 Per-capture (`app_coordinate_capture` → `lcd_bind`)

Runs once when TC358743 reports a lock and `ISC_Capture_Configure(w, h)` succeeds. See `video.c` `lcd_bind()` for the current implementation. Key settings:

- `XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO, XLCDC_RGB_COLOR_MODE_RGB_888_PACKED, ...)`
- `XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, x, y, ...)` — centered: `x = (1280-w)/2`, `y = (800-h)/2`
- `XLCDC_SetLayerWindowXYSize` and `HEOCFG3`/`HEOCFG4` for display and source-memory rects respectively
- All `update=false` until the final `SetLayerEnable(true, true)` which commits atomically

`XStride = 0` means no padding bytes between rows — source memory is tightly packed at `width × 3 B/row` for BGR888.

---

## 5. Current limitations / next steps

- **Panel-exceeding sources aren't displayed.** Pi 720p (1280×720) fits the 1280×800 panel; only 1080p and beyond need the HEO scaler enabled.
- **BASE layer is still the MCC-auto-allocated 1280×800 black buffer.** ~2 MB of `.region_nocache` DDR (RGB565) permanently displaying black around our overlay. Low priority; could reclaim by dropping `XLCDC_BUF_PER_LAYER` to 0 for BASE.
- **Limited→full range expansion — not yet done.** TC358743 reports RGB limited-range (16–235) for both Wii and Pi sources, so bytes in DDR reflect that range. Vision consumers deliberately see unexpanded bytes (linear rescale carries no new signal). The expansion is cosmetic, needed only because the panel expects 0–255 and renders limited-range data with muted blacks/whites.
  - **Planned path:** HEO layer Color Space Conversion matrix (`HEOCFG14..17`). Programmable `out = M × in + offset` configured as identity RGB→RGB with the 219-step limited→full gain/offset. Zero CPU, display-only, capture framebuffer unchanged.
