# Yellow-Tinge / Left-Half Artifact — Complete Analysis

Working document collecting every test we've run against the yellow-tinge / left-half-of-overlay artifact on the SAM9X75 + 10.1" NVDI 1280×800 LVDS panel. Goal of this doc: avoid re-treading any of the tested ground; record the empirical results; identify the most-likely root cause; list the hardware mitigations worth trying.

**Status (2026-05-15):** software config exhausted. Yellow remains. Hardware mitigation is the next move.

---

## 1. Symptom

- White pixels in **overlay layer content** (OVR1 or HEO) display with a yellow tinge.
- The tinge is concentrated on the **left half** of the overlay window with a **sharp vertical divide** near the window's horizontal midpoint. The right half of the same window is clean.
- The artifact is **modulated by physical handling of the LVDS cable** at the panel-side connector end — touching, twisting, or repositioning the cable changes the strength of the artifact and can fully clear it in a "right" position.
- Yellow ≈ blue channel attenuated, red+green normal. White = `0xFE 0xFE 0xFE` per channel; yellow tinge looks like blue dropping to ~50% while R+G stay full.
- Frame rate / sustained display look fine — no flicker, sync, or timing instability. Just the per-pixel color shift on the left half of overlay content.

## 2. Key finding: artifact is **overlay-layer-only**

BASE layer **never** shows the artifact, regardless of what we put in it or at what bandwidth. The instant any overlay layer (OVR1 or HEO) is bound to display non-zero content over BASE, yellow appears in that overlay's left half.

This is the strongest single clue. It rules out:
- LVDS cable being globally bad — both halves of the LVDS bit stream serve all panel pixels; if a lane were degraded uniformly, BASE pixels would also corrupt
- LVDS clock / serializer marginal — same reasoning
- DDR / panel timing — both paths use the same pixel clock and panel timing
- Total LCDC read bandwidth — BASE alone at 123 MB/s ARGB_8888 reads is clean; OVR1 at 62 MB/s RGB_888_PACKED reads is yellow

