# Review: `gfx/legato/vector/` vs. our hand-rolled AA drawing

**Date:** 2026-08-05 · **Scope:** review only, no code changed.

> **Status, 2026-08-06 — adopted for four widgets, with four corrections to this document.**
> `widget_tilt`, `widget_gauge`, the `panel_aa` dot and `widget_fret` now use `leDraw_Vector*`
> (shared conventions in `ui/gfx/vec_draw.h`); see the 2026-08-06 entry in
> [journal.md](journal.md) for the A/B results and what is still owed. Corrections found
> while doing it:
>
> 1. **§1 / §4.1 — "clips to each shape's own bbox" is wrong for arcs.** `_calculateScanArea`
>    scans the **full circle** at `radius + halfWidth`, ignoring the span. The tilt arc scans
>    254×254 where the old loop scanned 157×157; it only comes out even because the renderer's
>    clip rect is the widget's damage rect.
> 2. **§4.5 — a capsule is not expressible.** `leDraw_VectorRectFill` clamps every corner radius
>    to `min(w,h)/4` (`_clampCorners` halves extents that are already half-extents), so the
>    `bar` retarget as written cannot work, and the `panel_aa` capsule is a band `RectFill` plus
>    an `ArcFill` per end.
> 3. **§4.4 — the dot vtable also serves `screen_bus`'s pill tracks**, so that step had to
>    handle oblong widgets too, and had to take over the widget's background fill.
> 4. **§5 — two more entries for the vendor list:** the radius clamp above, and
>    `LE_REAL_I16_FROM_FLOAT(f)` = `((int32_t)(f * 65536))` with no parentheses around `f`, so
>    any argument containing `+` or `-` silently yields a wrong number. Legato's own uses are all
>    `/` and `*`, which is why nothing in-tree trips it.

Prompted by a Legato developer pointing out the anti-aliasing functions in
`config/default/gfx/legato/vector/`. Question: should marvin's custom widgets and
rounded-panel/button code be using them?

**Short answer:** yes, for the four widgets that already scan their whole rect
(`tilt`, `gauge`, `fret`, and the `panel_aa` dot). No, for `aa_corners` itself —
its corner-box-only strategy is genuinely cheaper than anything the vector API can
express. There are also two latent vendor bugs in the vector code worth raising
with the Legato developer before we depend on it.

---

## 1. What the vector API actually is

A standalone software rasterizer, unrelated to the classic skins. Public API in
[legato_vector.h](../default/src/config/default/gfx/legato/vector/legato_vector.h):

