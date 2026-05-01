# marvin — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for the marvin firmware. Newest entries at the top.

---

## Project north star

Vision-based guitar-playing robot. The marvin firmware captures live HDMI video from a Nintendo Wii (Guitar Hero / Rock Band), runs vision on it, and drives actuators to fret and strum a real guitar.

- **Target:** SAM9X75 Curiosity (MPLAB Harmony v5.6.4, SAM9X7 DFP 1.9.170, vision v3.2.1)
- **Video source:** Wii → ElectronWarp (component → HDMI, 480p60) → Waveshare HDMI-to-CSI-2 adapter (TC358743) → SAM9X75 MIPI CSI-2 RX → ISC → SDRAM
- **Display:** Legato GUI (already showing a basic screen as of commit `3bb441a`)

---

## Current focus

**TC358743 HDMI-to-CSI-2 bridge driver** — first real driver work on marvin. The bridge is the gate between "we have hardware wired up" and "we have pixels in memory."

### Hardware context

- **Bridge board:** Waveshare HDMI to CSI-2 Adapter v1/v2 (2-lane CSI, 27 MHz on-board XTAL, expected I2C address `0x0F`).
- **Source chain:** Wii → ElectronWarp (https://electron-shepherd.com/collections/all/products/electronwarp) → HDMI 480p60 → Waveshare → CSI-2 → SAM9X75.
- **Marvin pre-existing scaffolding:**
  - I2C: FLEXCOM6 @ 400 kHz (see `default/src/config/default/configuration.h:102`)
  - CSI driver: `default/src/config/default/vision/drivers/csi/` — configured for 2 data lanes (`CSI_NUM_LANES = CSI_DATA_LANES_2`, `configuration.h:167`)
  - ISC driver: `default/src/config/default/vision/drivers/isc/` — `enableMIPI = true`
  - CSI2DC bridge driver present
  - GPIOs: `CAMERA_RESET` = PC19 (active-high), `CAMERA_PWD` = PC15 (active-low), defined in `default/src/config/default/pin_configurations.csv`

### Reference sources (agreed)

- **Primary:** mainline Linux driver at `../linux-at91/drivers/media/i2c/tc358743.c` (2385 LOC) + `tc358743_regs.h` (782 LOC) + `include/media/i2c/tc358743.h`.
- **Secondary:** community / Waveshare 480p60 register dumps for the actual initialization values.
- **Emirror project** (`firmware/sam9x75_curiosity_emirror/`) as a template for how a camera-ish driver integrates with Harmony's ISC/CSI — NOT as a register reference (it drives IMX219/OV5640/OV2640/OV5647, no bridge chip).

### Integration approach — DEFERRED

Three options on the table:

1. Pretend TC358743 is a sensor — port emirror's `drv_image_sensor` framework and add a `tc358743` entry alongside IMX219/OV5640. Least new code; reuses libcamera abstraction.
2. Standalone `drv_hdmi_bridge` module. Cleaner semantically (a bridge isn't a sensor — no exposure, gain, AE/AWB), but more new code.
3. **Chosen:** Start probe/chip-ID code in a throwaway location, revisit once we see how complex init and runtime handling actually are.

### Phased plan

#### Phase 0 — Resolve open hardware/config questions

Software-answerable questions are closed out (see decision log). Remaining items need hardware inspection / bench checks:

Status of original hardware items:

- ~~PC19/PC15 physically wired to Waveshare RESET~~: superseded. Decided to use software reset over I2C (`SYSCTL.Sreset`) for Phase 1 — Waveshare v1/v2 typically doesn't expose TC358743 `RESET_N` to the 15-pin FFC anyway. GPIO-driven RESET can be revisited in Phase 3+ if useful for error recovery.
- ~~TC358743 INT on a SAM9X75 GPIO~~: deferred. We'll poll HDMI SYS_STATUS during bring-up; add IRQ wiring only if polling proves inadequate.
- [x] **Source chain verified (2026-05-01):** Waveshare board powered; HDMI 480p confirmed from a separate display.

Nothing blocking Phase 1. I2C path is unambiguous: FLEXCOM6 on PA24/PA25 → J29 pins 21/20 → 22→15 adapter → Waveshare CAM_I²C → TC358743 at `0x0f`.

#### Phase 1 — I2C probe & chip ID

Smallest code that proves I2C works and the chip is present.

**File layout (throwaway-friendly):**

- `default/src/tc358743.h` — public API: `TC358743_Initialize(void)`, `TC358743_Tasks(void)`.
- `default/src/tc358743.c` — implementation.

Kept flat in `default/src/` (not under a `drivers/` subfolder). Phase 3 is where we decide the permanent shape (sensor framework vs standalone).

**Integration touchpoints in marvin's existing scaffold:**

- `app.c:APP_Initialize()` → add `TC358743_Initialize();` after the initial state assignment.
- `app.c:APP_Tasks()` → add `TC358743_Tasks();` unconditionally at the top of the function (it has its own internal state machine, so the app's outer `APP_STATE_*` switch doesn't gate it).
- `app.h` → no changes needed for Phase 1.
- The FLEXCOM6 I2C driver is already brought up by `SYS_Initialize` (`initialization.c:360`, `sysObj.drvI2C0 = DRV_I2C_Initialize(...)`), so the TC358743 module only needs `DRV_I2C_Open(DRV_I2C_INDEX_0, DRV_IO_INTENT_EXCLUSIVE)`.
- `printf` is retargeted to DBGU via `default/src/config/default/stdio/xc32_monitor.c` — standard `printf("...")` works out of the box.

**Internal state machine (inside `tc358743.c`):**

| State | What it does | Next state on success |
|---|---|---|
| `INIT` | `DRV_I2C_Open`, register transfer-event callback, print banner | `SWRESET_ASSERT` |
| `SWRESET_ASSERT` | Queue write to `SYSCTL` with Sreset bit set (see wire-level table below) | `SWRESET_WAIT_ASSERT` |
| `SWRESET_WAIT_ASSERT` | Wait for completion callback, then start ≥1 ms delay | `SWRESET_RELEASE` |
| `SWRESET_RELEASE` | Queue write to `SYSCTL` clearing Sreset bit | `SWRESET_WAIT_RELEASE` |
| `SWRESET_WAIT_RELEASE` | Wait for completion callback, then start ≥1 ms delay | `READ_CHIPID` |
| `READ_CHIPID` | Queue `WriteReadTransferAdd` (reg addr write + 2-byte read at slave `0x0f`) | `REPORT` |
| `REPORT` | `printf` the result, evaluate pass condition, transition to `DONE` | `DONE` |
| `DONE` | Idle — no-op | `DONE` |
| `ERROR` | Printed in the callback; no retry in Phase 1 | `ERROR` |

The transfer-event callback sets a flag + stashes the `DRV_I2C_TRANSFER_EVENT` result; `TC358743_Tasks()` polls that flag to advance state.

**Wire-level transactions:**

- **SYSCTL Sreset assert** — 4-byte I2C write to slave `0x0f`:
  - Bytes: `0x00 0x02 0x01 0x00` (register addr `0x0002` MSB-first, value `0x0001` little-endian — matches kernel driver's `i2c_wrreg`).
- **SYSCTL Sreset release** — 4-byte I2C write to slave `0x0f`:
  - Bytes: `0x00 0x02 0x00 0x00` (value `0x0000`).
- **CHIPID read** — 2-byte write then 2-byte read at slave `0x0f`:
  - Write phase: `0x00 0x00` (register addr `0x0000` MSB-first).
  - Read phase: 2 bytes, little-endian — stored as `rx[0]` = low byte (revision, `MASK_REVID = 0x00ff`), `rx[1]` = high byte (chip-id field, `MASK_CHIPID = 0xff00`).

**Pass condition (matches kernel driver, `tc358743.c:2210-2215`):**

- No I2C error on any of the three transactions.
- `rx[1]` (high byte of CHIPID) is `0x00`.

If both hold, we print e.g. `"TC358743: present (chipid=0x%02X%02X)\n"` and advance to `DONE`. Otherwise we print the error and halt in `ERROR`.

Note the kernel driver's weak identity check: upper byte must be zero, but the lower byte (revision) isn't constrained. Our probe follows the same convention — we don't hard-code an expected revision.

**Delay strategy:**

TC358743 datasheet requires ≥1 ms after Sreset before the next I2C transaction. Use Harmony's `SYS_TIME` service — it's already initialized in `SYS_Initialize` (`initialization.c:376`) and marvin's own ISC code uses it (`drv_isc.c:32-34`, `plib_isc.c:12-15`).

Non-blocking pattern inside the state machine: the `SWRESET_WAIT_*` states call `SYS_TIME_DelayMS(1, &handle)` once (on entry) and then `SYS_TIME_DelayIsComplete(handle)` each tick until true, at which point the state advances. Avoids blocking `APP_Tasks()` (and Legato rendering with it) during the delay. The full init in Phase 3 will have many more delays, so the non-blocking shape earns its keep.

**Buffers (static, module-scoped, lifetime spans the async transaction):**

- `static uint8_t txBuf[4];`
- `static uint8_t rxBuf[2];`
- `static DRV_HANDLE i2cHandle;`
- `static volatile DRV_I2C_TRANSFER_EVENT lastEvt;` (written by the ISR-context callback; read by `_Tasks`)
- `static volatile bool evtReady;` (callback→Tasks handshake)

**Error cases we expect to debug during bring-up:**

- I2C NAK on the first SYSCTL write → wrong slave address (`0x0f` wrong — check A0 pin tie-off on Waveshare), or the cable/adapter is backwards, or PA24/PA25 aren't muxed to FLEXCOM6.
- `rxBuf = {0xff, 0xff}` → no ACK but our code didn't detect the NAK — would indicate a driver-level issue, not the chip.
- Nothing on DBGU at all → DBGU retarget path broken, or we're hanging in `DRV_I2C_Open`.

**Exit criteria:**

DBGU output contains `TC358743: present (chipid=0x...)` with the upper byte equal to `0x00`, after a power cycle of the board. No I2C errors printed.

**Explicitly out of scope for Phase 1 (deferred to later phases):**

- Hardware RESET via PC19 (see 2026-05-01 decision — software reset only).
- Retries, timeouts, or recovery from `ERROR`.
- Reading revision and printing the Toshiba datasheet name.
- Any HDMI / CSI register programming.
- Hooking into the `drv_image_sensor` framework.

#### Phase 2 — Register access helpers

TC358743 has 16-bit register addresses and mixed 8/16/32-bit data widths. Emirror's sensor helpers only cover 8/16-bit addr + byte / 2-byte data — not enough. Port the Linux driver's `i2c_wr8/wr16/wr32` + `i2c_rd8/rd16/rd32` against FLEXCOM6 TWI. Direct map from the kernel driver.

#### Phase 3 — Initialization sequence

Port from Linux `tc358743_initial_setup()` and its callees:

- Software reset
- `SYS_CTRL` clock muxing (depends on 27 MHz REFCLK)
- HDMI PHY config
- CSI-TX PLL + D-PHY timing (Linux driver computes from a `tc_data` struct — we hardcode for 2-lane 480p60)
- EDID RAM load (copy the 256-byte blob from the Linux driver; advertises 1080p/720p/480p)
- Enable HDMI input-format detection

This is the point where "throwaway file" becomes a real module. Revisit integration question (sensor framework vs standalone) here.

#### Phase 4 — Format detection & start CSI streaming

- Poll (or IRQ on TC358743's INT pin) the HDMI SYS_STATUS register until source is locked and the format matches expectation.
- Program CSI-TX output for 480p60 in the chosen pixel format.
- Release CSI-TX stop state → pixel data flows.

#### Phase 5 — Hook into marvin's ISC/CSI capture

- Call `tc358743_start()` after `SYS_Initialize()`.
- Arm ISC DMA into a framebuffer in SDRAM.
- Verify via ISC frame-complete IRQ / frame counter that frames are landing.

#### Phase 6 — First rendered frame

- Display the captured buffer on the existing Legato surface. Fix format/stride mismatches here.

### Known risks

1. **TC358743 CSI-TX minimum bitrate.** At 480p60 the native pixel rate is low enough that the Linux driver runs CSI-TX faster than the source and pads with blanking. We'll likely inherit that approach, and SAM9X75 ISC must tolerate it.
2. **ISC ↔ TC358743 handshake.** ISC expects specific CSI-2 frame-start/frame-end conventions. Format tag mismatches (YUV422 8-bit vs 10-bit, etc.) manifest as "no frames" and are painful to debug.
3. **EDID.** If the Wii sees a bad/missing EDID it may fall back to 480i or refuse to output. Test the source independently before blaming the driver.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-05-01 | First real driver work is the TC358743 HDMI-to-CSI bridge | It's the gate between hardware-wired and pixels-in-memory. Everything downstream (vision, actuation) depends on it. |
| 2026-05-01 | Reference source = mainline Linux tc358743 driver + community 480p dumps | Linux driver has correct state machine / EDID / IRQ handling; community dumps have verified 480p register values. |
| 2026-05-01 | Integration shape (sensor vs standalone) deferred until Phase 3 | Avoid premature structure; real answer depends on how much state the driver actually needs. |
| 2026-05-01 | Journal lives at `firmware/marvin/docs/journal.md` | Firmware docs co-located with firmware; top-level `docs/` is hardware/mechanical. |
| 2026-05-01 | TC358743 I2C address: **`0x0f`** (7-bit) | Matches the in-tree kernel DT example (`imx6q-h100.dts:190`, `tc358743@f`) and the Waveshare v1/v2 default tie-off. Confirm by I2C bus scan during Phase 1 bring-up. |
| 2026-05-01 | REFCLK: **27 MHz** | Waveshare v1/v2 on-board XTAL. Kernel driver restricts REFCLK to {26, 27, 42} MHz (`tc358743.c:717-719`); 27 MHz is the Waveshare v1/v2 value. |
| 2026-05-01 | Target CSI-2 output format: **RGB888** (MIPI data type `0x24`) | Marvin pipeline is already configured for RGB888 end-to-end: `CSI_DATA_FORMAT_TYPE = CSI2_DATA_FORMAT_RGB888`, ISC input RGB 8-bit, CSI2DC pipes RGB888, ISC output ARGB32. Emitting YUV from TC358743 would require reconfiguring ISC/CSI2DC — staying with RGB is cheaper. |
| 2026-05-01 | Register-access helpers will be a direct port from the kernel driver | `tc358743.c:131-269` has clean `i2c_rd/wr` + `i2c_rd8/wr8/rd16/wr16/rd32/wr32`. Map to FLEXCOM6 TWI underneath. |
| 2026-05-01 | J29 (22-pin MIPI) pinout confirmed; PC19=pin 17, PC15=pin 18 (not 5/6) | Per SAM9X75-Curiosity User Guide §3.4.10 (queried via Microchip MCP). Pins 5/6 are MIPI_D1_N/P — CSI data lane 1 diff pairs, not GPIOs. Correct GPIO pins for RESET/INT are pin 17 (PC19, `MIPI_CSI_GPIO0`) and pin 18 (PC15, `MIPI_CSI_GPIO1`). I2C on pins 20 (PA25, `CAM_I²C_CLK`) and 21 (PA24, `CAM_I²C_DATA`) confirmed to match marvin's FLEXCOM6 pin assignment (`pin_configurations.csv:85-86`). |
| 2026-05-01 | Phase 1 will drive RESET via software (I2C `SYSCTL.Sreset`), not via GPIO | Even if PC19 reaches the 15-pin side through the adapter, Waveshare v1/v2 typically does NOT route TC358743 `RESET_N` out to the 15-pin FFC — the chip's reset is an on-board RC/POR network. Software reset over I2C is always available and is what the kernel driver uses. Means we don't need to validate the GPIO path before Phase 1 bring-up. |
| 2026-05-01 | Delays use Harmony's `SYS_TIME` service (non-blocking shape) | Already initialized in `SYS_Initialize`; existing marvin code (`drv_isc.c`, `plib_isc.c`) uses it with `SYS_TIME_DelayUS/MS` + `SYS_TIME_DelayIsComplete`. Non-blocking pattern inside our state machine keeps `APP_Tasks()` (and Legato) from stalling during reset hold times, and scales naturally into Phase 3's longer init sequence. |

---

## Open questions

_(Questions we haven't answered yet. Move to decision log with rationale once resolved.)_

- **Bayer-pipeline bypass for RGB input.** Marvin's MCC config leaves ISC Bayer blocks enabled — `ISC_ENABLE_DPC`, `ISC_ENABLE_GDC`, `ISC_ENABLE_WHITE_BALANCE`, `ISC_ENABLE_GAMMA`, `ISC_BAYER_PATTERN_TYPE = ISC_CFA_CFG_BAYCFG_RGRG_Val` (see `configuration.h:117-143`). These apply to Bayer input only and are inconsistent with the `DRV_IMAGE_SENSOR_RGB` input format. Figure out during Phase 5 whether `drv_isc.c` already routes around them when `inputFormat = RGB`, or whether we need to disable them in MCC / config.
- CSI-TX minimum bitrate at 480p60. Linux driver's `tc_data` CSI-TX PLL config for 480p60 is the authoritative reference — pull those numbers during Phase 3 and check they're within SAM9X75 ISC RX tolerance.

---

## Session log

### 2026-05-01 — Planning kickoff

- Surveyed marvin and emirror to map existing I2C / CSI / ISC scaffolding and template patterns. (See "Current focus" above for findings.)
- Confirmed mainline Linux TC358743 driver is present locally in `linux-at91` and usable as the primary reference.
- Agreed phased plan (Phases 0–6 above) and the initial decisions in the decision log.
- Journal started; added project-wide rules in `CLAUDE.md` (always read/update the journal; keep code comments lean).

### 2026-05-01 — Phase 1 implemented (pending hardware verification)

Code written, not yet built/flashed:

- `default/src/tc358743.h` — public API: `TC358743_Initialize`, `TC358743_Tasks`.
- `default/src/tc358743.c` — probe state machine per plan above. Async I2C via `DRV_I2C_WriteTransferAdd` / `DRV_I2C_WriteReadTransferAdd` with a single shared transfer-event callback. Non-blocking delays via `SYS_TIME_DelayMS` + `SYS_TIME_DelayIsComplete`.
- `default/src/app.c` — `#include "tc358743.h"`, `TC358743_Initialize()` in `APP_Initialize`, `TC358743_Tasks()` at the top of `APP_Tasks`.
- `cmake/marvin/default/user.cmake` — appends `tc358743.c` to the existing `marvin_default_default_XC32_compile` OBJECT target. Uses `user.cmake` because `.generated/file.cmake` is marked "do not modify directly" (MCC regenerates it).

Note on MPLAB X: if the project is built from MPLAB X IDE rather than CLI CMake, `tc358743.c` also needs to be added to the IDE project file set (normally via MCC or the IDE's "Add Existing Item"). The `user.cmake` covers the CMake build path only.

Pending: confirm build succeeds, flash, watch DBGU for `TC358743: present (chipid=0x00XX)`.

### 2026-05-01 — Phase 1 detailed plan written

Filled out the Phase 1 plan above with file layout, integration touchpoints, state machine, wire-level transactions, pass condition, delay strategy, buffer lifetime, expected error modes, and scope cuts. No code written yet — stopping here for review before implementation.

Key detail surfaced during planning: the kernel driver's CHIPID probe check (`tc358743.c:2210-2215`) is a loose "upper byte == 0" test, not a hardcoded magic number match. Our probe follows the same convention — we accept any revision.

### 2026-05-01 — Phase 0, part 2 (hardware questions closed)

- Corrected J29 22-pin pinout via Microchip MCP query (User Guide §3.4.10): PC19 is on **pin 17** (`MIPI_CSI_GPIO0`), PC15 is on **pin 18** (`MIPI_CSI_GPIO1`). Pins 5/6 are CSI data lane 1 diff pairs, not GPIOs. I2C (PA24/PA25) on pins 21/20 confirmed against marvin's FLEXCOM6 pin assignments.
- Dropped the GPIO-RESET requirement for Phase 1: Waveshare v1/v2 typically doesn't route TC358743 `RESET_N` out to the 15-pin FFC. Plan is to use software reset (`SYSCTL.Sreset`) over I2C, matching what the kernel driver does.
- Dropped the INT-GPIO requirement for Phase 1: polling SYS_STATUS is fine for bring-up.
- Source chain confirmed by Greg: Waveshare board powered; HDMI 480p visible on a separate display.

Phase 0 is closed. Next up: Phase 1 — I2C probe + CHIPID read.

### 2026-05-01 — Phase 0, part 1 (software-answerable questions)

Closed four Phase 0 questions from code + kernel-driver inspection (see decision log for details):

- I2C address = `0x0f` (7-bit).
- REFCLK = 27 MHz (Waveshare v1/v2 XTAL; kernel driver only accepts 26/27/42 MHz).
- Target CSI-2 output format = RGB888 — marvin pipeline is already wired end-to-end for RGB888 → ISC → ARGB32 in SDRAM.
- Phase 2 register helpers will be a near-verbatim port of the kernel driver's `i2c_rd/wr*` family, mapped to FLEXCOM6 TWI.

New open question surfaced by the RGB format finding: marvin's ISC still has Bayer-pipeline blocks (DPC, WB, gamma, Bayer pattern) enabled from MCC defaults, inconsistent with RGB input. Deferred to Phase 5.

Remaining Phase 0 questions are all hardware-side and need bench/schematic inspection — see "Open questions."
