# marvin — UI architecture (canvas compositor + per-screen modules)

How marvin's operator UI is structured: independently-authored panels (dashboard, nav,
dialogs) each render into their own **canvas surface** in RAM, the compositor maps surfaces
onto the scarce **LCDC hardware layers**, and — with the MGS screen state machine disabled —
the application owns screen orchestration so multiple panels coexist **live and interactive
at once** instead of one-screen-at-a-time switching.

Read [`spec.md`](spec.md) §4.5 (operator UI) and §4.1 (video/display) first. Decisions in
[`journal.md`](journal.md) (2026-06-25). **Status: canvas compositor built and working**
(dashboard on BASE + slide-in nav on OVR1, single-active highlight). **Next: the `ui_manager`
+ per-screen-module refactor** described in §6–§7.

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
              │ui/dashboard│         │   ui/nav    │          │ui/song_select│
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

- **Base-view replacement** detail when a 2nd full-screen view arrives (rebuild vs parked
  last-frame on return; how nav persistence interacts with `screenShow`/`Hide` of layer 0).
- **`manual_input.c` fate** — `Screen0` retired; the strum handlers now serve the Dashboard
  screen, so it stays until manual control is reworked.
- **Per-pixel alpha** — which (if any) overlay needs ARGB8888 over the camera vs. layer alpha.
- **Rounded corners aren't anti-aliased.** The nav buttons use the widget `cornerRadius`
  (set in code — not exposed in MGS), but the classic skin's rounded-rect fill has hard,
  stepped edges (no AA). To smooth them: pre-AA'd button images / 9-patches, or a custom
  skin/draw with edge anti-aliasing. Cosmetic; deferred.

## 11. Relationship to spec §4.5 / Q5

This answers spec **Q5** (Legato vs. custom UI) for the *presentation* layer: **Legato is the
renderer; a marvin `ui_manager` over GFX Canvas owns surface/layer composition and screen
orchestration.** Per-screen *authoring* stays in MGS (one Screen per panel); the compositor
assembles their trees onto hardware layers.