What's different about overlays that BASE doesn't share:
- Overlays have **per-pixel alpha-blend logic** between layers
- Overlays have a **scaler / CSC / gamma / brightness-contrast / chroma-key / replication** pipeline that BASE lacks
- Overlay DMA is **windowed within a scanline** (active only during the overlay's x-range), so its bus activity is **clumped** rather than spread across the line
- Overlays are configurable for **VIDPRI** (elevated AHB priority); BASE is not

The bandwidth, blend factors, and color format have all been varied without changing the artifact. That points away from those as the root cause and toward something structural in the overlay's data path.

## 3. Tests performed

Across the `lcd` and `bigteninch` branches.

### 3.1 BASE layer alone tests (no overlay active)

| Test | BASE format | BASE content | BASE BW | Yellow? |
|---|---|---|---|---|
| MCC default boot, BASE-only | RGB_565 | auto-zero black | 122 MB/s | **clean** |
| BASE white fill (lcd `mode 1`) | RGB_565 | 0xFFFF white | 122 MB/s | **clean** |
| BASE white at 32-bit (today) | ARGB_8888 | 0x00FFFFFF white | 123 MB/s | **clean** |

### 3.2 BASE + ISC capture writes, no overlay reads

| Test | BASE format | BASE content | ISC writing? | OVR/HEO reading? | Yellow? |
|---|---|---|---|---|---|
| `lcd` mode 2 | RGB_565 | white | yes | no | **clean** |

This rules out ISC's DMA writes as a cause. ISC + BASE active simultaneously → no yellow.

### 3.3 BASE + Overlay (OVR1 or HEO) bound to capture

| Test | BASE | Overlay layer | Overlay format | Capture src | Yellow on overlay? |
|---|---|---|---|---|---|
| `lcd` initial config | RGB_565 black | OVR1 | ARGB_8888 | 720×480 BGRX32 | **yes, left half** |
| `lcd` after MCC regen → broken blend | RGB_565 black | OVR1 | ARGB_8888 | 720×480 BGRX32 | OVR1 invisible (separate bug) |
| `lcd` after blend fix | RGB_565 black | OVR1 | ARGB_8888 | 720×480 BGRX32 | **yes** |
| `lcd` post-HEO migration | RGB_565 black | HEO | ARGB_8888 | 720×480 BGRX32 | **yes** |
| `lcd` HEO with scaler 1.667× | RGB_565 black | HEO scaled | ARGB_8888 | 720×480→1200×800 | **yes** (in scaled output) |
| `lcd` mode 3 (HEO + BASE white) | RGB_565 white | HEO | ARGB_8888 | 720×480 BGRX32 | **yes (HEO area only); BASE pillarbox stays clean white** |
| `lcd` mode 4 (HEO, BASE *disabled*) | DMA off | HEO | ARGB_8888 | 720×480 BGRX32 | **yes** |
| `bigteninch` (post LVDSPLL fix, OVR1) | RGB_565 black | OVR1 | ARGB_8888 | 720×480 BGRX32 | **yes** |
| `bigteninch` RGB888 packed | RGB_565 black | OVR1 | RGB_888_PACKED | 720×480 BGR888 | **yes (same intensity)** |
| `bigteninch` BASE white (32-bit) | ARGB_8888 white | OVR1 | RGB_888_PACKED | 720×480 BGR888 | **yes (BASE pillarbox stays clean white)** |
| `bigteninch` BASE white (16-bit) | RGB_565 white | OVR1 | RGB_888_PACKED | 720×480 BGR888 | **yes; possibly slightly worse** |

**Pattern:** every overlay configuration exhibits yellow on its left half. BASE never does. Format, blend mode, scaler, layer choice (OVR1 vs HEO), and total bandwidth are all independently varied without affecting the symptom.

### 3.4 Software config tweaks (all on `lcd` branch except where noted)

| Lever changed | Direction tried | Effect on yellow |
|---|---|---|
| HEO `BLEN` | INCR32 → INCR4 (smaller bursts) | none |
| HEO `VIDPRI` | 1 → 0 (lower priority) | broke display (BASE also corrupted) |
| HEO `HEOCFG4 XMEMSIZE` in 1:1 mode | skip the write in bypass | none |
| HEO scaler taps | nearest → bilinear | unchanged at 1.667× |
| LVDS `PREEMP` (all lanes + clock) | 0 → 1 | slight improvement |
| LVDS `PREEMP` | 1 → 2 | no further improvement |
| LVDS `DC_BAL` | UNBALANCED → BALANCED | garbage (panel doesn't speak it) |
| OVR1 / HEO blend `SFACTC` | various | doesn't change yellow; affects layer visibility only |
| LCDC layer count | 4 → 2 (drop OVR2 + HEO when not used) | enables clean display, but doesn't affect yellow when OVR1 is active |
| Overlay color depth | ARGB_8888 → RGB_888_PACKED (4 B/pixel → 3 B/pixel) | none (same intensity) |
| BASE color depth (today) | ARGB_8888 → RGB_565 (4 B/pixel → 2 B/pixel) | slightly worse |
| Total LCDC read BW | 185 → 124 MB/s | none (or slightly worse) |
| Capture path | RMS=0/PACKED32/BGRX32 ↔ RMS=1/PACKED32/BGR888 | none |

### 3.5 Hardware tests

| Action | Effect |
|---|---|
| Wiggle / reseat LVDS cable at panel end | Yellow strength changes; can sometimes find a position that nearly clears it |
| Wiggle cable at SoC end | Less effect |

The cable-handling sensitivity is the strongest evidence that this is a **signal-integrity margin** issue, not a bus-arbitration / bandwidth issue.

## 4. Hypothesis

The artifact is **EMI / signal-integrity coupling between overlay-layer DMA activity and the LVDS pairs**, manifesting only when an overlay's data path is active.

Why overlays only:
- Overlays have substantially more **combinatorial logic** in their data path (scaler, CSC, alpha blender, chroma key, gamma/contrast/brightness, replication). All of these blocks toggle on every pixel, even when "disabled" — gates still switch based on inputs and remain in the data path. BASE has none of this. So overlay rendering generates more switching activity per pixel inside the SoC, which radiates more high-frequency noise.
- Overlays are **windowed** — DMA bursts are clumped within the overlay's x-range rather than spread across the scanline. Same total bandwidth concentrated in a shorter window means higher peak switching rate, more concentrated EMI energy.
- Overlays are typically configured with **`VIDPRI=1`** for video priority, which lets them preempt other masters — promotes burst-chaining and longer continuous bus-busy windows.

Why left-half-only:
- The LCDC has internal line-buffer / FIFO logic that fills at the start of a scanline and drains as it's displayed. The first portion of the overlay's window has the highest **input-side switching rate** (FIFO filling at peak rate while output is just starting). By mid-window, the steady state is reached and EMI evens out. The first half is where the noise is concentrated.

Why white pixels worst:
- White = `0xFE 0xFE 0xFE` per channel = `0x00FEFEFE` (BGRX) or `0xFEFEFE` (BGR888 packed). These pixel patterns produce the highest ratio of `1`-bits per LVDS lane symbol. Long runs of high-density `1`-bits push the LVDS receiver's input common-mode bias off-center, eroding the eye margin most aggressively. With slightly more EMI than the LVDS receiver can tolerate at marginal cable SI, the most off-balance bit positions (which carry blue components in VESA mapping) flip wrong, dropping blue → yellow tinge.

Why cable handling matters:
- LVDS pairs are differential; the cable shielding determines impedance and EMI rejection. Touching the cable changes the parasitic capacitance to ground, which shifts the impedance, the reflection profile, and the EMI pickup pattern. At marginal SI, those shifts can put the receiver above or below the bit-error threshold.

Together: at the LVDSPLL bit clock (~444 MHz over 4 lanes carrying full 24-bit color × 60 Hz), we're operating at the edge of the cable's signal-integrity envelope. SoC internal noise from overlay rendering tips us over the edge for the left half of overlay regions specifically, and the cable's positioning determines how far over.

## 5. What hasn't been tried (worth trying)

### Hardware
1. **Different LVDS cable** — shorter, better-shielded, rated for ≥ 500 Mbps/lane. The current cable is the prime suspect given the handling sensitivity.
2. **Ferrite clamp** on the cable near the panel-end connector. Cuts coupled HF noise.
3. **Cable shielding tweaks** — wrap the existing cable in foil/braid, ground to chassis, etc.
4. **PCB respin** — shorter LVDS traces, ground stitching around the LVDSC pins, ferrite bead on `Vdd_LVDS` near the chip. Last resort.

### Software (long shots, not yet tested)
1. **Move overlay framebuffer to `.region_nocache`** — would eliminate any cache-snoop traffic that adds to the bus activity. CPU isn't writing the buffer in our use case so this should be transparent.
2. **Disable HEO's combinatorial blocks more aggressively** — there may be control bits we haven't found that completely gate the scaler / CSC / etc. logic when not in use, reducing switching activity.
3. **Reduce LVDSPLL slightly** — running below 444 MHz would lower bit clock and reduce edge density, but the panel may not lock cleanly below ~50 Hz refresh. We saw clear flicker at 175 MHz and clean at 444 MHz; finding the sweet spot would take experimentation.

## 6. References

- Microchip's `mgsh_sam9x7/mgs_quickstart/firmware/src/config/curiosity_nvdi_10_1inch/` — validated config for this exact panel. Uses BASE-only display (no overlay) and runs clean. Same LVDSPLL, same cable, same panel.
- SAM9X7 Datasheet DS60001813 §44 (XLCDC) — layer config, blend factors, scaler use cases.
- `firmware/marvin/docs/capture_pipeline.md` — capture-side pipeline reference.
- `firmware/marvin/docs/display_path.md` — display-side wiring reference.
- `firmware/marvin/docs/journal.md` — chronological log of every test session and decision.
- `lcd` git branch — full commit history of all the experiments described here. Preserved as historical record.

## 7. Practical takeaway

For now, the system is in a clean architectural state on `bigteninch`:
- BGR888 packed throughout (capture + display)
- 25% less DDR pressure than the BGRX32 path
- Full RGB888 quality preserved for vision
- Yellow tinge present only in OVR1's display, not affecting vision data integrity

The vision pipeline can proceed independently — vision algorithms read pristine RGB888 from DDR, unaffected by the display-side yellow. Display correctness for the user-facing UI is the only remaining concern, and that's a hardware-level fix (cable / ferrite / shielding).
