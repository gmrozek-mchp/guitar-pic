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

**The pool size is not the ceiling it looks like.** `CONFIG_CANVAS_NUM_OBJ` only sizes the
static `GFXC_CANVAS canvas[]` array (`gfx_canvas.c:67`); `GFXC_Initialize` marks every unused
slot `CANVAS_ID_INVALID` / `GFXC_FX_IDLE`, so spare slots are inert at ~150 B of BSS each and
raising the number is an MCC regen and nothing more. `LE_LAYER_COUNT` is MGS-derived, and
`leState.layerList` is a dynamic `leList` rather than a fixed array — no ceiling there either.
What actually bounds the screen count is **`ram_nocache`** (`ddram.ld`, 32 MB): a full-screen
RGB565 surface is 1.95 MB, and with the eight layers below defined the region is ~30.8 MB used.
A ninth layer-screen therefore needs the `ram_nocache`/`ram` split moved (`.region_ram` has
~200 MB spare, so this is a one-line change) — *not* a bigger pool. The splash's 4 MB RGBA8888
buffer is the obvious reclaim if it is ever wanted instead.

### 0.1 Direction (decided 2026-06-29): adopt MGS's single-master-screen / layer-screen model

Per Microchip's GFX-canvas layer-screen guide¹, MGS is meant to be **one master screen whose
*layers* are the logical screens** ("layer-screens"), each auto-associated with its own canvas.
marvin **adopted this model on 2026-06-29** (confirmed on hardware). It previously *emulated*
it by hand — each panel a **separate 1-layer MGS Screen**, built on layer 0 then manually
disconnected and re-hosted by `ui_manager`, with `LE_LAYER_COUNT` pinned by a never-shown
`LayerBudget` screen. Now there is **one master screen named `Marvin`, one layer per panel**:
layer 0 dashboard, layer 1 nav, layer 2 song-select dialog, layer 3 song-select album-art strip
(the Legato layer ↔ HW-layer bindings are runtime and reassignable — see §16). MGS derives
`LE_LAYER_COUNT` (= **4**) and associates each layer-screen with `canvas[i]`, so the `LayerBudget`
hack and the disconnect/re-host dance are gone — `ui_manager` calls `screenInit_Marvin()` +
`screenShow_Marvin()` once (the latter builds the tree and attaches each root to its Legato
layer), then per-panel `*_Setup()` wiring, and binds canvases to HW layers at display time.
**The splash is separate and fully manual** (pre-Legato RGBA8888 scanout): it is *not* a
canvas at all — it owns a static framebuffer, reads a raw RGBA8888 blob straight from QSPI NOR
into it, and drives its XLCDC hardware layer directly via the PLIB
(`screens/splash/screen_splash.c` `ScreenSplash_Load`/`Show`/`Hide`, mirroring `video.c`'s HEO
setup), so the canvas pool is exactly the four layer-screens (0/1/2/3).

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

> **Current HW-layer map (supersedes the table above; see §16):** BASE = dashboard, HEO = video,
> **OVR1 = video frame overlay / song-select dialog / keyboard modal / system-screen board photo**,
> **OVR2 = nav drawer / album-art strip**.
> Z-order `OVR2 > OVR1 > HEO > BASE`. Nav moved to OVR2 so it sits *above* the OVR1 video frame
> (an open drawer covers the frame's left edge). The users of each overlay are time-exclusive:
> frame ⇎ dialog (frame hidden whenever video is), nav ⇎ album-art (nav closed during song-select),
> and the board photo ⇎ everything else on OVR1 (`UiManager_ShowSystem` drops the video *and* its
> frame; the dialog and keyboard are reachable only from the dashboard). The nav drawer is the one
> thing that *can* appear over the system screen, and it rides OVR2 above the photo — so it covers
> the photo's left edge, which is the wanted behaviour.

### 4.1 Two layer counts — don't conflate them
- **`LE_LAYER_COUNT`** (Legato, `legato_config.h:156`) = how many canvases / Legato layers the global
  `layerList` manages = **8** (the Marvin layer-screens, `CANVAS_*` in `ui_manager.h`):

  | Layer / canvas | Panel | Role | HW layer when shown |
  |---|---|---|---|
  | 0 | `PANEL_DASHBOARD` | dashboard (base view) | BASE |
  | 1 | `PANEL_NAVIGATION` | nav drawer | OVR2 |
  | 2 | `PANEL_SONG_SELECT` | song-select dialog | OVR1 |
  | 3 | `PANEL_SONG_SELECT_ALBUM_ART` | song-select cover strip (RGBA8888) | OVR2 |
  | 4 | `PANEL_WIIMOTES` | wiimotes / manual override (base view) | BASE |
  | 5 | `PANEL_KEYBOARD` | on-screen keyboard modal | OVR1 |
  | 6 | `PANEL_BUS` | 10BASE-T1S bus statistics (base view) | BASE |
  | 7 | `PANEL_SYSTEM` | system info / node showcase (base view) | BASE |

  MGS derives the count as the **max layer count across all screens** in the design; there is no
  explicit knob. (Adding a layer-screen therefore means adding a layer to the `Marvin` screen in
  `default_design.zip` — see the `add_layer.py` recipe in the `mgs-legato-design` skill — and then
  a Generate.)
- **`XLCDC_TOT_LAYERS`** (XLCDC driver, "Total Layers" in `le_gfx_driver_xlcdc.yml`) = enumerated
  hardware layers = **4** (`layerOrder` = {BASE 0, HEO 1, OVR1 2, OVR2 3}).

The eight canvases are mapped, at display time, onto the **three non-HEO** hardware layers
(BASE / OVR1 / OVR2); HEO is the live camera. Because the canvases are time-shared (four
mutually-exclusive base views on BASE, video frame ⇎ dialog on OVR1, nav ⇎ album-art on OVR2 —
see §16), never more than three are visible at once even though eight are defined.

**A base-view swap costs no drawing.** Legato renders into a canvas surface whether or not that
canvas is shown or bound to a hardware layer, so `paint_all_screens_once` (`ui_manager.c`) paints
all eight surfaces complete behind the splash and every later `bind_canvas` is a pure layer bind:
`gfxcSetLayer` + `gfxcShowCanvas` + `gfxcCanvasUpdate` + the colour-mode poke, and no repaint. This
is the property to reach for when a screen transition looks expensive — give the two states their
own canvases and the switch becomes free. It is also why each `Screen*_SetShown` gates only that
screen's *periodic work*, never a repaint.

### 4.2 The LayerBudget screen (obsolete)
> **Obsolete — removed on 2026-06-29.** This described the old per-screen-Factory design, where each
> panel was its own 1-layer MGS screen and a dedicated never-shown `LayerBudget` screen pinned
> `LE_LAYER_COUNT`. Under the single-master-`Marvin`-screen model (§0.1) MGS derives `LE_LAYER_COUNT`
> from the master screen's layer count directly, so the LayerBudget hack is gone.

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
  node_art.{c,h}                            board-photo cache for the system screen
  screens/<name>/   screen_<name>.{c,h}     one folder per panel (+ panel-specific helpers)
  widgets/<name>/   widget_<name>.{c,h}     reusable widgets, one folder each
  gfx/              <primitive>.{c,h}       raw-surface drawing primitives (no widget)
```

An asset cache belongs beside the screens that consume it, not in the domain modules: `node_art`
is keyed by **T1S PLCA node id** and read only by `screens/system`, so it lives here. `game_art`
stays in `game/` because its key *is* game data — `(setlist, index)` from the recognizer.

Current contents: `screens/dashboard/screen_dashboard`, `screens/video/screen_video`,
`screens/navigation/screen_navigation`, `screens/song_select/screen_song_select`,
`screens/splash/screen_splash`, `widgets/song_list/widget_song_list`,
`widgets/button_aa/widget_button_aa`.

`screen_video` is a screen module without its own canvas: the live video is on the
HEO hardware layer (compositor-owned), so this module owns only the *interaction* —
a tap handler on `Marvin_PANEL_DASHBOARD` that toggles the HEO window between the
windowed rect and fullscreen via `UiManager_VideoShow`. See §14.

**Naming convention:** a screen module's file is `screens/<name>/screen_<name>.{c,h}` and its
public functions are prefixed `Screen<Name>_` (PascalCase of the file basename) — e.g.
`screen_song_select.c` → `ScreenSongSelect_Setup`. This matches the project's `Module_Method`
style (`UiManager_*`, `ButtonAA_*`) and stays clear of the MGS-generated entry points
(lowercase `screenInit_Marvin`, `event_Marvin_*`). Each panel module exposes
`Screen<Name>_InitSurface()` (assign its canvas buffer, pre-scheduler) and `Screen<Name>_Setup()`
(set its canvas window + wire content/events on the widgets `screenShow_Marvin` already built);
`ui_manager` calls these — the modules never bind HW layers themselves. The splash is the one
exception: it is not a canvas, so it exposes `ScreenSplash_Load`/`Show`/`Hide` instead. Headers
are included from the `default/src` root, e.g. `#include "ui/screens/navigation/screen_navigation.h"`.
New panels add a `screens/<name>/` folder; new widgets a `widgets/<name>/` folder — each wired
into `user.cmake`
(no MCC involvement).

## 7. Build state & refactor plan

**Built (committed):** GFX Canvas substrate; state machine off + app-owned `screenInit/Show`
(`compat/le_gen_init.h` stub); `ui_manager` orchestrator + dashboard on BASE; `LE_LAYER_COUNT`=4
under the single-master-`Marvin`-screen model (the old `LayerBudget`-screen pin is retired — §4.2).
**Nav drawer is feature-complete:** the Marvin
master screen's layer-1 panel, bound to OVR2; slide in/out via canvas Move FX (Move FX
re-enabled; `NAVIGATION_CLOSED_X` dodges the window-clip row-wrap; mid-slide reversal cancels the
in-flight move); single-active highlight via a runtime-registered shared release sink; rounded
AA buttons (§10a). **Its widget tree is built in C, not authored in MGS** (§17).