| Function | Shape | Notable attrs |
|---|---|---|
| `leDraw_VectorRectFill` | rounded rect, **per-corner radii** | `rotation`, `alpha`, `aaMode` |
| `leDraw_VectorRectStroke` | rounded-rect outline | `width`, `mask` (all/inside/outside), `hardness` |
| `leDraw_VectorArcFill` | pie / disc | `hardness` |
| `leDraw_VectorArcStroke` | annulus sector | `width`, `capStyle` (none/square/**round**), `mask`, `hardness` |
| `leDraw_VectorLine` / `HLine` / `VLine` | line | `width`, `capStyle` |
| `leDraw_VectorConvexPolygonFill` | convex poly | `rotation` |
| `leDraw_VectorPoint` | dot | `width`, `hardness` |

Everything funnels through one scanline kernel,
[legato_vector_kernel.c:40-113](../default/src/config/default/gfx/legato/vector/legato_vector_kernel.c#L40-L113):

- Supersampled coverage AA — `LE_ANTIALIASING_NONE/2X/4X/8X/16X` → 1/2/4/8/16 sample
  points per pixel from fixed jitter tables in
  [legato_aa.c](../default/src/config/default/gfx/legato/vector/legato_aa.c).
- Coverage × attr alpha → `leRenderer_BlendPixel(x, y, color, a)`
  ([kernel:107](../default/src/config/default/gfx/legato/vector/legato_vector_kernel.c#L107)),
  i.e. a real per-pixel read-modify-write against whatever is in the framebuffer.
- **All fixed-point.** `leReal_i16` is Q16.16 `int32_t`; multiply/divide widen to
  `int64_t`. No `float` anywhere in the vector tree.
- Every entry point clips its scan area to `leRenderer_GetFrameRect()` **and**
  `leRenderer_GetClipRect()` before rasterizing
  (e.g. [rect_fill:332-344](../default/src/config/default/gfx/legato/vector/legato_vector_rect_fill.c#L332-L344)).
- State lives in a per-module `static struct Context` + `static leVector_Kernel` —
  BSS, no allocation. Consistent with our static-only discipline; not reentrant,
  which is fine inside Legato's single paint pass.

Two things to know before using it:

- **`leRectF` is centre + half-extents**, not x/y/w/h. `leRectF_FromRect()` converts
  ([legato_rectf.c:5-27](../default/src/config/default/gfx/legato/vector/legato_rectf.c#L5-L27)).
  Coordinates are screen space, same as `leRenderer_*Pixel`.
- **Arc angles are 1/16 degree** (`span = 90 * 16` for a quadrant), CCW with 0° to
  the right.

Prerequisites in this build, all satisfied:
`LE_ALPHA_BLENDING_ENABLED == 1` (without it `blendPixel` degrades to `putPixel` and
all AA becomes hard edges — [legato_draw.c:498-511](../default/src/config/default/gfx/legato/renderer/legato_draw.c#L498-L511)),
`LE_RENDER_ORIENTATION == 0`, `LE_DEFAULT_COLOR_MODE == RGB_565`.

Nothing in Legato calls `leDraw_Vector*` — no skin, no widget. It is a pure add-on
API, and the `.o` files are currently discarded by `--gc-sections` (confirmed in
`mem.map`), so adopting it costs flash we aren't paying today rather than freeing any.

---

## 2. The case for switching: we are doing soft-float per pixel

SAM9X75 is ARM926EJ-S — **no FPU**. Confirmed both from the datasheet and from the
link map, which pulls in `__aeabi_fadd`, `__aeabi_fmul`, `__aeabi_fdiv`, `sqrtf`,
`atan2f`. Every `float` operation in our widgets is a library call.

Per-pixel transcendental/soft-float work in the current code:

| Widget | Per pixel | Pixels touched |
|---|---|---|
| [widget_tilt.c:92-135](../default/src/ui/widgets/tilt/widget_tilt.c#L92-L135) | 3× `sqrtf`, 4× `cosf`/`sinf`, 1× `atan2f`, ~40 float ops | **full rect** (157×157 ≈ 24.6k) |
| [widget_gauge.c:55-81](../default/src/ui/widgets/gauge/widget_gauge.c#L55-L81) | 1× `sqrtf`, 1× `atan2f` | full rect |
| [widget_fret.c:95-121](../default/src/ui/widgets/fret/widget_fret.c#L95-L121) | 1-2× `sqrtf` | full rect |
| [aa_corners.c:22-51](../default/src/ui/gfx/aa_corners.c#L22-L51) | 1× `sqrtf` | 4 × r² only |

`tilt` is the outlier by a wide margin — it calls `cap_cov()` four times per pixel and
each one does a `cosf`, a `sinf`, and a `sqrtf`
([widget_tilt.c:58-67](../default/src/ui/widgets/tilt/widget_tilt.c#L58-L67)), on top of an
`atan2f` for the sweep test. That is roughly 8 soft-float transcendentals per pixel
across a 24k-pixel rect, every time the value changes. The vector `ArcStroke` with
`LE_CAPSTYLE_ROUND` computes the same shape with integer squared-distance compares.

Three secondary benefits:

1. **Clipping correctness.** Our helpers write via the unsafe `leRenderer_PutPixel`
   with no bounds check. That is the root cause of the progress-bar corruption
   documented in [journal.md](journal.md) ("Partial-repaint gotcha (fill-end
   artifacts)") — when the damage rect is a sliver, corner writes land outside the
   tile. We worked around it by force-invalidating the whole widget. The vector
   kernel clips its scan area to frame ∩ clip rect up front, so the bug class
   disappears structurally and partial repaints become safe again.
2. **No backdrop assumption.** `AaCorners_Render` samples *one* pixel as "the
   backdrop" ([aa_corners.c:18-19](../default/src/ui/gfx/aa_corners.c#L18-L19)) and the
   dot variant samples at `rect.x - 1`
   ([widget_panel_aa.c:118-121](../default/src/ui/widgets/panel_aa/widget_panel_aa.c#L118-L121)),
   both requiring a known-solid surround. `BlendPixel` reads each destination pixel,
   so vector shapes composite correctly over gradients, images, or other widgets.
3. **Sidesteps the stock-skin hazards.** Both documented gotchas — the classic
   rounded paint hanging at `cornerRadius >= min(w,h)/2`, and `drawBackground`
   switching to oversized `FillRoundCornerRect` arcs — only exist because we let the
   skin draw the shape. A vector-drawn shape does not involve the skin at all.

### AA quality is not a reason to stay

Our analytic coverage produces 101 levels; 4X/8X vector AA produces 4/8. In RGB565
with 5/6-bit channels, a typical fill↔backdrop step spans well under 16 distinguishable
values, so 8X is visually indistinguishable and 4X is very close. Worth an A/B
screendump, but I would not expect a difference.

---

## 3. Where NOT to switch: `aa_corners`

`AaCorners_Render` touches `4 × radius²` pixels. `leDraw_VectorRectFill` scans the
whole rect. For a 149×149 dashboard card at radius 6 that is 144 px vs 22 201 px × 4
samples — roughly a 150× increase in pixel work. There is no way to narrow it:
`leRenderer_GetClipRect` has no public setter, and `leDraw_VectorArcFill` computes its
scan box from the **full circle**, not the requested span
([arc_fill.c:184-193](../default/src/config/default/gfx/legato/vector/legato_vector_arc_fill.c#L184-L193)),
so even a per-corner quadrant fill would scan 2r × 2r.

Structurally it also doesn't fit: `aa_corners` runs *after* the skin has drawn fill,
border and centred text, and only repairs the corners. A vector rect fill would
clobber the text, and the skin's paint states give us no hook to inject between
background and text.

**Keep `aa_corners` as-is** for `panel_aa` and `button_aa`. Its narrow footprint is
the right design for "repair the corners of an otherwise-fine skin draw."

---

## 4. Concrete mapping, highest value first

### 4.1 `widget_tilt` → `ArcStroke` ×2 + `ArcFill` ×2

The single biggest win. The widget is exactly an annulus sector with round caps, an
overlaid partial sector, and two discs for the thumb.

```
leVectorArc_StrokeAttr a = { .color = track, .alpha = 255,
                             .width = LE_REAL_I16_FROM_INT(38),
                             .hardness = LE_REAL_I16_ONE,
                             .mask = LE_STROKEMASK_ALL,
                             .aaMode = LE_ANTIALIASING_8X,
                             .capStyle = LE_CAPSTYLE_ROUND };
leDraw_VectorArcStroke(&centre, radius, 0, 90 * 16, &a);      /* track */
a.color = fill;
leDraw_VectorArcStroke(&centre, radius, 0, tilt * 16, &a);    /* fill  */
leDraw_VectorArcFill(&thumb, THUMB_R + RIM_HALF, 0, 360*16, &rim);
leDraw_VectorArcFill(&thumb, THUMB_R - RIM_HALF, 0, 360*16, &body);
```

Deletes `cap_cov`, `coverage`, `blend`, and every `cosf`/`sinf`/`sqrtf`/`atan2f` from
the paint path. Also shrinks the scan from the full rect to each shape's own bbox.
`value_from_point`'s `atan2f` stays (once per touch, irrelevant).

### 4.2 `widget_gauge` → `ArcStroke` ×2

A 180° annulus with square caps. Two calls replace the whole nested loop, and the
`frac <= pct` colour decision becomes two spans instead of a per-pixel `atan2f`.

### 4.3 `widget_fret` → `RectFill` ×1-2

`rrect_sdf` is precisely `leDraw_VectorRectFill` with four equal radii. `IDLE_ALPHA
0.75` becomes `attr.alpha = 191`. The held ring becomes an outer `RectFill(ring, r=8)`
plus an inner `RectFill(body)` on a rect inset by `RING_PX` with radius `8 - RING_PX`
— cleaner than the current SDF-offset trick, and it removes the last `sqrtf` from the
fret path.

### 4.4 `panel_aa` dot → `ArcFill(360°)`

`PanelAA_EnableDot` currently fills a square, then eats the corners back to a colour
sampled at `rect.x - 1`, purely to dodge the stock rounded-paint hang. A single
`leDraw_VectorArcFill` with `span = 360 * 16` draws a true AA disc, blends against the
real backdrop rather than a sampled guess, needs no `rect.x > 0` guard, and never
touches the stock rounding path.

### 4.5 `progressbar_aa` → `RectFill` ×2 (capsule track + capsule fill)

Radius = height/2 on both. Two calls replace stock-track + manual `RectFill` +
`RenderRoundImage`. This is also the widget whose clipping workaround the vector
kernel makes unnecessary — worth revisiting whether the whole-widget invalidate can
be relaxed (keep the pixel-width fill; that fixed quantization, not clipping).

### 4.6 `widget_sparkline` (optional)

Currently `leRenderer_VertLine` per column — a hard staircase.
[sparkline](../default/src/ui/widgets/sparkline/widget_sparkline.c) could use
`leDraw_VectorLine` for AA sloped segments. Cosmetic, low priority.

---

## 5. Two latent bugs in the vector code (raise with the Legato developer)

Since nothing in Legato calls this API, it is effectively untested in-tree. Two
defects found by reading:

**(a) `_kernel.shadeFragment` is never cleared.** `leDraw_Vector*` memsets its
`_context` but not its `_kernel`, and the `hardness < 1` path assigns
`_kernel.shadeFragment = _shade_Gradient` while the `hardness == 1` path never sets it
back to `NULL`. The kernel dispatches on `if (krn->shadeFragment != NULL)`
([kernel:71](../default/src/config/default/gfx/legato/vector/legato_vector_kernel.c#L71)),
so the first soft-edged call permanently sticks the gradient shader on that module and
every later crisp-edged call silently renders wrong. Affects `arc_fill`, `arc_stroke`,
`rect_stroke`, `line`, `hline`, `vline`, `point`.
*Mitigation for us:* always pass `hardness = LE_REAL_I16_ONE`, which is what we want
anyway. Fragile — one soft-edge experiment poisons the module.

**(b) `arc_stroke` picks its evaluator before deciding convexity.**
[arc_stroke.c:995-1047](../default/src/config/default/gfx/legato/vector/legato_vector_arc_stroke.c#L995-L1047)
selects `_fillConvexArc*` vs `_fillConcaveArc*` from `_context.convex`, then sets
`_context.convex = LE_FALSE` for `|span| > 180°` *afterwards* — too late to affect the
choice. Arcs wider than a half-turn get the convex evaluator.
*Mitigation for us:* our arcs are ≤ 180°, so we never hit it.

Minor, same file family: `arc_fill`'s range loop
([arc_fill.c:113-120](../default/src/config/default/gfx/legato/vector/legato_vector_arc_fill.c#L113-L120))
iterates `rangeCount` but indexes `ranges[0]`/`ranges[1]` unconditionally; with
`rangeCount == 1`, `ranges[1]` is `{0, 0}` and a point at exactly 0° passes the test it
should fail.

---

## 6. Recommendation

Adopt incrementally, one widget per change, screendump-compared before/after:

1. **`widget_tilt`** — biggest win by a wide margin, self-contained, and it validates
   `ArcStroke` + round caps + `ArcFill` in one go.
2. **`widget_gauge`** — same primitives, trivially follows.
3. **`panel_aa` dot** — smallest diff, removes a documented workaround.
4. **`widget_fret`** — validates `RectFill`.
5. **`progressbar_aa`** — then reconsider the invalidate workaround.
6. Leave **`aa_corners`**, `panel_aa`, `button_aa` alone.

Pin `aaMode = LE_ANTIALIASING_8X` and `hardness = LE_REAL_I16_ONE` project-wide (the
latter avoids bug (a)); drop to 4X if profiling asks for it.

Two things to measure on hardware, not assume:

- **Actual cost.** Fixed-point-with-8-samples vs soft-float-with-1-sample is a
  plausible win, not a certain one. `tilt` and `gauge` should be measured with
  `marvin-perf` before and after; if 8X is too slow, 4X halves the sample work.
- **Visual parity in RGB565.** Screendump A/B, particularly the tilt thumb rim
  (1.25 px half-width — near the limit of what 8 samples resolve) and the gauge's
  fill/track boundary.

Worth asking the Legato developer directly: are the two bugs in §5 known, is there a
fix in a newer Legato drop, and is `leDraw_Vector*` considered production-ready or
still preview?
