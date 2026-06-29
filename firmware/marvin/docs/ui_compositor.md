# marvin — UI architecture (canvas compositor + per-screen modules)

How marvin's operator UI is structured: independently-authored panels (dashboard, nav,
dialogs) each render into their own **canvas surface** in RAM, the compositor maps surfaces
onto the scarce **LCDC hardware layers**, and — with the MGS screen state machine disabled —
the application owns screen orchestration so multiple panels coexist **live and interactive
at once** instead of one-screen-at-a-time switching.

Read [`spec.md`](spec.md) §4.5 (operator UI) and §4.1 (video/display) first. Decisions in
[`journal.md`](journal.md) (2026-06-25, 2026-06-29). **Status: migrated to the MGS layer-screen
model and confirmed on hardware** — one master `Marvin` screen with dashboard (BASE), nav
(OVR1), and song-select dialog (OVR2) as layer-screens; the model and the canonical mental
picture are in §0.

---

## 0. The mental model (read this first) — three concepts, not two

These are distinct and must not be collapsed (this has been a recurring source of confusion):

1. **Legato layer ("layer-screen")** — a logical screen / dialog / overlay. Bounded by
   `LE_LAYER_COUNT` = the number of layer-screens.
2. **GFX canvas** — the RAM framebuffer backing a layer-screen. **Coupled 1:1 to its Legato
   layer:** the renderer writes Legato layer *i* into `canvas[baseCanvasID + i]` (base 0 →
   layer *i* ↔ `canvas[i]`), and `leAddRootWidget(root, layerIdx)` rejects
   `layerIdx >= LE_LAYER_COUNT`. So a canvas id is **not** free of its Legato layer.
3. **XLCDC hardware layer** (`BASE`/`OVR1`/`OVR2`; `HEO` = camera) — where a canvas is
   composited for scanout. Bound at **display time, at runtime, and reassignable** via
   `gfxcSetLayer(canvasID, hwLayer)` (`ui_manager.c` `bind_canvas`).

**"canvas ≠ layer" is true only for the *hardware* layer (3), not for the *Legato* layer (2).**
A panel never *owns* a HW layer — two canvases never shown together can share one; the same
canvas can be on `OVR1` in one situation and `OVR2` in another. The 8-slot canvas pool
(`CONFIG_CANVAS_NUM_OBJ = 8`) means up to 8 layer-screens may be **defined**
(`LE_LAYER_COUNT` ≤ 8, canvases 0–7); the LCDC then composites **any 3** of them onto its 3
usable HW layers at once — *define many, show a few.*

### 0.1 Direction (decided 2026-06-29): adopt MGS's single-master-screen / layer-screen model