**Song/mode-select is done too** (2026-08-05): the layer-2 dialog on OVR1 with its own layer-3
album-art strip on OVR2, both built in C (§17), the modal coexist verb exercised against a live
dashboard, and rounded dialog corners without per-pixel alpha (§18).

**Next:** **`manual_input.c` fate** — `Screen0` is retired, but the strum handlers now serve the
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
- **Splash→dashboard transition** is a hard cut: the splash owns OVR1 above BASE, and reveal simply
  disables OVR1 to uncover the dashboard already painted on BASE (§13 paints every surface before
  reveal, so there's no garbage frame — only an abrupt cut). Smooth later with a brief fade if wanted.
- **Splash backlight timing** — the raw QSPI splash is complete in its framebuffer the moment it's
  read (no decode), so the boot task lights the backlight right after `ScreenSplash_Show`; the
  render-idle wait (§13) then gates *reveal*, not the backlight.
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
- Wired from each screen's own button init (nav rows, wiimotes controls, song-select). Any
  radius works — `AaCorners_Render` (`ui/gfx/aa_corners.c`) computes coverage analytically
  per pixel rather than from a mask precomputed for one radius.

## 12. Boot splash (done)

A full-screen photo splash is the first thing on the panel: the boot task reads it out of QSPI NOR
into RAM, gets it onto its overlay layer, and only then lights the backlight, so the panel never
shows a pre-splash/garbage frame.

- **Splash on OVR1, drawn over the dashboard, no canvas.** The splash is *not* a GFX canvas — it
  owns a static full-screen RGBA8888 framebuffer (`s_fb[BASE_W*BASE_H]` in `.region_nocache`) and
  drives its XLCDC hardware layer directly via the PLIB, mirroring `video.c`'s HEO setup.
  `ScreenSplash_Show(layer)` sets the layer to `RGBA_8888`, points it at `s_fb`, opaque (alpha 255,
  DMA on), full-screen, zero stride, then enables — all deferred writes latched together at vsync.
  It shows on **`SPLASH_HW_LAYER` = OVR1** (`ui_manager.h:39`), above BASE, so it covers the BASE
  dashboard while the dashboard paints underneath; `ScreenSplash_Hide` disables OVR1 to reveal the
  finished dashboard.
- **Raw RGBA8888 from QSPI NOR — no decode.** `ScreenSplash_Load` opens the SST26 QSPI driver and
  `DRV_SST26_Read(h, s_fb, sizeof s_fb, QSPI_SPLASH_OFFSET)`s the raw pixel blob straight into the
  scanout framebuffer (the read sets up the QSPI memory-read frame and completes synchronously; the
  status poll is a bounded backstop). There is **no SD mount and no image decode** — the blob is
  pre-provisioned into QSPI (see `openocd/program-qspi.sh`). On any failure the buffer is filled with
  opaque black (`SPLASH_FILL = 0x000000FF`) as a fallback.
- **Backlight — PWM, not GPIO.** The backlight is PWM-dimmed on PC18 (PWM channel 0, configured by
  MCC); `XLCDC_EnableBacklight()` is **not** used. The channel is left stopped (idles low = dark,
  `CPOL_LOW`) so the panel stays dark until the boot task calls `enable_backlight()` after the splash
  is shown — it writes the persisted brightness (`Settings_Get()->backlight_pct`, restored from the
  QSPI settings ring) via `UiManager_SetBacklight` then `PWM_ChannelsStart(PWM_CHANNEL_0_MASK)`. See
  [`display_path.md`](display_path.md) §4.1. Camera go-live (`Video_CaptureEnable`) also runs from the
  boot task, after reveal, so the camera doesn't pop in over the splash.
- **Asset:** provisioned into QSPI NOR at `QSPI_SPLASH_OFFSET` (raw 1280×800 RGBA8888) by
  `openocd/program-qspi.sh`.

### 12.1 Boot progress bar (self-calibrating) — `splash_progress.c`

A bottom-anchored progress bar over the splash art: a monospace stage label on the left, a
percentage on the right, an 8 px rounded capsule with a cyan gradient fill. Geometry is the mockup's
(`MarvinSplash` `App.tsx`) resolved at 1280×800 — bar `x 64..1216`, `y 752..759`, text row top
`y 728`, gradient `#0E7490 → #22D3EE`, label `zinc-400`.

- **Drawn into the splash framebuffer, not through Legato.** The splash is up long before any screen
  exists, so there is no canvas and no paint pass to draw in. `splash_progress.c` composites straight
  into `s_fb` (exposed by `ScreenSplash_Framebuffer()`) — it is `.region_nocache`, so a CPU write is
  on screen at the next scan-out with no cache maintenance.
- **Text comes from the Legato font ASSETS, which work with the stack down.**
  `leFont_GetGlyphInfo()` is a pure lookup in a `const` glyph table — no renderer, no globals, no
  init — and an 8bpp antialias glyph's coverage bitmap is directly addressable at
  `font->base.header.address + glyph->dataOffset` (`dataRowWidth` bytes per row, one alpha byte per
  pixel), placed at `x + bearingX`, `top + (baseline − bearingY)`. `ui/gfx/glyph_blit.h` wraps that
  into "draw an ASCII string into a raw RGBA8888 buffer". What is **not** usable is
  `leFont_DrawGlyph()` / the string renderer: they blit through `leRenderer_BlendPixel()`, which needs
  an active paint pass — the same constraint that produced `AaCorners_RenderSurface565` (§18). The
  font is `DejaVuSansMono_12`, already linked, matching the mockup's `font-mono text-xs`.
- **A Legato overlay was the wrong tool, on timing.** A layer-screen would need an MGS design change
  and — fatally — cannot paint until `init_screens()` runs, which is two thirds of the way through
  boot, *after* the album-art decode. It cannot cover the phase that most needs a bar.
- **Own ticker task, because there are no software timers.** `configUSE_TIMERS` is 0 in the
  MCC-owned FreeRTOSConfig, and the boot task blocks in `GameArt_LoadAll` / `init_screens` /
  `wait_render_idle`, so the bar runs on a static 25 Hz task at **priority 3** (above the boot task's
  2). It repaints only when the fill's pixel width or a string actually changes, and
  `SplashProgress_Complete()` stops it and waits for it to exit, so the framebuffer is back to a
  single writer before the final 100% draw and the fade.
- **Two backdrop copies, so nothing double-blends.** The pristine art for the band (`y 724..763`)
  and the rendered track are kept in plain cached RAM (~220 KB, `ram` not `.region_nocache`): text
  erases to the art, the fill — including its antialiased leading cap — draws onto the track. Every
  repaint is therefore idempotent, and the capsule's AA corners sit on the photo rather than on a
  black box. Capsule coverage comes from a rounded-rect distance field (float, as `aa_corners.c`);
  with height 8 and radius 4 the straight section is fully covered at every row, so the field is only
  evaluated in the two 4-px cap boxes.
- **The milestones are measured, not apportioned by hand.** Each stage owns the share of the bar
  that its duration *on the previous boot* was of the whole:
  `mark[i] = cum_ms[i] × 990 / total`, and within a stage the bar interpolates on
  `elapsed_in_stage / predicted_stage_ms`. So the bar's *speed* tracks what each stage actually
  costs instead of being uniform in time; a stage that finishes early hands the bar straight to the
  next mark, one that runs long eases to its own ceiling and waits there. Monotone by construction
  (marks only increase, and a stage starts where the previous one ended); only `_Complete()` writes
  100%. Stages: `INITIALIZING SYSTEM` (services spawning) · `LOADING ARTWORK` ·
  `BUILDING INTERFACE` · `RENDERING SCREENS` · `READY`.
- **The profile lives in the QSPI settings ring** as `settings_t.boot_stage_ms[4]` (record v3) —
  one duration per *work* stage, written after the reveal when any stage has drifted more than
  250 ms, so a steady system stops touching flash after the first boot. All-zero means nothing has
  been measured yet and a compiled seed profile is used; the seed is the only guessed number left,
  and one boot replaces it. `SplashProgress_SetStage` logs `stage → elapsed (predicted)` and
  `_Calibrate` logs the whole measured-vs-stored table, so the grounding is inspectable from a boot
  log rather than inferred.
- **Drawing into live scanout has two disciplines, both learned the hard way** (glitches on
  hardware, 2026-08-06). The splash framebuffer is being scanned out *while* the bar draws into
  it — there is no back buffer and no vsync gate — so:
  1. **Never erase-then-draw. Compose off-screen, then blit once.** Two passes over rows the
     beam is crossing means a frame caught between them shows the *intermediate* state; for text
     that is a visible blank. `draw_field` composites art + glyphs in cached scratch and pushes
     the finished strip out in one pass, so a pixel only ever goes old-final → new-final. Both
     the write and the beam run top-to-bottom, so the worst artifact is old-above/new-below.
  2. **Count transactions, not bytes.** `.region_nocache` is strongly-ordered: writes are
     neither buffered nor coalesced, so each pixel is its own DDR transaction, serialized
     against the LCDC's own layer reads (~360 MB/s with the RGBA8888 splash on OVR1 and the
     RGB565 dashboard on BASE) and, late in boot, the 2D engine blitting every screen canvas.
     Redrawing the whole fill each tick (~7200 px) was enough to tip that into occasional
     scanout underrun — a whole-frame shift. `draw_fill` is incremental (~48 px/tick, 150×
     fewer) because only the leading cap's width of columns depends on the current width.
