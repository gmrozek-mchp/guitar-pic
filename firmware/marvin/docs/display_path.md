# marvin BGRX32 → LCD Display Path

How the captured video (documented in [`capture_pipeline.md`](capture_pipeline.md)) lands on the AC69T88A 7" LVDS panel. Minimum-viable implementation: BGRX32 from DDR → SAM9X75 XLCDC OVR1 layer → LVDSC → panel. Zero CPU, zero scaling, pillarboxed.

**Status:** working as of 2026-05-02 at 720×480 pillarboxed onto 800×480 panel. Source coming from Wii (via ElectronWarp) or Pi, both through TC358743.

---

## 1. Path overview

```
DDR framebuffer (g_framebuffer in isc_capture.c)
  BGRX32, 32-byte aligned, in .region_cache_aligned
        │                     <── XLCDC DMA reads
        ▼
XLCDC OVR1 layer (Overlay 1)
  ARGB_8888 color mode (memory byte order matches BGRX32)
  720×480 window, positioned at (40, 0) on the 800×480 panel
  alpha=255 global, per-pixel A ignored
        │
        ▼
XLCDC timing engine  (800×480 @ 60 Hz, 7" panel timings from MCC)
        │
        ▼
LVDSC  (LVDS serializer)
        │
        ▼
AC69T88A LCD panel (800×480 7" LVDS)
```

---

## 2. Key decisions, with rationale

### 2.1 OVR1, not BASE

The SAM9X75 XLCDC BASE layer has **no window position or size registers** (`XLCDC_SetupBaseLayer` in `plib_xlcdc.c` configures `BASECFG0..BASECFG6` but no XPOS/YPOS/XSIZE/YSIZE). Its window is always the full configured display size. Pointing BASE at a 720-wide framebuffer while the display is 800 wide produces an 80 px/row skew because LCDC reads 800 × 4 = 3200 B/row but memory only has 720 × 4 = 2880 B/row — each row drifts left.

Overlays (OVR1, OVR2, HEO) have `OVRxCFG2` (XPOS/YPOS) and `OVRxCFG3` (XSIZE/YSIZE), so their window can be smaller than the panel and positioned anywhere. OVR1 is the simplest overlay (no scaling, no chroma key). That's what we use.

### 2.2 `XLCDC_RGB_COLOR_MODE_ARGB_8888`

Our memory layout is `B G R X` (low-to-high), i.e. `0x00RRGGBB` as a little-endian 32-bit word.

The MCC XLCDC enum exposes only `ARGB_8888` and `RGBA_8888`. Per SAM9X75 LCDC convention:

| Mode | Memory byte order (low → high) |
|---|---|
| `ARGB_8888` | **B, G, R, A** |
| `RGBA_8888` | A, B, G, R |

`ARGB_8888` is byte-for-byte compatible with our BGRX32 output. The `A` byte in memory ends up being our `X = 0x00`, which would mean fully transparent — but we override at the layer level:

### 2.3 Global alpha via `XLCDC_SetLayerOpts(layer, 255, true, false)`

OVR1's layer opts take a global alpha (0–255) and an `enable_dma` flag. We pass `alpha = 255` + `enable_dma = true`:
- `enable_dma = true` → pixel data comes from the framebuffer via DMA (RGB channels only).
- `alpha = 255` → the layer is fully opaque regardless of the per-pixel A byte.

So our `X = 0x00` in the framebuffer is harmless; the display shows opaque RGB.

### 2.4 Pillarbox, not scale

Source 720×480, panel 800×480. Simplest mapping: center the source horizontally with 40 px black bars on each side (`xpos = (800-720)/2 = 40`, `ypos = 0`). No scaling engine needed. BASE layer (below OVR1 in z-order) paints the side bars; it's still the MCC-auto-allocated 800×480 buffer of zeroes, so the pillarbox is naturally black.

If the source is 1280×720 (Pi native), this path bails (`LCD: source exceeds panel`) and does nothing. Scaling a 1280×720 source to fit 800×480 requires the HEO layer's hardware scaler, which the MCC plib does not expose — it'd need direct `HEOCFG*` register writes. Deferred.

### 2.5 Single-buffer read, tear-tolerant

XLCDC DMA re-reads the OVR1 base address on each of its own frame refreshes (60 Hz panel). We point it at **buffer 0** of the double-buffer pool (`ISC_Capture_GetBufferAddress()` returns the pool base = buffer 0). ISC DMA alternates writes between buffer 0 and buffer 1, so about half the captured frames are only visible to the CPU/vision stage, not to the display.

