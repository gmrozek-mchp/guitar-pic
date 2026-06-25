# marvin — UI compositor (canvas surface pool)

Design for marvin's operator-UI presentation layer: **pre-render UI panels into RAM
surfaces, then multiplex the scarce LCDC hardware layers across them** so a panel can
be brought up (or slid in) instantly without re-rendering or disturbing what is behind
it. Built on Microchip's **GFX Canvas** component as the surface/layer substrate, with
Legato kept as the rendering engine (fonts, draw primitives, the `song_list` widget).

Read [`spec.md`](spec.md) §4.5 (operator UI) and §4.1 (video/display) first; this doc is
the authoritative design for the compositor specifically. Status: **design — not yet
built.** Decision recorded in [`journal.md`](journal.md) (2026-06-25).

---

## 1. The core idea

Today, content geometry is welded to hardware layers 1:1:1 — a Legato layer renders into
exactly one framebuffer, which is exactly one LCDC layer. There are only **4 LCDC layers**
on the SAM9X75 XLCDC (`BASE`, `OVR1`, `HEO`, `OVR2`), and one (`HEO`) is owned by the live
camera. That is too few to give every UI panel a permanent layer.

The compositor breaks the welding:

- A **surface** is a chunk of RAM (a framebuffer) holding pre-rendered pixels. Surfaces are
  cheap — we can keep many resident.
- A **layer** is a scarce hardware compositor window. We have **two free** ones (`OVR1`,
  `OVR2`) plus `BASE`.
- Bringing a panel up = point a free layer at a surface and enable it (register writes, no
  render). Hiding = disable the layer. Sliding = animate the layer's window position. The
  surface's pixels persist, so none of this re-renders anything or touches the layers below.

This is exactly what the **GFX Canvas** component provides, and it is the one capability the
plain layer registers cannot give us: canvas redirects Legato's render target to an arbitrary
off-screen buffer (a "canvas object"), independent of which hardware layer — if any — that
buffer is currently shown on.

## 2. Why canvas (and why not the alternatives)

How Legato renders today (confirmed in `legato_renderer.c` + `drv_gfx_xlcdc.c`): Legato
paints damaged tiles into a **scratch buffer**, then `DRV_XLCDC_BlitBuffer` copies each tile
into `drvLayer[activeLayer].pixelBuffer` — the framebuffer of whatever layer is currently
*active* (`GFX_IOCTL_SET_ACTIVE_LAYER`). **The only knob deciding where Legato's pixels land
is the active layer's `pixelBuffer.pixels` pointer.** There is no stock Legato API for
"render this widget tree into that arbitrary buffer."

- **GFX Canvas (chosen).** `GFXC_GetPixelBuffer()` hands Legato the *canvas object's* buffer
  instead of a fixed layer framebuffer, and `gfxcSetBaseCanvasID` switches which canvas a
  Legato layer renders into. That is precisely "many pre-rendered surfaces in RAM,
  multiplexed onto few layers." It also brings layer binding (`gfxcSetLayer`), show/hide,
  window position/size/alpha, and hardware-stepped fade/move effects — all generated.
- **DIY pixel-buffer retarget (rejected).** We could mutate the active layer's
  `pixelBuffer.pixels` ourselves before each render and park the buffer afterward. Viable
  (~100–200 LOC) but it reimplements the bookkeeping canvas already does (per-surface size,
  scratch tiling, layer binding) — and once surfaces are non-full-screen it grows. Not worth
  maintaining a hand-rolled half of canvas.