- **Notes say what the stage is doing, without touching the bar.**
  `SplashProgress_SetNote(note, done, total)` replaces the stage label with e.g.
  `LOADING ALBUM ART 42/70`; `SetStage` clears it. The art caches take a registered
  progress callback (`GameArt_SetProgressCallback` / `NodeArt_SetProgressCallback`, the same
  idiom as `Video_SetFrameLatchCallback`) and report their own tier name and file count —
  `ui_manager` owns the wording, so `game/` stays free of UI vocabulary. The card is also
  mounted explicitly from the boot task (`Storage_Mount` is idempotent) so the mount wait
  gets its own note instead of hiding inside the artwork stage.
  **Deliberately display-only: the counter does not drive the bar.** A progress bar should
  be linear in *time*, and work inside a stage is not uniform per item — a 508×208 PNG cover
  costs more than a 144×144 JPEG, so a count-linear bar would move at two different speeds
  against the clock. The measured profile is accurate to ~0.05% over an 18 s stage, which is
  far better than counting items would be.
- **The last stage is slack, and must not be stored as a cost.** `READY` waits out
  `SPLASH_MIN_MS`, so its duration is the remainder (`max(Σwork, min_hold) − Σwork`, 0 when the work
  already exceeded the hold). Recording it as a measured cost would invert the calibration: speeding
  the work up *lengthens* the idle wait, which would then train the bar to crawl through the stages
  that do the work.
