# Yellow-Tinge / Left-Half Artifact — Complete Analysis

Working document collecting every test we've run against the yellow-tinge / left-half-of-overlay artifact on the SAM9X75 + 10.1" NVDI 1280×800 LVDS panel. Goal of this doc: avoid re-treading any of the tested ground; record the empirical results; identify the root cause; document the resolution.

**Status (2026-06-09): production refresh raised to 55 Hz on the SAM9X75 Curiosity Hybrid board.** The board port (Curiosity → Curiosity Hybrid, same 10.1″ panel + LVDS cable) widened the signal-integrity margin: the cliff moved up from the original board's 52–54 Hz window to ~57–58 Hz. 60 Hz now shows only *mild* overlay yellow (vs *severe* before), 57 Hz is clean at rest but still cable-handling-sensitive (right at the new edge), and **55 Hz is clean and handling-robust → new production setting** (~3.5% margin under the 57 Hz edge, +5 Hz over the original board's 50 Hz). Same root cause and overlay-only signature as before — only the transmit-side margin improved. Full Hybrid sweep in §9. The original-board investigation and root-cause analysis below (§1–§8) stand as the historical record.

**Status (2026-05-15, original SAM9X75 Curiosity board): RESOLVED at 50 Hz.** Yellow is gone at **40, 45, 50, and 52 Hz**, present at 54 Hz, severe at 60 Hz. Threshold sits in the **razor-thin 52–54 Hz window** (427 → 444 MHz LVDS bit clock — only ~4% bit-rate gap separates clean from failing). Confirms the hypothesis: at 444 MHz the LVDS receiver was right at the edge of its eye-margin budget for this cable; modest EMI from overlay-layer DMA tipped it over. Backing off bit rate restores margin. **Production setting: 50 Hz** (~8% margin under the cliff, comfortable headroom without sacrificing visible refresh quality).

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

## 5. Resolution: lower LVDSPLL → 40 Hz refresh

The "long shot" of reducing LVDSPLL turned out to be the actual fix. We swept frame rates 30 / 40 / 45 / 50 / 55 / 60 (each computed to keep the LVDSPLL VCO above the 600 MHz minimum) and observed:

| Refresh | LVDSPLL | Bit clock per LVDS lane | Result |
|---|---|---|---|
| 30 Hz | 246.5 MHz | ~246 Mbps | Yellow gone, subtle flicker on bright content |
| 40 Hz | 328.6 MHz | ~329 Mbps | Yellow gone, no flicker |
| 45 Hz | 369.7 MHz | ~370 Mbps | Yellow gone, no flicker |
| **50 Hz** ← chosen | **410.8 MHz** | **~411 Mbps** | **Yellow gone, no flicker — production setting** |
| 52 Hz | 427.2 MHz | ~427 Mbps | Yellow gone, no flicker (closest clean) |
| 54 Hz (was) | 444.5 MHz | ~444 Mbps | Yellow on overlay left half |
| 60 Hz | 492.9 MHz | ~493 Mbps | **Significant yellow, much worse than 54 Hz** |

The yellow threshold is bracketed in the **razor-thin 52–54 Hz window** — only ~4% bit-rate spread separates clean from failing. Textbook signature of a marginal SI link operating right at its eye-budget cliff: until the eye closes past the receiver's threshold there's no visible degradation, then it falls off rapidly. Past the cliff, severity scales with how far over you push:

- 52 Hz @ 427 Mbps = clean → ~4% margin under threshold (closest tested clean)
- 50 Hz @ 411 Mbps = clean → ~8% margin (chosen production setting)
- 45 Hz @ 370 Mbps = clean → ~17% margin
- 40 Hz @ 329 Mbps = clean → ~26% margin
- 30 Hz @ 246 Mbps = clean → ~45% margin (but slight flicker)
- 54 Hz @ 444 Mbps = visible yellow → ~0% margin (just over the cliff)
- **60 Hz @ 493 Mbps = severe yellow** → ~−10% margin (well past)

The 60 Hz observation confirms the SI cliff is one-directional: pushing further past the threshold makes things worse fast. Beyond simple bit errors, the LVDS receiver may also be dropping into a degraded lock state where it's not fully recovering between symbols.

Cable swap or hardware mitigations would push the ceiling up — but at 50 Hz with ~8% margin, we're comfortably below the cliff and haven't seen any new artifacts in extended testing. No further hardware work needed for this panel.

### Production MCC values: 50 Hz (original Curiosity board — superseded on the Hybrid; see §9)

Set in the **XLCDC Driver** MCC component (`le_gfx_driver_xlcdc.yml`):

```
LVDSClockMul       = 34
LVDSClockFrac      = 964690
LVDSClockDivPMC    = 2
XLCDCPixClockHint  = 58680000
LVDSClockOutHint   = 410760000
```

Verify: `24 × (34 + 964690/2²²) / 2 = 24 × 34.230 / 2 = 410.76 MHz` ✓
- VCO `24 × 34.230 = 821.5 MHz` (above 600 MHz floor) ✓
- LVDSPLL output `410.76 MHz`
- Pixel clock `410.76 / 7 = 58.68 MHz`
- Frame rate `58,680,000 / (1440 × 815) = 50.00 Hz` exactly

### Alternative settings

For reference if you ever want to back further off the cliff (slower refresh, more SI margin) — both confirmed clean:

**45 Hz** — `Mul=30, Frac=3384800, DivPMC=2` → 369.68 MHz output, 17% margin
**40 Hz** — `Mul=27, Frac=1610613, DivPMC=2` → 328.61 MHz output, 26% margin
**30 Hz** — `Mul=41, Frac=318768, DivPMC=4` → 246.46 MHz output, 45% margin (subtle flicker on bright content)

## 6. Why the panel + cable can't handle 444 MHz cleanly

