# marvin BGR888 → LCD Display Path

How the captured video (documented in [`capture_pipeline.md`](capture_pipeline.md)) lands on the 10.1" 1280×800 LVDS panel. Implementation: BGR888 packed from DDR → SAM9X75 XLCDC HEO layer → LVDSC → panel. Zero CPU on the display path; the HEO hardware scaler maps the detected active picture onto a window rect; per-frame pointer swap on the ISC frame-done ISR.

> **This is a standalone reference for the HEO scanout mechanics.** The HEO layer,
> its scaler, active-picture crop, levels-expansion CLUT, and the surrounding UI
> compositing are owned by `ui_manager`; the deeper source for all of that is
> [`ui_compositor.md`](ui_compositor.md) §14–16 (and §15.1 for the gamma CLUT). This
> doc covers the DDR→HEO→panel path itself.

**Status:** working. The live capture is displayed windowed over the operator dashboard and can be blown up to fullscreen by tapping it. The HEO scaler maps the detected active-picture rect (the source's dead black bars cropped out) onto whichever destination rect is in use, so both the windowed rect and fullscreen are scaled to fit.

---

## 1. Path overview

```
DDR framebuffer (g_framebuffer in video/isc_capture.c)
  BGR888 packed, 32-byte aligned, in .region_nocache
        │                     <── XLCDC DMA reads
        ▼
XLCDC HEO layer (High-End Overlay, with hardware scaler)
  RGB_888_PACKED color mode (memory order B, G, R per pixel)
  source-memory rect = detected active crop; display rect = window/fullscreen
  bicubic scaler maps active crop → destination rect
  per-component gamma CLUT expands limited→full range (display-only)
  base address re-pointed to the just-completed ring slot on every ISC ISR
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

### 2.1 HEO, not OVR1/OVR2 or BASE

The SAM9X75 XLCDC BASE layer has **no window position or size registers** — its window is always the full configured display size. Pointing BASE at a 720-wide framebuffer while the display is 800 wide produces an 80 px/row skew.

Overlays (OVR1, OVR2, HEO) have position/size registers so their window can be smaller than the panel and positioned anywhere. **We use HEO** (High-End Overlay) because:
- It has a built-in hardware scaler (`HEOCFG3`/`HEOCFG4` separate the display rect from the source-memory rect; `HEOCFG23`–`HEOCFG31` drive the bilinear/bicubic engine) — used to map the captured active picture onto the target window.
- It natively supports `RGB_888_PACKED` color mode, which matches our BGR888 capture format exactly.
- OVR1/OVR2 lack a scaler and are used by the UI compositor for the video frame overlay, nav drawer, and dialogs.

### 2.2 `XLCDC_RGB_COLOR_MODE_RGB_888_PACKED`

Our capture format is BGR888 packed — memory byte order per pixel is `B, G, R` (low address to high), dense with no padding byte.

The XLCDC `RGB_888_PACKED` mode reads memory in this byte order (per SAM9X75 LCDC datasheet Table 44.26). Setting this mode on the HEO layer lets LCDC DMA read the capture buffer directly with zero conversion. `heo_bind` sets it with `XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO, RGB_888_PACKED, …)`.

### 2.3 Active-picture crop + scale, not raw pillarbox

The Wii's component→HDMI conversion frames the active raster with dead black bars whose thickness varies by converter, so the active rect is **detected at runtime** (`Video_GetActiveRect`, see `ui_compositor.md` §15). `heo_bind` consumes it as the HEO source crop:

- offset the frame base to the active top-left (`ay·stride + ax·bpp`),
- size the source-memory rect (`HEOCFG4` `XMEMSIZE`/`YMEMSIZE`) to the active `w/h`,
- skip the cropped columns each line via `XSTRIDE = (src_w − active_w)·bpp`,
- size the display rect (`HEOCFG3` `XSIZE`/`YSIZE`) to the destination window.

When the display rect differs from the active source size, `heo_bind` engages the scaler: bicubic luma+chroma on both axes (`HEOCFG30`/`HEOCFG31` `…BICU`, enabled via `HEOCFG23`), with the 12.20 fixed-point factors `factor = (src / dst) << 20` in `HEOCFG24`–`HEOCFG27`. Until active-picture detection locks, a full-frame fallback (`x=y=0`, full `w/h`) is used.

### 2.4 Levels expansion via the HEO gamma CLUT

The source is mildly range-compressed against the panel's full 0..255. The HEO **per-component gamma CLUT** (`HEOCFG1.GAM`, `LCDC_HEOCLUT[256]`) operates on true RGB, so `ui_manager` loads it with a linear levels-expansion curve (near-black → 0, near-white → 255) and enables `GAM`. This is **display-only** — the DDR capture is untouched, so the detector/gameplay read raw pixels; the expansion is applied by the LCDC at scanout at zero runtime cost. `XLCDC_SetLayerRGBColorMode` clears `GAM` on every bind, so `heo_bind` re-asserts it after. See `ui_compositor.md` §15.1.

### 2.5 Per-frame pointer swap (`heo_frame_latch`)

On every ISC frame-done ISR, `video.c` invokes the compositor's frame-latch callback, `heo_frame_latch(buffer_addr)` in `ui_manager.c`. It re-points HEO at the just-completed ring slot:

```c
XLCDC_REGS->LCDC_HEOYFBA = buffer_addr + s_heo_crop_off;
XLCDC_REGS->LCDC_ATTRE  |= LCDC_ATTRE_HEO_Msk;
```

This is a **non-blocking** base update: it writes the frame address and *requests* the layer-attribute update but does not wait for the sync. `XLCDC_SetLayerAddress(…, true)` would busy-wait on `LCDC_ATTRE/ATTRS_SIP` until the update latches (a vsync away while HEO scans) — too costly for the per-frame ISC IRQ. The address register is double-buffered, so the hardware latches the new base at the next vsync on its own. ORing into `LCDC_ATTRE` (rather than overwriting) preserves any pending BASE/OVR update. The crop offset (`s_heo_crop_off`, set by `heo_bind`) points HEO at the active top-left of the slot. With a 4-deep capture ring there is no buffer aliasing — ISC is never writing the same slot HEO is reading.

---

## 3. Cache coherency

`g_framebuffer` lives in `.region_nocache` (uncached DDR). All DMA consumers (ISC write, LCDC read, CPU vision reads) see the same bytes directly from DDR — no cache flush or invalidate calls needed. This is the simplest correct choice for a buffer written by DMA and read by multiple consumers including the CPU.

---

## 4. Ownership & call sequence

The HEO hardware layer is owned by **`ui_manager`** (`firmware/marvin/default/src/ui/ui_manager.c`), not by `video.c`. `video.c` is the capture *producer*; it exposes two callbacks that `ui_manager` registers in `UiManager_Initialize`:

- `Video_SetFrameLatchCallback(heo_frame_latch)` — fires in the ISC IRQ, re-points HEO at the freshest complete ring slot (§2.5).
- `Video_SetDisplayReconcileCallback(heo_reconcile)` — fires each video-task tick; (re)binds/unbinds HEO to match the compositor's show intent and the current source/active-crop size.

Every HEO/BASE register write lives in `ui_manager.c` but executes in the video-task or IRQ context, so those layers stay single-writer. The UI task only sets volatile intent via `UiManager_VideoShow(x,y,w,h)` / `UiManager_VideoHide()`.

### 4.1 Backlight — PWM, not `XLCDC_EnableBacklight`

The panel backlight is **PWM-dimmed on PC18** (PWM channel 0, configured by MCC), not the LCDC's `LCD_PWM` output — `XLCDC_EnableBacklight()` is **not** called. `UiManager_SetBacklight(pct)` writes the duty:

```c
uint32_t period = PWM_ChannelPeriodGet(PWM_CHANNEL_0);
PWM_ChannelDutySet(PWM_CHANNEL_0, (period * (100u - pct)) / 100u);
```

The channel is `CPOL_LOW` — it idles low (dark) when stopped — so `CDTY` is the low-level time and brightness is the high fraction (`pct=100` → `CDTY 0` → full bright). The channel is left **stopped** until the splash is up; `enable_backlight()` sets the persisted brightness (`Settings_Get()->backlight_pct`, restored from QSPI) then `PWM_ChannelsStart(PWM_CHANNEL_0_MASK)`, so the panel stays dark until the boot task lights it after the splash is painted — no pre-splash frame.

### 4.2 Bind (`heo_bind`)

Runs from the video-task reconcile when video is shown and the source is locked. Key writes (all in `ui_manager.c`, see §2.2–2.4):

- `XLCDC_SetLayerRGBColorMode(HEO, RGB_888_PACKED, false)` + re-assert `GAM`.
- `XLCDC_SetLayerAddress(HEO, frame_base + crop_off, false)`.
- `XLCDC_SetLayerXStride(HEO, (src_w − active_w)·bpp, false)`.
- `XLCDC_SetLayerWindowXYPos(HEO, x, y, false)`.
- `HEOCFG3` (display rect) and `HEOCFG4` (source-memory = active crop) written directly.
- Scaler (`HEOCFG23`–`HEOCFG31`) enabled when the display rect ≠ active source size, else disabled.
- `XLCDC_SetLayerEnable(HEO, true, true)` commits.

If the requested window exceeds the panel, `heo_bind` logs a warning and skips (capture still runs into DDR). `heo_reconcile` rebinds when the window, source size, or active-crop rect changes, and `heo_unbind` disables HEO when video is hidden.

---

## 5. Current limitations / next steps

- **Source sizing.** Sources are scaled to the destination rect via the HEO scaler; a window/fullscreen rect larger than the panel is rejected by `heo_bind`.
- **BASE DMA discard.** While the video (or the song-select dialog) opaquely covers part of BASE, `ui_manager` arms the single BASE discard window (`base_discard_reconcile`) so BASE skips its DDR read behind the overlay, freeing read bandwidth. See `ui_compositor.md`.
- **Levels curve is fixed.** The pedestal/ceiling constants may vary by Wii/converter; retune `VIDEO_LEVELS_BLACK`/`WHITE` if the source changes (`ui_compositor.md` §15.1).