Per Microchip's GFX-canvas layer-screen guide¹, MGS is meant to be **one master screen whose
*layers* are the logical screens** ("layer-screens"), each auto-associated with its own canvas.
marvin **adopted this model on 2026-06-29** (confirmed on hardware). It previously *emulated*
it by hand — each panel a **separate 1-layer MGS Screen**, built on layer 0 then manually
disconnected and re-hosted by `ui_manager`, with `LE_LAYER_COUNT` pinned at 3 by a never-shown
`LayerBudget` screen. Now there is **one master screen named `Marvin`, one layer per panel**:
layer 0 dashboard (BASE), layer 1 nav (OVR1), layer 2 song-select dialog (OVR2). MGS derives
`LE_LAYER_COUNT` and associates each layer-screen with `canvas[i]`, so the `LayerBudget` hack
and the disconnect/re-host dance are gone — `ui_manager` calls `screenInit_Marvin()` +
`screenShow_Marvin()` once (the latter builds the tree and attaches each root to its Legato
layer), then per-panel `*_Setup()` wiring, and binds canvases to HW layers at display time.
**The splash is separate and fully manual** (pre-Legato RGBA8888 scanout): it is *not* a
canvas at all — it owns a static framebuffer and drives its XLCDC hardware layer directly via
the PLIB (`splash.c` `Splash_Show`/`Splash_Hide`, mirroring `video.c`'s HEO setup), so the
canvas pool is exactly the three layer-screens (0/1/2).

> **§1–§7 below were written for the previous per-screen-Factory + manual-re-host design** and
> are kept for history; where they conflict with §0, §0 wins. `screenInit_X` is now a one-time
> flag only — the widget tree is constructed in **`screenShow_X`** (a gotcha worth remembering).

¹ developerhelp.microchip.com → MGS Harmony guide → how-to → gfx-canvas → layer-screen.

---

## 1. The core idea

Content geometry was welded to hardware 1:1:1 — one Legato layer → one framebuffer → one LCDC
layer. There are only **4 LCDC layers** (`BASE`, `OVR1`, `HEO`, `OVR2`); `HEO` is the live
camera. The compositor breaks the welding:

- A **surface** is a RAM framebuffer (a GFX Canvas object) holding a panel's pixels.
- A **layer** is a hardware compositor window. The LCDC blends them every scan with zero CPU.
- Showing a panel = point a layer at its surface + enable (register writes, no re-render).
  Hiding = disable/move the layer. Sliding = animate the layer's window position.

**GFX Canvas** is the substrate: `GFXC_GetPixelBuffer` redirects Legato's render target to a
canvas object, and `gfxcSetLayer`/`ShowCanvas`/`SetWindowPosition` map/show/move that surface
on an LCDC layer. (Why canvas and not a hand-rolled retarget or direct-layer-only: see the
2026-06-25 journal entry — canvas is the only thing that gets Legato content *into* an
arbitrary off-screen surface; direct layer control still does the show/move.)

## 2. The enabling change — MGS screen state machine OFF

Legato's stock "screen state machine" (`le_gen_init.c`) shows **one screen at a time** —
`legato_showScreen` tears down the outgoing screen's widget tree. That is fatal to a compositor
that wants several panels live simultaneously. So it is **disabled** ("Generate Screen State
Machine" off; `le_gen_init.{c,h}` no longer generated — see the 2026-06-25 journal entry and
the `compat/` stub that works around the resulting MGS include bug).

Two confirmed facts make this work (verified in `legato_state.c` / `legato_renderer.c` /
`legato_input.c`):

1. **`_state.layerList` is global and `leUpdate` renders/updates *every* attached layer each
   frame.** There is no "active screen" render gate. Attach roots to multiple layers → they all
   render live.
2. **Touch picks across all attached layers, top-to-bottom** (`leInput` iterates `layerList`
   from the top). So an attached overlay is interactive wherever it sits; the layer below
   receives touches elsewhere. *(This supersedes the earlier "parked surfaces aren't pickable /
   reveal-then-promote" idea — that was a consequence of the state machine, which is now gone.)*

So the model is simply: **attach a panel's root to a layer → it is live and interactive;
detach (or move off-screen) → it is gone.** No freeze, no promotion dance.

## 3. MGS Screen = widget-tree factory

With the state machine off, the generated per-screen functions are ours to call directly
(MGS even relabels them `// call to show this screen`). We treat each **MGS Screen as a
widget-tree factory**, decoupled from display:

- `screenInit_X()` — builds the widget tree (+ registers any MGS-wired events).
- `screenShow_X()` — attaches its root(s) to layer(s) and raises the `X_OnShow` hook.
- `leAddRootWidget(wgt, layer)` / `leRemoveRootWidget(wgt, layer)` — **re-host** a root onto
  whatever Legato layer we want, independent of the index MGS authored it on.
- `screenGetRoot_X(lyrIdx)` — fetch a root to re-host or toggle visibility.

So each panel is authored in its own clean MGS Screen, then the compositor assembles the
trees onto the layer set it manages. The screen's `OnShow` hook is that panel's
**composition root** — where it runtime-registers its widget event callbacks (the DI pattern;
see the 2026-06-25 journal entry on widget events being runtime-registerable while screen
lifecycle events are direct calls).

## 4. Hardware layer budget

| Legato layer | LCDC | Content | Lifecycle |
|---|---|---|---|
| 0 | BASE | active full-screen view (dashboard, …) | swappable (replace) |
| 1 | OVR1 | **nav drawer** | resident — attach once, persists across base-view swaps |
| 2 | OVR2 | **modal dialog** (song/mode select), sized to the dialog | shown/hidden; background stays live |
| — | HEO | camera | `video.c`, **outside Legato** |

- **Nav on its own resident layer ⇒ "same nav over every view" for free** — it is independent
  of layer 0, so swapping the base view leaves it untouched.
- **Modal dialog on OVR2 sized to the dialog ⇒ uncovered dashboard telemetry keeps updating**
  (layer 0 renders live behind it). The dialog is invoked only from the dashboard.
- **Ceiling:** dashboard + nav + dialog + camera = **all 4 LCDC layers**. A 4th simultaneous UI
  surface needs canvas multiplexing (`gfxcSetBaseCanvasID`) or giving up a layer.

### 4.1 Two layer counts — don't conflate them
- **`LE_LAYER_COUNT`** (Legato, `legato_config.h`) = how many canvases / Legato layers the global
  `layerList` manages = **3**. MGS derives it as the **max layer count across all screens** in the
  design; there is no explicit knob.
- **`XLCDC_TOT_LAYERS`** (XLCDC driver, "Total Layers" in `le_gfx_driver_xlcdc.yml`) = enumerated
  hardware layers = **4** (`layerOrder` = {BASE 0, HEO 1, OVR1 2, OVR2 3}).

They differ on purpose: Legato manages 3 canvases, each mapped onto **3 of the 4** hardware layers.

### 4.2 Pinning `LE_LAYER_COUNT` = 3 (the LayerBudget screen)
Each panel is authored as its own **1-layer** MGS screen, so nothing reaches 3 layers on its own —
and there's no direct setting. A dedicated, **never-shown `LayerBudget` screen with 3 layers** pins
the count (regen-safe; no `legato_config.h` patch). Keep it un-shown: its roots then never attach,
so Legato layer 2 stays an empty internal root. **Caveat:** `LE_LAYER_COUNT` 3 lets Legato render
into `canvas[2]`, whose buffer is still the generated `NULL` until the dialog assigns one — safe
**only** while layer 2 has no attached root / no damage (i.e. LayerBudget is never shown and no panel
re-hosts onto layer 2 yet).

### 4.3 Why HEO is safe (and stays enabled)
Layer targeting is **explicit, not positional**: Legato layer *i* → `canvas[i]` →
`gfxcSetLayer(i, hwLayer)`, where `hwLayer` indexes `layerOrder`. We map canvases only to hw layers
**0 / 2 / 3** (BASE / OVR1 / OVR2) and **never to 1 (HEO)**, so the canvas framework never issues an
IOCTL to HEO regardless of enumeration order. The only GFX-side touch of HEO is
`DRV_XLCDC_Initialize` disabling all enumerated layers once at init — before `video.c` configures it
(post-scheduler), so video is the last writer. So **HEO stays enabled** and `video.c` keeps full
runtime control via the XLCDC PLIB, including the `HEOCFG12` blender + `VIDPRI` z-order that
`XLCDC_SetupHEOLayer` provides (`video.c` does not set those itself — which is exactly why disabling
HEO in MCC would break compositing, and why we don't).

## 5. Live coexistence vs. replacement

The compositor offers two verbs, chosen per interaction — you do **not** pick one global mode:

- **Coexist (overlay-as-layer)** — the panel is its own layer over a still-live background.
  Used for the **nav drawer** (dashboard keeps updating behind it) and the **modal dialog**
  (telemetry keeps updating around it). No freeze.
- **Replace (switch base view)** — a full-screen view replaces another on layer 0; the
  outgoing one detaches (its surface can hold the last frame, or it is rebuilt on return).
  Used when a view genuinely supersedes the dashboard and live background isn't needed.

Because the compositor owns layer placement and attach/detach, moving an interaction between
these is a compositor-level change, not a per-module rewrite — the reversibility we wanted.

## 6. Module structure

```
            ┌──────────────────────── ui_manager ─────────────────────────┐
            │ string-table init · screen init/show · root re-host onto     │
            │ layers · layer/canvas pool · visibility · verbs:             │
            │   show_view(X) · push_overlay(nav|dialog) · hide_overlay(…)   │
            └───────┬──────────────────────┬───────────────────────┬───────┘
              ┌─────▼─────┐          ┌──────▼──────┐          ┌──────▼──────┐
              │  dashboard │         │ screens/nav │          │ song_select  │
              │ (BASE,lyr0)│         │ (OVR1,lyr1) │          │ (OVR2,lyr2)  │
              └────────────┘         └─────────────┘          └──────────────┘
   each: owns its widgets + interactions; composition root = its screen OnShow hook
```

- **`ui_manager`** (promoted from today's `compositor.c`) owns the *mechanism*: string-table
  setup, calling `screenInit_/screenShow_`, re-hosting roots onto the right layer, the canvas
  buffer pool, layer visibility, and the coexist/replace verbs. It is the "top-level module
  that ties everything together."
- **Per-screen/overlay modules** own only their panel's content + event wiring, registered
  from their own `OnShow` composition root. They never call `legato_*`/canvas APIs directly —
  they go through `ui_manager`, which is what keeps the layer mechanics swappable.

### 6.1 Source layout (`default/src/ui/`)

The UI tree is grouped by role; module files carry an explicit `screen_`/`widget_` prefix so
a flat editor tab list reads unambiguously:

```
ui/
  ui_manager.{c,h}                          orchestrator (mechanism; stays at root)
  manual_input.c                            input shim for the future manual-input screen
  screens/<name>/   screen_<name>.{c,h}     one folder per panel (+ panel-specific helpers)
  widgets/<name>/   widget_<name>.{c,h}     reusable widgets, one folder each
```

Current contents: `screens/nav/screen_nav`, `screens/song_select/screen_song_select`,
`screens/splash/splash`, `widgets/song_list/widget_song_list`, `widgets/button_aa/widget_button_aa`.
Each panel module exposes `*_InitSurface()` (assign its canvas buffer, pre-scheduler) and
`*_Setup()` (wire content/events on the widgets `screenShow_Marvin` already built); `ui_manager`
calls these — the modules never bind HW layers themselves. Headers are included from the
`default/src` root, e.g. `#include "ui/screens/nav/screen_nav.h"`. New panels add a
`screens/<name>/` folder; new widgets a `widgets/<name>/` folder — each wired into `user.cmake`
(no MCC involvement).

## 7. Build state & refactor plan

**Built (committed):** GFX Canvas substrate; state machine off + app-owned `screenInit/Show`
(`compat/le_gen_init.h` stub); `ui_manager` orchestrator + dashboard on BASE; `LE_LAYER_COUNT`=3
pinned via the never-shown `LayerBudget` screen (§4.2). **Nav drawer is feature-complete:** its
own `Navigation` MGS Screen re-hosted onto OVR1; slide in/out via canvas Move FX (Move FX
re-enabled; `NAV_CLOSED_X` dodges the window-clip row-wrap; mid-slide reversal cancels the
in-flight move); single-active highlight via a runtime-registered shared release sink (Dashboard
closes, others switch); full-repaint-on-open; rounded buttons (set in code; not AA — §10).

**Next:**
1. **Author song/mode-select as its own MGS Screen;** assign `canvas[2]` a real buffer and have
   `ui_manager` host it on layer 2 (OVR2) as a modal (sized to the dialog, dashboard live behind).
   First real exercise of the coexist verb with a *live* background; the re-host + slide
   mechanics are proven on the nav.
2. **`manual_input.c` fate** — `Screen0` is retired, but the strum handlers now serve the
   Dashboard screen, so `manual_input.c` stays until manual control is reworked.

## 8. Static-allocation, cache & priority rules

- **Static non-cached surfaces.** Each canvas surface is a file-scope static array in
  `.region_nocache` (no malloc), passed via `gfxcSetPixelBuffer`. They are CPU-rendered then
  read by the 2D engine / LCDC DMA, so non-cached is required (same policy as `FB_CACHE_NC`
  and the Legato scratch — 2026-06-24 cache-coherency fix). RGB565 to match the panel.
- **`GFX_CANVAS_Task`** is at UI-band priority 2 (set in the gfx_canvas yml), required even
  with FX off (drives `INIT→RUNNING`). It's a dynamic MCC task on heap_1 like the others.
- **Heap.** The canvas component pushed `heap_1` past 40 KB → silent boot hang via the
  malloc-failed hook; heap is now 65536 and the fault hooks emit a DBGU marker (2026-06-25).

## 9. Memory budget

`.region_nocache` is 32 MB, shared with the ISC capture pool (~10.5 MB) + Legato scratch
(0.5 MB). Full-screen RGB565 surface ≈ 2.0 MB; overlays sized to content are far less (nav
~320×800 ≈ 0.5 MB; a modal dialog smaller still). ~19 MB free after BASE + capture + scratch,
so **memory is not the binding constraint — the 3 UI layers are.**

## 10. Open items

- **Base-view replacement** — first exercised by the boot splash → dashboard handoff (§12).
  Still open: the *return* path (rebuild vs parked last-frame) and nav-persistence interaction
  when two interactive base views swap (the splash is non-interactive, so it didn't surface this).
- **Splash→dashboard transition** is a hard cut (the BASE buffer swaps from the 32bpp splash to
  the as-yet-unpainted RGB565 dashboard, so a frame of garbage/black is possible before the
  first dashboard paint). Acceptable for a one-time boot; smooth later by painting the dashboard
  into its buffer before re-binding the layer, or a brief fade.
- **Splash backlight timing** is a fixed settle (`SPLASH_RENDER_SETTLE_MS`) because the JPEG
  decode runs inside a Legato paint we can't cleanly join from the loader. Replace with a real
  paint-complete signal if the panel ever lights mid-decode.
- **Reveal-before-paint** — *resolved* by the pre-render/persistent model (§13): screens are now
  painted to completion off-screen before being revealed.
- **`manual_input.c` fate** — `Screen0` retired; the strum handlers now serve the Dashboard
  screen, so it stays until manual control is reworked.
- **Per-pixel alpha** — which (if any) overlay needs ARGB8888 over the camera vs. layer alpha.

## 10a. Anti-aliased rounded button corners (done)

The nav buttons use the widget `cornerRadius` (set in code — not exposed in MGS), but the
classic skin's rounded-rect fill draws corners with `leRenderer_ArcFill` (hard, stepped
edges). Smoothing is done **without touching Legato/MCC code** (`config/default/` is
off-limits), via a per-instance vtable re-point in `src/ui/widgets/button_aa/widget_button_aa.c`:

- `leWidget.fn` is a writable per-instance pointer to a `const` vtable. `ButtonAA_Enable`
  makes one shared writable copy of the button vtable, overrides only `_paint`, and points
  the button's `fn`/`widget.fn` at it. No `LE_DYNAMIC_VTABLES` (not an MCC knob; unguarded
  `#define 0` in generated `legato_config.h`), no global RAM cost — scoped to re-pointed
  buttons.
- The wrapper runs the original paint (bg + label + border), then on `DONE` with the
  supported radius overdraws the four corners: sample the backdrop from each corner's
  extreme pixel (left untouched by the rounded fill), then blend a 1px transition band from
  a precomputed coverage mask via `leColorLerp` + `leRenderer_PutPixel`. Fill is read from
  the live scheme each paint, so highlight (selected/unselected) and pressed states track.
- Runs **in-pass** (damage → repaint → AA in one paint), so no timing/flicker window.
- Wired from `nav_buttons_init()`; currently nav-only. Extending app-wide is just calling
  `ButtonAA_Enable` from other screens' button init. Single radius (`BUTTON_AA_RADIUS`);
  other radii pass through unsmoothed.

## 12. Boot splash (done — code-complete, pending hardware test)

A full-screen photo splash is the first thing on the panel: the loader reads it off the SD card
into RAM, gets it onto BASE, and only then lights the backlight, so the panel never shows a
pre-splash/garbage frame. It's also the first real use of the **replace** verb (§5) and proves
the runtime-image path the album-art pre-load will reuse.

- **Splash on its own OVR2 layer/canvas, drawn over the dashboard.** The `Splash` MGS screen
  (`le_gen_screen_Splash.c`: full-screen `Splash_Panel_0`, solid scheme fill) is kept as the base
  for the "every base view has an MGS component" convention; its only real content — the photo — is
  loaded, not authored, so `screens/splash/screen_splash.c` adds one `leImageWidget` onto
  `Splash_Panel_0` in code and points it at the in-memory JPEG (solid fill = fallback when there's
  no card/image). `ui_manager` re-hosts the splash onto **Legato layer 2 / canvas 2 / OVR2** (32bpp
  XRGB8888, full screen, topmost overlay) so it covers the BASE dashboard while the dashboard paints
  underneath; dropping OVR2 reveals the finished dashboard.
- **Runtime JPEG from SD.** The loader reads `/<card>/ui/splash.jpg` (≤1 MB, **baseline** JPEG —
  the decoder is baseline-only) into a static cache-aligned buffer; `Splash_SetImageJpeg` builds a
  runtime `leImage` (format `JPEG`, mode `RGB_888`, buffer = compressed bytes + length, mirroring a
  generated `leImage`) and sets it on the image widget. Legato's enabled JPEG decoder decodes on
  paint into the 32bpp canvas. The compressed buffer stays resident while the splash is shown.
- **OVR2 must be set to 32bpp by hand (driver gotcha).** The GFX-XLCDC driver
  (`drv_gfx_xlcdc.c`) assumes **every** layer is the project framebuffer format (`FB_COL_MODE` =
  RGB565, `FB_TYPE_SZ` = 2): its canvas commit path (`SET_LAYER_UNLOCK`) writes address/alpha/
  position/size/stride/enable but **never `RGBMODE`**, so a canvas's `GFX_IOCTL_SET_LAYER_COLOR_MODE`
  is captured in `drvLayer.pixelformat` and silently dropped. An RGBA8888 splash buffer therefore
  gets read by a still-RGB565 OVR2 → garbled. `ui_manager` fixes this with a direct
  `XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_OVR2, RGBA_8888, true)` after binding the splash canvas;
  the driver never rewrites RGBMODE, so it sticks (same runtime XLCDC poke `video.c` uses for HEO).
  BASE/OVR1 stay RGB565 = the default, so they need no poke. Full-screen ⇒ XSTRIDE gap is 0, so the
  driver's `FB_TYPE_SZ`-based stride calc is harmless here.
- **Backlight gating.** The backlight is a **GPIO** (`AC69T88A_BACKLIGHT_EN` / PC18), *not* the LCDC
  PWM — `XLCDC_EnableBacklight()` only enables the LCDC's `LCD_PWM` dimming output, which isn't
  wired to this board's backlight (so calling it does nothing visible). `UiManager_EnableBacklight()`
  drives PC18 directly (`AC69T88A_BACKLIGHT_EN_Set()`, active-high assumed). The pin starts low at
  boot (MCC PIO config — the old "set high on start" that made the panel light too early is gone),
  so the panel stays dark until the loader calls this after the splash is painted. Video go-live
  (`Video_SetWindow`/`CaptureEnable`/`DisplayShow`) also moved into the loader, post-handoff, so the
  camera doesn't pop in over the splash.
- **Asset:** seed at `data/ui/splash.jpg` (1280×800), copied to the card's `/ui/splash.jpg`
  (mirrors the catalog `data/` convention).

## 13. Pre-rendered, persistent per-screen canvases (done — code-complete, pending hardware test)

Fixes the reveal-before-paint flash (§10): a screen used to be shown while Legato incrementally
painted its freshly-rebuilt tree, so unpainted regions flashed uninitialized-DRAM noise (~1–2 s for
the Dashboard's ~200-widget tree). The model is now the §1 ideal — **paint into an off-screen
surface, then reveal**:

- **Every screen is persistent + owns a canvas.** MGS screens are set **persistent** (built once in
  `screenInit_*`, never torn down — `screenHide_*` no longer deletes), each re-hosted onto its own
  Legato layer → canvas → HW layer: Dashboard 0/0/BASE (RGB565), Nav 1/1/OVR1 (RGB565), Splash
  2/2/OVR2 (RGBA8888). `CONFIG_CANVAS_NUM_OBJ` (8, expandable) gives room per screen; no
  `gfxcSetBaseCanvasID` multiplexing needed.
- **Build pre-scheduler, then let the render task paint; the loader just waits.** All screens are
  built + hosted on their layers in `UiManager_Initialize` (pre-scheduler → no concurrency). Once
  the scheduler is up, the normal `LEGATO_Tasks` + `GFX_CANVAS_Task` pair paints them — a full-screen
  surface spans several scratch tiles (`LE_SCRATCH_BUFFER_COUNT = 1`, 512 KB ⇒ ~131 072 px @ 32bpp),
  and the scratch is freed by the canvas commit *between* render passes, which only happens when
  those tasks actually run. So the loader does **not** drive `leUpdate` or touch the scheduler — it
  yields and polls the public `leRenderer_IsIdle()`. Important: `leRenderer_IsIdle()` is just
  `frameState == LE_FRAME_READY`, which is **also true in the gaps between `leUpdate` calls** (which
  tick only every ~10 ms), so a single sample reads "done" mid-paint; `wait_render_idle` requires
  idle to hold **continuously for ≥120 ms** before trusting it. Boot sequence: load JPEG (retried —
  SDMMC isn't ready this early) → `Splash_SetImageJpeg` → `wait_render_idle`
  (splash + dashboard + nav all painted; idle = every layer's frame complete) →
  `UiManager_EnableBacklight` (first lit frame = complete splash, dashboard finished behind the OVR2
  splash) → *(asset pre-load — none yet)* → min on-screen hold → `UiManager_RevealDashboard`
  (hide OVR2 → finished dashboard) → video go-live → self-delete.
  - *Earlier mistake (corrected):* the loader first suspended `LEGATO_Tasks` and drove `leUpdate`
    itself. That fought the scheduler and was wrong twice over — one `leUpdate(0)` only fills one
    scratch tile (the rest bail on the locked scratch), and calling `GFX_CANVAS_Task()` in a tight
    loop doesn't free it (the commit completes between real task runs). Letting the normal tasks run
    and pending on idle is both correct and simpler.
  - **Scratch buffer sized for a full-screen tile.** With the stock 512 KB scratch a full-screen
    32bpp surface is ~8 tiles, and a JPEG re-decodes per tile (~8×) → slow. Bumped
    `LE_SCRATCH_BUFFER_SIZE_KB` 512 → **4096** (MGS Graphics setting) so `maxScratchPixels` ≈
    1.05 M ≥ 1280×800 → one tile = whole screen, one decode, one render pass. Cost: a single 4 MB
    nocache scratch (`LE_SCRATCH_BUFFER_COUNT = 1`, widget buffer disabled) — fits the headroom; also
    makes the RGB565 dashboard one pass.
- **Open:** the splash stays attached to layer 2 after reveal (just hidden); the future modal dialog
  (also OVR2/layer 2) will need to detach it first.

## 11. Relationship to spec §4.5 / Q5

This answers spec **Q5** (Legato vs. custom UI) for the *presentation* layer: **Legato is the
renderer; a marvin `ui_manager` over GFX Canvas owns surface/layer composition and screen
orchestration.** Per-screen *authoring* stays in MGS (one Screen per panel); the compositor
assembles their trees onto hardware layers.