This is the actual root cause:

- **Cable SI margin too tight at ~444 Mbps/lane.** Confirmed by physical-handling sensitivity — touching/repositioning the cable changes the artifact, which is the textbook signature of a marginally-failing differential pair. Likely the cable wasn't qualified for this rate, or the connector seating is slightly off.
- **Overlay-layer rendering pushes EMI floor higher than BASE-only does.** Overlay's data path has scaler/CSC/blender combinatorial logic that toggles on every pixel, plus its DMA bursts are clumped within the overlay's x-window rather than spread across the line. Both increase peak switching activity and EMI relative to BASE.
- **At 444 MHz bit clock, the marginal cable + elevated EMI floor combine to cross the bit-error threshold** specifically on the worst-case data patterns (long runs of high-density 1-bits, i.e. white pixels), and specifically on the bits that carry blue components (LVDS lane A2 in VESA mapping). That's the yellow tinge.
- **At 328 MHz bit clock**, the eye opens by ~35% (proportional to bit period). The receiver gets enough margin to recover all bits cleanly, including the worst-case patterns. Yellow disappears.

## 7. Future work (optional, not blocking)

1. **Hardware: better cable** — would let us run higher refresh rates if needed. 60 Hz would be possible with a properly-rated cable. Not currently necessary.
2. **Hardware: ferrite or shielding** — same goal. Not necessary at 40 Hz.
3. **Software: reduce overlay EMI floor** — long shots if we want to push refresh higher without fixing the cable: move overlay buffer to `.region_nocache`, disable any HEO blocks we haven't yet, etc. None of these promise more than a small SI improvement.

## 8. Why 40 Hz is fine

- Panel locks cleanly with no visible flicker at this rate on our content (live camera feed + static UI elements).
- Camera input is 60 fps, but display only needs to show "current frame" — 40 Hz LCDC refresh just means each captured frame is shown for at most 25 ms before the next refresh starts. Worst-case input-to-output latency is unchanged (the limiter is ISC writing into DDR at 60 fps).
- DDR bandwidth needed by LCDC drops 33% (from `BASE + OVR1 read at 60 Hz` to `at 40 Hz`), freeing it for vision workloads.
- 40 Hz is well above the 24 Hz cinema-rate baseline humans tolerate easily.

## 9. Board port re-test: SAM9X75 Curiosity Hybrid — ceiling raised, production → 55 Hz

Re-ran the frame-rate sweep after porting marvin to the SAM9X75 **Curiosity Hybrid** board (2026-06-09), reusing the same 10.1″ panel and its LVDS cable. The board change swaps everything on the **transmit** side — PCB routing from the LVDSC pins to the panel connector, the connector itself, power/ground integrity — and removes the on-board USB host as an EMI aggressor. The receiver side (panel + cable) is unchanged.

Result: **the SI margin widened and the cliff moved up ~5 Hz.**

| Refresh | Mul / Frac (DivPMC=2) | LVDS clock | Result on Hybrid |
|---|---|---|---|
| 60 Hz | 41 / 318768 | 492.9 MHz | **mild** overlay yellow (was *severe* on the original board) |
| 57 Hz | 39 / 93114 | 468.3 MHz | clean at rest; slight yellow inducible by handling the cable at the connector (right at the new edge) |
| **55 Hz** ← chosen | **37 / 2738881** | **451.8 MHz** | **clean and handling-robust → production** |
| 50 Hz | 34 / 964690 | 410.8 MHz | clean (original-board production) |

Reading: the new cliff edge sits at ~57–58 Hz (vs the original board's 52–54 Hz). 57 Hz is the new "closest clean" — clean undisturbed but handling-sensitive, the same marginal-SI signature the original board showed at 52 Hz. Backing off to **55 Hz** gives ~3.5% margin under that edge and stays clean even under deliberate cable handling, so it's the production setting — a net +5 Hz over the original board's 50 Hz.

The improvement is entirely transmit-side: same overlay-only artifact, same yellow-on-white signature, same cable-handling sensitivity — only the eye margin at a given bit rate improved. A better-rated panel cable (§7) would still be the lever to reach a robust 60 Hz; not pursued — 55 Hz is comfortable.

### Production MCC values: 55 Hz

Set in the **XLCDC Driver** MCC component (`le_gfx_driver_xlcdc.yml`):

```
LVDSClockMul       = 37
LVDSClockFrac      = 2738881
LVDSClockDivPMC    = 2
XLCDCPixClockHint  = 64548000
LVDSClockOutHint   = 451836000
```

Verify: `24 × (37 + 2738881/2²²) / 2 = 24 × 37.653 / 2 = 451.84 MHz` ✓
- VCO `24 × 37.653 = 903.7 MHz` (above 600 MHz floor) ✓
- LVDSPLL output `451.84 MHz`
- Pixel clock `451.84 / 7 = 64.55 MHz`
- Frame rate `64,548,000 / (1440 × 815) = 55.00 Hz` exactly

## 10. References

- Microchip's `mgsh_sam9x7/mgs_quickstart/firmware/src/config/curiosity_nvdi_10_1inch/` — validated config for this exact panel. Uses BASE-only display (no overlay) and runs clean at 444 MHz. Now we know why: BASE alone has lower EMI than BASE+overlay, so the cable SI margin holds even at the higher bit rate.
- SAM9X7 Datasheet DS60001813 §44 (XLCDC) — layer config, blend factors, scaler use cases.
- `firmware/marvin/docs/capture_pipeline.md` — capture-side pipeline reference.
- `firmware/marvin/docs/display_path.md` — display-side wiring reference.
- `firmware/marvin/docs/journal.md` — chronological log of every test session and decision.
- `lcd` git branch — full commit history of all the experiments described here. Preserved as historical record.