- **Reveal is a cross-dissolve, not a cut.** OVR1's blender is `SFACTC = A0·As` / `DFACTC = 1−A0·As`
  and splash pixels are opaque, so `ScreenSplash_FadeOut()` ramping the layer's global alpha 255→0
  dissolves into the already-painted BASE dashboard at zero extra pixel cost. The ramp needs no
  delay of its own: `XLCDC_SetLayerOpts(..., update=true)` ends in `XLCDC_UpdateLayerAttributes`,
  which spins until the LCDC latches at the next vsync, so one step **is** one frame and the step
  count is just `ms / 17`. It restores A0 to 255 after disabling the layer so OVR1's next user — the
  AA video frame overlay (§16) — cannot inherit a transparent layer.

## 13. Pre-rendered, persistent per-screen canvases (done — code-complete, pending hardware test)

Fixes the reveal-before-paint flash (§10): a screen used to be shown while Legato incrementally
painted its freshly-rebuilt tree, so unpainted regions flashed uninitialized-DRAM noise (~1–2 s for
the Dashboard's ~200-widget tree). The model is now the §1 ideal — **paint into an off-screen
surface, then reveal**:

- **Every layer-screen is persistent + owns a canvas.** The Marvin master screen's layer-screens are
  persistent (built once, never torn down), each on its own Legato layer → canvas: Dashboard 0
  (RGB565), Nav 1 (RGB565), song-select dialog 2 (RGB565), song-select album-art 3 (RGBA8888).
  `CONFIG_CANVAS_NUM_OBJ` (8, expandable) gives room per screen; no `gfxcSetBaseCanvasID`
  multiplexing needed. Canvas → HW-layer binding is runtime and reassignable (dashboard rides BASE;
  nav OVR2; the dialog OVR1 + its album-art strip OVR2, bound on open — see §16). The splash is
  *not* a canvas — it drives OVR1 directly (§12).
- **Build pre-scheduler, then let the render task paint; the loader just waits.** All screens are
  built + hosted on their layers in `UiManager_Initialize` (pre-scheduler → no concurrency). Once
  the scheduler is up, the normal `LEGATO_Tasks` + `GFX_CANVAS_Task` pair paints them — a full-screen
  surface spans several scratch tiles (`LE_SCRATCH_BUFFER_COUNT = 1`, 512 KB ⇒ ~131 072 px @ 32bpp),
  and the scratch is freed by the canvas commit *between* render passes, which only happens when
  those tasks actually run. So the loader does **not** drive `leUpdate` or touch the scheduler — it
  yields and polls the public `leRenderer_IsIdle()`. Important: `leRenderer_IsIdle()` is just
  `frameState == LE_FRAME_READY`, which is **also true in the gaps between `leUpdate` calls** (which
  tick only every ~10 ms), so a single sample reads "done" mid-paint; `wait_render_idle` requires
  idle to hold **continuously for ≥120 ms** before trusting it. Boot sequence (`ui_boot_task`):
  `ScreenSplash_Load` (raw RGBA8888 from QSPI) → `ScreenSplash_Show` (OVR1) → `enable_backlight`
  (the splash framebuffer is already complete the moment it's read, so lighting it here is safe) →
  splash-shown callback (app starts services + camera in parallel) → `Art_LoadAll` (album-art
  decode) → build screens (`init_screens`) → bind dashboard canvas to BASE → `paint_all_screens_once`
  → `wait_render_idle` (all layer-screens painted behind the splash) → min on-screen hold →
  `ScreenSplash_Hide` (disable OVR1 → finished dashboard) → `ScreenVideo_ShowWindowed` → `Video_CaptureEnable`
  → arm health monitor → self-delete.
  - *Earlier mistake (corrected):* the loader first suspended `LEGATO_Tasks` and drove `leUpdate`
    itself. That fought the scheduler and was wrong twice over — one `leUpdate(0)` only fills one
    scratch tile (the rest bail on the locked scratch), and calling `GFX_CANVAS_Task()` in a tight
    loop doesn't free it (the commit completes between real task runs). Letting the normal tasks run
    and pending on idle is both correct and simpler.
  - **Scratch buffer sized for a full-screen tile.** With the stock 512 KB scratch a full-screen
    32bpp surface spans ~8 scratch tiles → multiple render passes. Bumped `LE_SCRATCH_BUFFER_SIZE_KB`
    512 → **4096** (MGS Graphics setting) so `maxScratchPixels` ≈ 1.05 M ≥ 1280×800 → one tile = whole
    screen, one render pass. Cost: a single 4 MB nocache scratch (`LE_SCRATCH_BUFFER_COUNT = 1`, widget
    buffer disabled) — fits the headroom; makes the RGB565 canvases one pass too.
- **Open:** the splash owns OVR1 while shown (disabled after reveal); the song-select dialog also
  binds a canvas onto OVR1 (§16), so opening it re-binds OVR1 from the splash/video-frame overlay.

## 14. Tap-to-fullscreen live video (`screen_video`)

Tap the live video to blow it up to fullscreen; tap again to shrink it back to the
windowed rect. The video is on **HEO**, which Legato cannot pick, so the tap is
caught on a Legato widget and the interaction is expressed purely as HEO geometry:

- **Catch on `Marvin_PANEL_DASHBOARD`.** It's the full-screen (1280×800) pickable
  layer-0 panel under the IGNOREPICK/IGNOREEVENTS `root0`. Legato **bubbles** a
  touch up the parent chain until a widget calls `leWidgetEvent_Accept`
  (`legato_input.c`), and plain panels (VIDEO_STREAM, BASE_*) never accept — so a
  tap on the video region bubbles up to the dashboard panel. `screen_video`
  re-points that panel's `touchDownEvent` (the shared-vtable-copy idiom from
  `screen_dashboard.c`'s header tap).
- **Windowed → fullscreen:** a tap whose (x,y) is inside the windowed video rect
  calls `UiManager_VideoShow(0,0,BASE_W,BASE_H)`. The HEO bilinear/bicubic scaler
  stretches the source to fill (stretch-to-fit; slight aspect change vs the window).
  `base_discard_reconcile` follows the video rect, so BASE DMA is discarded across
  the whole panel while fullscreen — a free bandwidth win.
- **Fullscreen → windowed:** while fullscreen, `screen_video` gates picking off on
  `Marvin_PANEL_BASE_TOP` + `Marvin_PANEL_BASE_BOTTOM` (the two subtrees holding
  every interactive dashboard widget) by clearing `LE_WIDGET_ENABLED` — flag-only,
  no repaint, same gate as `ui_manager`'s `panel_set_pickable`. So **every** tap
  then resolves to the dashboard panel and exits fullscreen, restoring the windowed
  rect and re-enabling input.
- **Windowed state is re-established** by `ScreenVideo_ShowWindowed()`, which
  `ui_manager` also calls at reveal and on song-select close, so the video always
  returns to a known windowed state.

## 15. Active-picture detection + HEO source-crop

The Wii's component→HDMI conversion frames the active raster with dead black bars
(measured ≈ **716×448 @ (0,15)** in a 720×480 capture — top 15 / bottom 17 / right
4 / left 0), and the thickness varies by console/converter, so it's **detected at
runtime**, not hardcoded. The bars aren't black=0 — they sit at the same ~13
pedestal as in-video black — so detection keys on a **fixed luma threshold**
(`max(B,G,R) > 40`, well above the ≤14 border and below the ≥145 content), not on
zero.

- **Detector lives in `video.c`** (`Video_GetActiveRect`): it owns the frame,
  format, and ring, and the capture buffer is in `.region_nocache` so CPU reads
  are coherent. It scans inward from each edge (bounded to the deepest border the
  ≥700×420 min-size gate allows, so a dark loading frame stays cheap), unions the
  bright bounds over 16 *bright* frames (dark frames find no content and are
  skipped — a loading screen at capture start just defers the lock), then locks if
  the union clears 700×420. One-shot per capture arm; reset on re-arm/source-size
  change; **full-frame fallback** (`x=y=0`, `w/h`=full) until locked.
- **`ui_manager` `heo_bind` consumes it as the HEO source crop:** offset the frame
  base by `y*stride + x*bpp`, size `HEOCFG4` (source memory rect) to the active
  w/h, and skip the cropped columns each line via `XSTRIDE=(src_w-active_w)*bpp`
  (same formula the GFX driver uses, `FB_TYPE_SZ*(resx-sizex)`). The bicubic
  scaler then maps the active rect onto the window/fullscreen dst — the console's
  dead bars never reach the panel. `heo_frame_latch` (ISC IRQ) adds the same crop
  offset per ring slot; `heo_reconcile` rebinds when the rect changes (detection
  flipping full→cropped). Applies to both windowed and fullscreen; bonus: active
  716×448 ≈ 1.60 = 1280×800, so fullscreen is essentially aspect-correct.

### 15.1 HEO video levels expansion (gamma CLUT)

The source is mildly range-compressed against the panel's full 0..255 — measured
black cluster ~5 (the dead-border pedestal ~13 is cropped out by §15), white ceiling
~233. The HEO **CBHS** limited-range black-level block is **YCbCr-only** (SAM9X7 DS
§6.2.6.7), so it can't touch our RGB888 HEO input. The HEO's **per-component gamma
CLUT** *does* operate on true RGB (`HEOCFG1.GAM`, four 256-entry LUTs), so `ui_manager`
loads it with a linear levels-expansion curve (`out = clamp((in−5)·255/228)`;
`VIDEO_LEVELS_BLACK/WHITE`) — deepening near-black to true black and lifting whites.

- **Display-only:** the DDR capture is untouched, so the detector/gameplay read raw
  pixels; the expansion is applied by the LCDC at scanout (zero runtime cost).
- **Load timing:** the CLUT (`LCDC_HEOCLUT[256]`) must be written with `GAM`/`CLUTEN`
  clear, so it's loaded in `UiManager_Initialize` (post `XLCDC_SetupHEOLayer`, pre any
  bind). `XLCDC_SetLayerRGBColorMode` rewrites `HEOCFG1` with `GAM(0)` each bind, so
  `heo_bind` re-asserts `GAM` after it.
- **Fixed curve.** The ~5/233 pedestal/ceiling may vary by Wii/converter — retune the
  `VIDEO_LEVELS_*` constants (same per-hardware caveat as §15's detection).

## 16. Rounded AA video frame (OVR1 overlay)

A 1px rounded frame with anti-aliased corners around the windowed video, matching
the dashboard panels (`0x404040` = `SCHEME_PANEL` `LE_SCHM_SHADOWDARK`, radius 6).
The video is on HEO (a hard rectangle, not a canvas), so `PanelAA`'s backdrop-blend
can't reach it; instead a per-pixel-alpha overlay *above* HEO composites the frame.

- **Direct-driven ARGB_4444 layer on OVR1.** The GFX canvas framework has no 4444
  mode (`gfxColorMode` = 5551/8888 only), so the frame is a static framebuffer
  driven straight through the XLCDC PLIB (splash/HEO-style), not a canvas.
  ARGB_4444's 16 alpha levels give smooth AA at half the bandwidth of 8888
  (ARGB_1555's 1 alpha bit cannot AA). Register writes live in `ui_manager`
  (`UiManager_VideoOverlayShow/Hide`); the buffer + fill live in `screen_video.c`.
- **Fill** (once, from a rounded-rect SDF): corner-cut = opaque black (the dashboard
  backdrop, so the video's square corners read as rounded), a 1px stroke, and a
  transparent interior (video shows through). Alpha AA on the stroke→video edge is
  what smooths the corner. `alpha = 1 − innerCoverage`; opaque colour = stroke
  scaled by its share of the opaque part (remainder is the black cut).
- **Nav moved OVR1 → OVR2** so the drawer sits above the frame and covers its left
  edge when open (z-order `OVR2 > OVR1 > HEO > BASE`). Nav binds via
  `UiManager_ShowNavLayer` (`bind_canvas` → RGB565, re-poked each open because
  album-art leaves OVR2 in RGBA8888).
- **Lifecycle = windowed video visible:** shown by `ScreenVideo_ShowWindowed`,
  hidden on fullscreen enter (edge-to-edge) and song-select open (video hidden).
- **Bandwidth:** an always-on full-rect OVR1 read while windowed (~720×480
  ARGB_4444 ≈ 41 MB/s) on top of BASE+HEO — watch for CSI-2 D-PHY capture stalls.

### 16.1 Future: per-frame gameplay overlay on this layer

This OVR1 overlay (above HEO, aligned to the video window) is also the natural home
for a **frame-by-frame gameplay HUD** drawn over the live video — note-highway
markers, hit/miss annotations, detected-note boxes, timing cues, etc. It would
compose with (or replace) the static frame in the same surface.

Design implications when we build it:
- **Direct buffer draw, no Legato.** Same as the frame: the canvas framework can't
  own this layer (no ARGB_4444 mode, and per-frame Legato repaint is too heavy), so
  the HUD is rasterised straight into the overlay framebuffer (CPU and/or the GFX2D
  engine) and the LCDC composites it. Must be **fast / minimal overhead** — it runs
  every gameplay tick, so favour incremental redraw (clear+draw only changed
  regions) over full-surface clears.
- **Double-buffer to avoid tearing.** The static frame is written once so a single
  buffer is fine; a per-frame HUD redrawn while the LCDC scans it would tear. Use
  two overlay buffers and flip the layer base address at vsync (mirrors the HEO
  ring / `heo_frame_latch` non-blocking address update).
- **Format.** ARGB_4444 keeps bandwidth down and is enough for solid markers/AA
  edges; bump to ARGB_8888 only if the HUD needs finer gradients.
- **Geometry.** Overlay coords map 1:1 to the *displayed* video window; if the HUD
  is derived from source-pixel positions, apply the active-area crop + scale
  (§15) to place markers correctly on the scaled video.

## 17. Panels built in C, not authored in MGS

**Every layer-screen is now a programmatic builder over an empty MGS root panel** — bus stats,
wiimotes, the on-screen keyboard, the nav drawer, the dashboard, and (2026-08-05) the
**song-select dialog**, which was the last. The design is now **assets only**: seven layers, seven
bare panels, zero authored widgets. The design supplies one bare panel per layer-screen —
position, size, an opaque scheme — and the module builds every child in `Screen<Name>_Setup()`
with the in-place constructors (`leWidget_Constructor`, `leButtonWidget_Constructor`,
`leLabelWidget_Constructor`), storing widgets in file-scope arrays (no Legato pool, no
`LE_MALLOC`). `screen_bus.c` is the reference; `ui/titlebar.c` is the shared-component flavour of
the same idiom. What remains authored in MGS is the *asset* layer — schemes, strings, fonts,
images — which is exactly what MGS is good at.

Why, for the drawer specifically: its row set has to track *which screens exist*, which is a
code fact, not a design fact — `NAV_ENTRY[]` in `screen_navigation.c` is one line per row
(caption `stringID`, icon pair, base-view verb), so adding a screen adds a row. The design keeps
the unused icon pairs and captions for the rows not built yet.

Three rules this style has to respect, all learned the hard way:

- **Captions come from the design string table** (`leTableString_Constructor(&s, stringID_X)`),
  never C literals: MGS only auto-includes glyphs for strings it can see in the design, and a
  design string keeps its per-language values. A caption with *two states* (PLAYING/IDLE,
  START/STOP) is two table strings with the label re-pointed — not a runtime literal. Text that
  is genuinely *data* (a song title, a score) is a `leFixedString`, and any non-ASCII character
  it can contain must be declared in the font's range (see the `mgs-legato-design` skill).
- **Every appearance change needs an explicit `invalidate()`.** Image setters raise no damage and
  `setScheme`'s damage doesn't cover the icon rect; the leftover pixels were the 2026-08-05 bug
  (stale fill, or unzeroed `.region_nocache` DDR).
- **Round a card with a frame overlay added LAST — and set `LE_WIDGET_IGNOREPICK` on it.** A
  full-card panel with `SCHEME_BACKGROUND` + `BORDER_LINE` + radius + `PanelAA_EnableRoundImage`
  draws the rounded border and then eats the corners back to the page black. Rounding the filled
  card instead eats its corners back to its *own* fill (the radius never shows), and an image
  flush to the card's edge would overwrite the arc anyway — the overlay is on top of it.
  **The catch:** `leUtils_PickFromWidget` keeps the *last* child whose rect contains the point, so
  "paints on top" and "wins the touch" are the same property. Without `IGNOREPICK` the overlay
  swallows every touch inside its card — which is exactly what happened on the dashboard's first
  hardware run (only the titlebar hamburger, which has no overlay, responded). Any decorative
  widget stacked over interactive ones needs the flag.

The MGS-side edit is mechanical: `strip_subtree.py <zip> <PANEL_NAME>` deletes the imported
children and keeps the panel. Since the root panel's fill is what `PanelAA`/`ButtonAA` sample as
the backdrop behind a rounded child, it must stay **opaque** (`BACKGROUND_FILL`).

### 17.1 Widget TYPES are enabled by the design, not by the code

`legato_config.h`'s `LE_<TYPE>_WIDGET_ENABLED` flags are derived by MGS from the widget types the
**design** instantiates. A type used only by hand-built code therefore vanishes — `leXWidget`
becomes an unknown type name — the moment the last design widget of that type is deleted. This
bit us on 2026-08-05: stripping the dashboard removed the design's only progress bars and its only
gradient, and the next Generate broke a build whose C had not changed.

Consequences for this style of screen:

- **Prefer a plain `leWidget` plus a paint override** to a specialised widget type. We already own
  the paint for everything non-trivial (`PanelAA`, `ButtonAA`, `ui/widgets/bar`, `widget_fret`,
  `widget_gauge`, …), and a plain widget is always available. `ui/widgets/bar` exists precisely
  because the stock progress bar contributed nothing but a square track and a vtable to hijack.
- **What the code genuinely needs must be forced on** in the Legato component rather than left to
  the design's whim. `leButtonWidget` / `leLabelWidget` / `leImageWidget` were alive *only* because
  song-select was still MGS-authored; they are now **pinned**, which is what made stripping it
  safe. With the design holding no widgets at all, every type the firmware uses is on loan from a
  pin — there is nothing left for the design to keep them alive. The tell that a pin is real:
  `LE_CIRCULARGAUGE_WIDGET_ENABLED` reads 1 with no design instance anywhere.
- **The failure is a compile error, not a silent one** — which is the one mercy here. Grep the
  firmware for `\ble[A-Z][A-Za-z]*Widget\b` to get the true list of types it depends on.

## 18. Rounded corners on an opaque overlay, by copying the layer below

**The modal look** — a rounded, 1px-bordered zinc-900 card floating over the dimmed base view —
is shared by both modals (song-select and the on-screen keyboard) off `MODAL_R` /
`MODAL_SCRIM_PCT` in `ui_manager.h`. The border and radius are ordinary widget style on each
modal's root panel; the *corners* are the hard part.

A modal canvas is an **opaque RGB565 canvas on OVR1** — RGB565 has no alpha, so its corners
cannot reveal the base view on BASE the way §16's ARGB_4444 video frame reveals HEO. Three ways
out; we took the third.

1. Make the dialog canvas RGBA8888 with transparent AA'd corners. Correct, but 2.9 MB and ~174
   MB/s of OVR1 read while open (vs 87), in the exact bandwidth region that has knocked CSI-2
   out of lock before.
2. Cut the corners to a fixed colour, as the video frame does. Wrong here — the dialog floats
   over dashboard *content*, so a black cut reads as four black notches.
3. **Copy the pixels the base view has at those coordinates.** The corner boxes are filled with
   `base_surface[(y0 + y) * stride + x0 + x]`, anti-aliased against the modal's own fill and 1px
   border. 4 × 12 × 12 px of work, no format change, no extra layer.

What makes it work is that both surfaces are **CPU-readable RGB565 statics** and `UiSurface_Get`
already records every canvas's base pointer and geometry — so an overlay can read what is
underneath it even though they are different hardware layers.
`AaCorners_RenderSurface565` (`ui/gfx/aa_corners.c`) takes a per-pixel backdrop **sampler** and
writes the surface directly rather than through `leRenderer`, so it runs outside a paint pass and
the caller picks the moment.

The pass itself is **`UiManager_CutModalCorners(canvas)`**, a compositor verb rather than a screen
function, because every input it needs is compositor knowledge: which canvas is the base view
(`UiManager_BaseCanvas()`, so it tracks whichever is up), where the modal's window sits
(`gfxcGetWindowPosition` — the same single source of that geometry the BASE discard uses), and how
dim the scrim is. A screen passes its canvas id and nothing else; both modals share one
implementation. Called on **open**, after requesting the scrim and before binding the canvas.

Three properties worth knowing before reusing this:

- **It is a snapshot.** Live content moving under a corner goes stale until the next open. Fine
  here (page/card background); check before applying it elsewhere.
- **Anything that repaints over a corner undoes it.** The song list had to stop `MODAL_R` short of
  the dialog's bottom edge for exactly this reason — its row separators and selected-row fill
  span its full width, so a list reaching the bottom would repaint the corner on every scroll.
  Keep repainting widgets out of the arc boxes, or re-run the pass after they paint.
- **The canvas window's X must be 4-aligned, or the corner cut samples the wrong place and a
  black column appears.** The framework floors a non-32bpp window's X to a multiple of 4 and
  leaves the width alone, while `gfxcGetWindowPosition` still reports the unaligned request — so
  the discard rect, the sample offset and any overlay placed in panel coords all end up
  disagreeing with where the canvas really is. Place modal canvases through `CANVAS_X_ALIGN`
  (`ui_manager.h`). This produced a visible 2px black bar on both modals and a 2px offset of the
  album-art strip; see the 2026-08-06 journal entry.
- **It composes with the BASE-discard optimisation.** The dialog still discards BASE DMA behind
  itself while open (bandwidth), and that is safe precisely because the corner pixels were
  *copied* — nothing needs BASE to be scanned there.

### 18.1 The modal scrim — a full-screen dim that costs nothing

The mockup dims everything behind a modal (`bg-black/75`). Doing that with a surface would cost a
full-screen buffer plus the DDR bandwidth to scan it every frame — the contention that has knocked
CSI-2 out of lock before, and the reason this was skipped at first. It turns out to be **free**,
from two LCDC properties:

- **`HEOCFG12.DMA = 0` makes the layer take its pixel from the default-colour register** instead
  of memory, and HEO's `HEOCFG9` carries a real **alpha** there (`ADEF`) alongside `RDEF`/`GDEF`/
  `BDEF`. So the layer emits one constant ARGB pixel with **no buffer, no DMA, no bandwidth**.
  OVR1/OVR2 have `ADEF` too, but the datasheet marks theirs *"only for post-processing usage"* —
  this is HEO's trick specifically.
- **MCC already configures HEO's blender as straight src-over on source alpha** (`SFACTC = A0*As`,
  `DFACTC = 1-(A0*As)`, `A0 = 255`), so the composite is exactly
  `out = black·ADEF/255 + dst·(1 − ADEF/255)`. `ADEF = 191` is `bg-black/75`.

HEO sits **below OVR1 and above BASE** (`VIDPRI = 0`) — precisely where a scrim belongs: the base
view dims, the modal on OVR1 stays full strength. And it is available whenever the video is
hidden, which every modal already does.

`heo_scrim_bind` does the register work; `UiManager_ScrimShow(pct)` / `_ScrimHide()` are intent
only, applied by the video task's `heo_reconcile`, so **HEO stays single-writer**. That reconcile
is now three-state — **video / scrim / off**, in that priority — and acts only when leaving a video
bind or when the level changes, since each layer `update` busy-waits a vsync. `MODAL_SCRIM_PCT` in
`ui_manager.h` is the shared level, used by **both modals** — song-select and the on-screen
keyboard. The **nav drawer cannot** use this: it does not hide the video, so HEO is busy.

Three couplings worth knowing:

- **`heo_bind` must re-assert `DMA = 1`.** It never touched `HEOCFG12` before, relying on
  `DRV_XLCDC_Initialize` having left DMA set. Once a scrim clears it, a later video bind would
  scan out the default colour instead of the capture — a silent, confusing regression. `heo_bind`
  now calls `XLCDC_SetLayerOpts(HEO, 255, true, false)` and is self-sufficient.
- **The dialog's sampled corners must be dimmed to match** (§18). They hold a *copy* of the base
  view, but what the panel shows around them is the base view *dimmed*; an undimmed copy reads as
  four bright notches. `scrim_dim565` applies the same factor the blender does.
- **BASE discard is unaffected.** BASE is still read outside the dialog (that is what gets
  dimmed) and still discarded behind the opaque dialog rect.

**Confirmed on hardware** (2026-08-06), including the corner dim. Still unknown, and worth knowing
if a partial dim is ever wanted: whether the layer's window geometry (`HEOCFG2`/`3`) is honoured
with DMA off — we program it full-screen either way, so it cannot be told apart from here.

### 18.2 What each state costs the memory bus

Two independent savings, often confused: **BASE discard stops reads**, a `SetShown` gate stops
**writes**.

- **`BASECFG4.DISCEN` + `BASECFG5/6`** give the LCDC one BASE discard window: BASE skips its DDR
  *read* where an opaque layer fully covers it. `base_discard_reconcile` owns it (single DISCEN
  writer, video-task ctx) and picks the region each tick — **video rect** while HEO is bound, else
  the **open modal's rect**, else the **system screen's board-photo rect** while that overlay is up,
  else none. Fullscreen video therefore discards the *whole panel*; BASE
  stays enabled, which is the point — nothing needs disabling.

  | state | discard rect | BASE read skipped (RGB565 @60 Hz) |
  |---|---|---|
  | fullscreen video | 1280×800 | 122.9 MB/s |
  | song-select dialog | 1100×660 | 87.1 MB/s |
  | keyboard modal | 1060×560 | 71.2 MB/s |
  | windowed video | 720×480 | 41.5 MB/s |
  | system-screen board photo | 288×620 | 21.4 MB/s |

  The modal rect is read from that modal's **canvas window**, not from constants here, so the
  geometry has one owner — the screen module that lays it out.

  The board photo qualifies only because it is opaque over its **whole** rect: its rounded frame is
  baked into the asset as opaque page-black corners rather than left as alpha, so no part of the rect
  still needs BASE. An alpha-rounded photo would have forced an inset discard instead — see §19.
- **`Screen<Name>_SetShown`** stops the *other* half: repainting a surface nobody scans out costs
  CPU and DDR **writes**, which no discard can help. The dashboard's gate lives in
  `dashboard_feed.c` (its sole writer) and is **deferring, not dropping** — events keep coalescing
  while hidden and the show flushes what changed, with a wake event to unblock the consumer.
  **Only full occlusion gates:** a modal leaves the dashboard visible around it, so telemetry must
  keep flowing behind it — that is §5's coexist premise.

All of this is by-construction: no bus monitor is wired up, so none of these figures has been
observed as actual DDR traffic.

If a *non-uniform* scrim is ever wanted (a hole, a gradient), the fallback is a CLUT-mode layer:
CLUT entries carry 8-bit alpha (`ACLUT`) and `CLUTMODE` goes down to **1 bpp**, so a binary mask is
125 KB and ~7.7 MB/s. Note `CLUTEN` conflicts with the gamma CLUT we use on HEO for video levels
(§15.1), so that path would have to swap around the video rather than coexist with it.

## 19. Full-colour board photo on OVR1 (done — confirmed on hardware)

The system screen's photo column escapes the canvas's RGB565: the decoded photo is scanned out
**directly from its `ui/node_art.c` slot** as an opaque RGBA8888 OVR1 layer at `(16, 164) 288×620`,
so it keeps 8 bits per channel. `UiManager_NodePhotoShow/Hide` are the same direct-PLIB pattern as the
video frame overlay (§16) — `SetLayerRGBColorMode` + `SetLayerAddress` + window pos/size + enable.

- **Why not a canvas.** Nothing on this layer needs Legato: it is one static raster at a fixed rect.
  So this needed **no new Legato layer, no `add_layer.py`, no MGS Generate and no
  `CONFIG_CANVAS_NUM_OBJ` bump** — unlike the album-art strip (§4.1 layer 3), which is a canvas
  because it carries labels beside the cover.
- **Why RGBA8888 and not RGB888_PACKED**, which the XLCDC does support and would be 25% cheaper: the
  photo arrives through Legato's PNG decoder, and spec §4.8.7 records that the 2D engine's
  `gfx2dFormats[]` maps RGB888 to `-1`. RGBA8888 is the mode the decoder and the XLCDC already agree
  on, proven by the album-art tier. The alpha byte is **forced opaque after decode** (`game_art.c`'s
  fix: a PNG-without-alpha decode leaves it zero, which the LCDC reads as a transparent layer).
- **The frame is baked into the asset, not drawn.** Nothing on BASE can draw over an overlay, so the
  1px `#404040` border and the radius-4 corners (eaten back to page black) are rendered offline by
  `tools/node-photos/`, reproducing what `PanelAA_EnableRoundImage` used to do at runtime. This is
  what keeps the rect fully opaque, which is what makes the full-rect BASE discard valid (§18.2).
  The canvas keeps an **empty** frame widget for nodes with no photo; the two are exclusive.
- **Slots stay in cached `.region_ram`** even though the LCDC DMA-reads them, because a slot is
  write-once: the CPU decodes into it, `decode_one` cleans it to DDR, and nothing writes it again, so
  no line can go dirty behind the display controller. Cost of the format change: `.region_ram`
  +2,499,840 B (7 slots, 565 → 8888). **`.region_nocache` is byte-for-byte unchanged** at
  32,336,480 B — this buys full colour without spending the last full-screen surface.
- **`photo_apply()` is gated on the screen being shown**, because OVR1 belongs to whoever is on the
  panel: the splash owns it for all of boot, and `show_view(VIEW_OVERVIEW)` runs at build time, which
  would otherwise disable the splash mid-boot.
- **The reveal is deferred a frame; the takedown is not.** Enabling a hardware layer is instant while
  the rest of the view is still being painted into the canvas, so on hardware the photo arrived
  visibly ahead of its own page. `photo_task` (static, prio 2) waits for the repaint and then enables
  the layer. It waits on **`UiManager_WaitFrameAfter`**, i.e. the renderer's `leRenderer_GetDrawCount()`
  advancing past the value sampled where the damage was queued — exact, because that counter moves in
  the renderer's `postFrame` only once every damaged rect on every layer has been drawn. This is the
  signal to reach for over `leRenderer_IsIdle()`, which is also true in the gaps between `leUpdate`
  calls and so needs a tuned stable window to be trusted. A `s_photo_gen` counter drops a reveal the
  user has already navigated past. Its own task because the tap is dispatched from inside `leUpdate`:
  waiting in the handler would be waiting on `LEGATO_Tasks` from inside `LEGATO_Tasks`.
  - **The structural fix, not taken here:** give overview and detail their own canvases (§13's
    property — Legato paints a canvas whether or not it is bound), which makes the switch a pure
    layer bind with *no* repaint to outrun, and lets the page and the photo land in the same frame
    with no wait at all. Costs a second full-screen RGB565 surface (~2.0 MB) against the 1.16 MB
    `.region_nocache` has free, so it needs the region grown. Worth doing if the deferred reveal
    still reads as a two-stage arrival.
- **Both of the open risks are now settled on hardware.** The XLCDC blender does treat an alpha-255
  RGBA8888 overlay as fully opaque — the full-rect BASE discard is active while the photo is up and
  nothing shows through — and direct LCDC scanout of cached `.region_ram` renders correctly, so the
  write-once + clean argument holds in practice. Neither needed the `.region_nocache` fallback.

## 11. Relationship to spec §4.5 / Q5

This answers spec **Q5** (Legato vs. custom UI) for the *presentation* layer: **Legato is the
renderer; a marvin `ui_manager` over GFX Canvas owns surface/layer composition and screen
orchestration.** Per-screen *authoring* is now **code**, not MGS — the design supplies only assets
and one bare panel per layer-screen (§17); the compositor assembles the built trees onto hardware
layers.