Practical effect: display effective refresh is ~30 Hz (every other ISC write) and a scanline-level tear is possible where an LCDC read crosses mid-frame an ISC write. Good enough for a sanity check and for static/slow content. Upgrades when needed:
- **Synchronized pointer swap:** on ISC frame-done ISR, call `XLCDC_SetLayerAddress(OVR1, just_filled_buffer, true)`. The "update" flag delays the effect until the next panel VSYNC, so no mid-frame read tear.
- **Single-buffer capture:** set `iscObj->dmaDescSize = 1`. ISC overwrites the same buffer continuously; display tracks it at full 60 Hz with some scanline tear but no frame drop.

---

## 3. Cache coherency

`g_framebuffer` lives in `.region_cache_aligned` (cached DDR). Consumers should read it like this:

- **CPU does not write to the buffer** → no flush needed. ISC DMA writes bypass CPU cache and go straight to DDR; LCDC DMA reads straight from DDR. Both see the same data.
- **If CPU (vision, overlay rendering, alpha stamp) ever writes to the buffer** → call `SYS_CACHE_CleanDCache_by_Addr((uint32_t *)addr, size)` before yielding to LCDC, and `SYS_CACHE_InvalidateDCache_by_Addr` before reading something that DMA wrote.

MCC's `drv_gfx_xlcdc.c` places its own auto-allocated buffers in `.region_nocache` to sidestep this entirely. We don't need that for a read-only display, but it's the standard pattern for CPU-rendered content.

---

## 4. Call sequence

All wiring is in `firmware/marvin/default/src/app.c`:

### 4.1 Boot-time (`APP_Initialize`)

```c
XLCDC_EnableBacklight();
```

MCC's auto-initialization (`SYS_Initialize`) already ran:
- `XLCDC_Initialize()` — clocks, timing engine, all 4 layers (BASE/OVR1/HEO/OVR2) set up at 800×480 with auto-allocated buffers in `.region_nocache`.
- `XLCDC_Start()` — panel clock, sync, disp enable, SD enable (all called from inside `Initialize`).
- `LVDSC_Initialize()` — LVDS serializer.
- `DRV_GFX2D_Initialize()` — 2D graphics engine (available for later work).

Backlight is *not* turned on by auto-init. We do it ourselves.

### 4.2 Per-capture (`app_coordinate_capture` → `lcd_bind_capture`)

Runs once when TC358743 reports a lock and `ISC_Capture_Configure(w, h)` succeeds:

```c
XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, false, true);
XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_OVR1, XLCDC_RGB_COLOR_MODE_ARGB_8888, false);
XLCDC_SetLayerAddress(XLCDC_LAYER_OVR1, ISC_Capture_GetBufferAddress(), false);
XLCDC_SetLayerXStride(XLCDC_LAYER_OVR1, 0u, false);
XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_OVR1, (800-w)/2, (480-h)/2, false);
XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_OVR1, w, h, false);
XLCDC_SetLayerOpts(XLCDC_LAYER_OVR1, 255u, true, false);
XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, true, true);
```

Note the `update = false` on all but the last call. The last `SetLayerEnable(true, true)` commits the pending config atomically, so the layer doesn't briefly render with a mix of old/new settings.

`XStride = 0` means "no extra bytes between rows" — source memory is tightly packed at `window_width × 4 B/row`. If we later consume a buffer wider than the window (e.g. cropping a 1280×720 source down to an 800-wide visible strip), XStride would be `(source_width - window_width) × 4`.

---

## 5. Current limitations / next steps

- **Panel-exceeding sources aren't displayed.** At Pi 720p the capture works; display bails. Either crop at the LCDC (set window = 800×480, XStride = `(1280-800)*4`, keeps a scrolling-window view) or scale via HEO.
- **No double-buffer swap** — LCDC stuck on buffer 0, ~30 Hz effective with possible tear. Switch to ISR-driven pointer swap when motion clarity matters.
- **BASE layer is still the MCC-auto-allocated 800×480 black buffer.** It uses ~1.5 MB of `.region_nocache` DDR that's permanently displaying black under our pillarbox. Low priority; could reclaim by dropping `XLCDC_BUF_PER_LAYER` to 0 for BASE, or by disabling BASE entirely (the pillarbox area would then go transparent and show whatever the LCDC's "default layer color" is — `BASECFG3.RDEF/GDEF/BDEF = 0,0,0`, i.e. black, same result).
- **Analog-chain range expansion** — Wii peaks at ~0xC8 instead of 0xFF (limited-range + analog loss). LCDC has no built-in range expansion; would need a CPU pass (`out = min(255, (in - 16) * 255 / 219)`) or a GFX2D blit with scaling LUT, only if the dimness is actually a problem.