- **Direct layer control only (insufficient).** The XLCDC PLIB exposes everything needed to
  *present and animate* a layer (`XLCDC_SetLayerAddress/WindowXYPos/WindowXYSize/Opts/Enable`,
  each with a deferred-commit flag). This is the right tool for the **slide/show/hide**, and
  the compositor uses it (or canvas's wrappers over it). What it cannot do is get Legato
  widget content *into* an off-screen surface in the first place — that is the canvas half.

So the split is: **canvas owns "render content into a surface and bind a surface to a
layer"; direct layer control (via canvas's effect engine or our own stepper) owns "move it."**

## 3. Hardware layer budget

| LCDC layer | Owner | Notes |
|---|---|---|
| `BASE`  | Compositor — persistent background / main dashboard | Always on; bottom of the stack. |
| `OVR1`  | Compositor — swappable overlay A | **To be enabled** (`XLCDC_TOT_LAYERS` is 2 today = BASE+HEO). |
| `OVR2`  | Compositor — swappable overlay B | **To be enabled.** |
| `HEO`   | **Video capture** (`video.c`) — live camera | Re-pointed every frame in the ISC ISR; the scaler lives here. **Off-limits to the compositor.** |

So at any instant the stack is: `BASE` (background) + up to two overlay panels (`OVR1`/`OVR2`)
+ the camera (`HEO`), composited in hardware. We can keep *many* surfaces pre-rendered in RAM
and choose which two overlays are bound/visible at a time.

> Latent config item: the stock generated `Marvin` screen maps Legato layer 1 → `HEO`
> (a 320×800 root). That conflicts with video owning `HEO`. Under this design the two Legato
> layers must map to `BASE` + an `OVR`, never `HEO`. Reconcile during bring-up (§8).

## 4. Architecture

```
                 ┌──────────── marvin compositor (new module, ui/) ───────────┐
                 │  surface registry  │  panel state machine  │  touch router  │
                 └───────┬───────────────────────┬───────────────────┬────────┘
                         │ render into surface    │ bind/show/slide   │ route touch
                 ┌───────▼────────┐      ┌────────▼─────────┐   ┌─────▼───────┐
   Legato  ──────►  GFX Canvas    │      │   GFX Canvas      │   │ Legato pick │
 (fonts,         │  gfxcGetPixel  │      │ gfxcSetLayer/Show │   │  (live only)│
  song_list,     │  Buffer → surf │      │ /Position/Move    │   └─────────────┘
  string render) └───────┬────────┘      └────────┬──────────┘
                         │ canvas objects (static, non-cached RAM surfaces)
                 ┌───────▼───────────────────────▼──────────┐
                 │      XLCDC driver — per-layer IOCTLs      │   HEO: driven directly
                 │   BASE / OVR1 / OVR2  (canvas-managed)    │   by video.c (camera)
                 └──────────────────────────────────────────┘
```

### 4.1 Surfaces
A fixed pool of **canvas objects**, each given a **statically allocated, non-cached** buffer
(`gfxcSetPixelBuffer(id, w, h, mode, &static_buf)`) sized to its panel — not necessarily
full-screen. RGB565 to match the panel and halve memory; ARGB8888 only for a surface that
needs per-pixel alpha over the camera (most fades use the layer's *global* alpha instead).

### 4.2 The compositor module (marvin-owned, in `ui/`)
A thin module over the canvas API, following the MCC-isolation idiom (own files added via
`user.cmake`, no edits to the generated tree). It owns:
- the **surface registry** — logical panel (song list, nav menu, dashboard, dialog) → canvas id;
- a small **panel state machine** — `render → park → present(layer) → (interact) → hide`;
- the **touch router** (§5).

### 4.3 Rendering content into a surface
To (re)render a panel's widget tree into its surface, point Legato at that canvas group
(`gfxcSetBaseCanvasID`) and drive one Legato render. The `song_list` widget and Legato's
string/scheme renderers are reused verbatim. Rendering happens **only when the panel's
content is dirty** (new model, scroll, selection) — not per display frame. The rendered
pixels then sit parked in the surface indefinitely.

### 4.4 Presenting / animating
- **Instant reveal:** `gfxcSetLayer(id, OVRx)` + `gfxcShowCanvas(id)` (+ position/size). No render.
- **Hide:** `gfxcHideCanvas(id)`.
- **Slide:** `gfxcStartEffectMove(...)` (canvas's stepped tween) or our own vsync stepper over
  `gfxcSetWindowPosition`. Off-edge slides need window clipping (`CONFIG_CANVAS_ENABLE_WINDOW_CLIPPING`,
  on by default) so the layer window stays within display bounds.
- **Fade:** layer global alpha ramp (`gfxcStartEffectFade`).

## 5. Touch routing — the key constraint

Legato delivers touch by hit-testing the **active screen's widget tree** (`legato_input.c`
→ layer roots). A *parked* surface bound to `OVR1` shows pixels but is **not** in Legato's
screen tree, so Legato will not route touches to it. This is the one thing the surface-pool
model does not get for free, and it must be designed, not discovered.

**Rule:** a surface is interactive **only while it is the live render target of a Legato
layer** positioned where it is shown. Parked snapshots are for fast presentation and
transitions (reveal, slide, background), not interaction.

**Recommended model — reveal-then-promote:** bring a panel up instantly from its parked
surface; when it must become interactive, **promote** it — make its canvas the active Legato
layer's render target (`gfxcSetBaseCanvasID`), align the layer to its on-screen position, and
render live. Promotion is cheap (register writes + one render). `song_list` already
hit-tests internally, so the live path needs only Legato's event *delivery*, which promotion
restores. (Alternative, deferred: a marvin-owned input dispatcher reading maxtouch and
mapping coords → topmost visible surface → element, bypassing Legato's screen model
entirely. More control, more code; revisit only if promotion proves awkward.)

## 6. Memory budget

`.region_nocache` is **32 MB** and is *shared* with the ISC capture pool — account for both:

| Consumer | Size (approx) |
|---|---|
| ISC capture pool (4 × 1280×720 × 3 B, BGR888) | ~10.5 MB |
| Legato scratch (`LE_SCRATCH_BUFFER_SIZE_KB` 512) | 0.5 MB |
| `BASE` surface (1280×800 × 2 B, RGB565) | ~2.0 MB |
| Overlay surfaces (RGB565, sized to panel) | song list ~700×800 ≈ 1.1 MB; nav menu ~320×800 ≈ 0.5 MB; dialog/metadata each ≈ 0.5–2 MB |

Full-screen RGB565 = **~2.0 MB**; most overlays are well under that. With ~21 MB free after
the capture pool and scratch, memory is **not** the binding constraint — the **two free
overlay layers** are. Size surfaces to their content, prefer RGB565, and set
`CONFIG_CANVAS_NUM_OBJ` to the number of distinct surfaces we pre-cache (≥ the panel count,
not the layer count).

## 7. Static-allocation & cache rules

- **Static buffers.** Canvas's default config allocates nothing (NULL buffers). Every canvas
  surface gets a file-scope static array in `.region_nocache`, passed via `gfxcSetPixelBuffer`.
  No malloc — consistent with the project rule. ([static-allocation rule](journal.md))
- **Non-cached.** Surfaces are CPU-rendered then read by the LCDC DMA / 2D engine, so they
  must be non-cached, same as the framebuffer (`FB_CACHE_NC`) and the Legato scratch (the
  2026-06-24 cache-coherency fix). Note CPU writes to non-cached DDR are slow; rendering is
  occasional (dirty-only), so this is acceptable — re-measure if a full-surface repaint
  shows up hot.
- **FX task.** The generated `GFX_CANVAS_Task` uses dynamic `xTaskCreate` (like the other MCC
  XLCDC/USB tasks on heap_1). Either convert to `xTaskCreateStatic` or accept it alongside
  the existing MCC dynamic tasks; log the choice as an MCC re-apply patch. The task is
  **required even with FX off** — it drives the `GFXC_INIT → RUNNING` transition, and
  `gfxcShowCanvas`/update return early until `RUNNING`.
- **Task priority = 2 (UI band).** Set the gfx_canvas component's *Task Priority* field to **2**
  so the task joins marvin's UI tier (`LEGATO`/`XLCDC`/`DRV_MAXTOUCH`/`SYS_INPUT`; see the
  2026-05-21 priority re-tiering in [`journal.md`](journal.md)). The generated default of 1 is
  wrong — band 1 is reserved for future housekeeping, and the canvas task is an active
  UI-critical-path stage (it commits Legato's output to the layers). Set it in the component
  yml, not `tasks.c`, so regen reproduces it with no re-apply patch. Stack 1024 / 10 ms delay
  are fine (match the other UI tasks); FX is off so it mostly idles between updates.

## 8. MCC configuration & re-apply notes

The canvas component re-architects the display path (canvas becomes Legato's
`gfxDriverInterface`; XLCDC's own struct → `xlcdcDisplayDriver`; the static `frame_buffer`
array is removed; a per-layer IOCTL subset is added; `GFX_CANVAS_Initialize` + the FX task
are wired in). Steps:

1. **Enable `OVR1` + `OVR2`** in the XLCDC driver (`XLCDC_TOT_LAYERS` 2→4, `layerOrder`) so
   the compositor has its two overlay layers. Keep `HEO` for video.
2. **Add the GFX Canvas component**, set `CONFIG_CANVAS_NUM_OBJ` to our surface count, default
   color mode RGB565, and give each object a static non-cached buffer (replace the generated
   `gfxcSetPixelBuffer(..., NULL)` template with our buffers + real sizes).
3. **Reconcile the Legato layer→hardware-layer mapping** so the two Legato layers map to
   `BASE` + an `OVR`, never `HEO` (§3 latent item).
4. **Verify `video.c`/HEO is undisturbed** by the canvas/XLCDC init reorder (canvas inits all
   layers disabled; video sets up HEO at runtime — confirm ordering doesn't clobber it).
5. Add the canvas/FX-task choices to the MCC re-apply patch list (cache/scratch `-D`, task
   static conversion, layer-count, buffer wiring).

## 9. Phased plan

- **P0 — substrate.** Enable OVR1/OVR2; add the canvas component with static non-cached
  surfaces; get a blank `BASE` + one OVR rendering through canvas (display still works).
- **P1 — compositor module.** `ui/compositor.{h,c}`: surface registry + present/hide/bind over
  the canvas API; a console command to bind/show/hide a test surface on OVR1. Reuse `song_list`.
- **P2 — pre-render + instant reveal.** Render the song list into its surface once; bind to
  OVR1 and reveal/hide instantly; confirm no re-render and BASE untouched.
- **P3 — slide + nav menu.** Nav-menu surface on OVR2; slide in/out (canvas move effect or our
  stepper); off-edge clipping verified.
- **P4 — touch promotion.** Reveal-then-promote so the song list is interactive once shown;
  scroll/select validated on hardware.
- **P5 — panel set.** Generalize to the §4.5 surfaces (dashboard, metadata, dialog); finalize
  the surface count + memory budget.

## 10. Open questions

- **Touch model:** is reveal-then-promote (§5) sufficient, or do we need a marvin-owned
  maxtouch dispatcher? Decide at P4.
- **`GFX_CANVAS_Task` static conversion** vs. accepting it as an MCC dynamic task.
- **Surface count / sizes** (`CONFIG_CANVAS_NUM_OBJ`, per-panel dimensions) — lock at P5
  against the real panel set and the shared `.region_nocache` budget.
- **HEO ↔ canvas coexistence:** confirm canvas's layer init/IOCTL path never touches HEO and
  the init reorder doesn't disturb video's runtime HEO setup.
- **Per-pixel alpha:** which surfaces (if any) need ARGB8888 over the camera vs. layer global
  alpha.

## 11. Relationship to spec §4.5 / Q5

This is the concrete answer to spec **Q5** (Legato vs. custom UI) for the *presentation*
layer: **keep Legato as the renderer, add a thin marvin compositor over GFX Canvas for
surface/layer management.** It does not change how individual screens are *authored* (MGS /
custom widgets) — only how their rendered output is cached in RAM and composited onto the
panel's hardware layers.
