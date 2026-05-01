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

#### Phase 1 — I2C probe & chip ID ✅ COMPLETE (2026-05-01)

Smallest code that proves I2C works and the chip is present.

**Result:** DBGU reports `TC358743: present (chipid=0x0000)` on every boot — TC358743 is on the bus at `0x0f`, ACKs our transactions, accepts the software reset, and returns a clean CHIPID read. Full I2C path validated end-to-end: FLEXCOM6 → PA24/PA25 → J29 → 22→15 adapter → Waveshare → TC358743.

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

#### Phase 2 — Register access helpers ✅ CODE COMPLETE (2026-05-01, pending flash verify)

TC358743 has 16-bit register addresses and mixed 8/16/32-bit data widths. Port the Linux driver's `i2c_wr*` + `i2c_rd*` family, one-for-one, against FLEXCOM6 TWI.

**API (module-internal, `static` in `tc358743.c`):**

```c
static bool tc358743_wr(uint16_t reg, const uint8_t *vals, size_t n);
static bool tc358743_rd(uint16_t reg, uint8_t *vals, size_t n);
static bool tc358743_wr8(uint16_t reg, uint8_t val);
static bool tc358743_wr16(uint16_t reg, uint16_t val);
static bool tc358743_wr32(uint16_t reg, uint32_t val);
static bool tc358743_rd8(uint16_t reg, uint8_t *val);
static bool tc358743_rd16(uint16_t reg, uint16_t *val);
static bool tc358743_rd32(uint16_t reg, uint32_t *val);
static bool tc358743_wr16_and_or(uint16_t reg, uint16_t mask, uint16_t val);
```

All return `true` on success, `false` on any I2C error. Values are little-endian on the wire (matches kernel driver's `i2c_wrreg`/`i2c_rdreg` via `cpu_to_le32`/`le32_to_cpu`). Register addresses go out big-endian (MSB first) — also matches kernel driver.

**Sync mechanism:**

Each helper runs synchronously from the caller's view:

1. Clear module-scoped `xferDone` / `xferErr` flags.
2. Issue `DRV_I2C_WriteTransferAdd` / `DRV_I2C_WriteReadTransferAdd`.
3. Spin on `while (!xferDone && !xferErr)`.
4. Return success/failure.

A single shared transfer-event callback flips the flags. FLEXCOM TWI is interrupt-driven on SAM9X7, so the spin loop doesn't need to pump anything — the ISR fires and the flag flips.

**Blocking implications:**

Each helper blocks for the duration of its I2C transaction (~100-500 µs at 400 kHz depending on size). Phase 3's init sequence may do 50+ transactions plus an EDID load; total boot-time block is expected to be tens of ms. Legato's initial animation may show a brief pause at startup. Acceptable — init runs once at boot.

If any Phase 3+ use case needs to issue I2C without blocking (e.g. runtime format change), we add an async variant then. YAGNI for now.

**Refactor of Phase 1:**

Replace the ~10-state machine with imperative code in `TC358743_Initialize`:

```c
open i2c
register callback
tc358743_wr16(SYSCTL, SRESET);
delay_ms(1);
tc358743_wr16(SYSCTL, 0);
delay_ms(1);
tc358743_rd16(CHIPID, &id);
print
```

`TC358743_Tasks` becomes empty for now — reserved for Phase 4 runtime work (format-change polling).

**Buffer sizing:**

Small static buffer (8 bytes tx + 4 bytes rx) handles all Phase 2 needs — reg addr (2 bytes) + up to 4-byte data. Phase 3's EDID load needs a larger buffer; we'll add a dedicated bulk-write helper then (or grow `txBuf` with a `#define`).

**File structure:**

Stay monolithic (`tc358743.c` / `tc358743.h`). Split only when file grows past 300-400 lines — likely sometime in Phase 3 or 4.

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
| 2026-05-01 | Our I2C open uses `DRV_IO_INTENT_READWRITE`, not `EXCLUSIVE` | `EXCLUSIVE` is rejected by Harmony's I2C driver if any other client is already open. Even after libcamera is removed we prefer `READWRITE` so our module can coexist with any future MCC-generated I2C client. `DRV_I2C_CLIENTS_NUMBER_IDX0 = 2` in `configuration.h` already permits multi-client use. |
| 2026-05-01 | Libcamera (Camera Module + Image Sensor Driver + Vision Camera Library) to be removed from MCC | Conflicts with TC358743 bridge approach: libcamera expects a directly-attached sensor (IMX219/OV5640/etc.), probes them at boot (adds bus traffic + console noise), and configures ISC/CSI/CSI2DC with sensor-style assumptions that we'll need to override in Phase 3. Keeping CSI/ISC/CSI2DC peripheral/driver layers — we'll drive those directly from the TC358743 module. Greg regenerating MCC. |
| 2026-05-01 | Post-regen: keep a minimal `drv_image_sensor.h` shim at the original path | ISC driver (`drv_isc.c:15, 246, 257, 363-365`) and `configuration.h:106-107` reference `DRV_IMAGE_SENSOR_*` enum values that MCC didn't scrub when the image-sensor component was removed. Shim defines just the enum values used (exact numeric equivalence to originals). Lives at the MCC-generated path because `drv_isc.c`'s include is hardcoded — but MCC no longer regenerates that directory, so the shim is stable. |
| 2026-05-01 | `CAMERA_ENABLE_DEBUG=0` provided via `user.cmake` compile definition | Both `drv_csi.c:45` and `drv_isc.c:18` define `debug_print(...) if (CAMERA_ENABLE_DEBUG) fprintf(...)` and the symbol used to come from the deleted `camera.h`. Define it as a compile flag instead of editing `configuration.h` (MCC-regenerated) — `user.cmake` survives regens. Value `0` elides the debug prints entirely; if we ever need them, bump to `1`. |
| 2026-05-01 | ISC Bayer blocks auto-bypass for RGB input (previously an open question) | `drv_isc.c:363-365` disables CFA/WB/Gamma/CSC/Sub422/Sub420 whenever `inputFormat == DRV_IMAGE_SENSOR_RGB` — no MCC config changes needed despite `ISC_ENABLE_DPC/GDC/WHITE_BALANCE/GAMMA` remaining `true` in `configuration.h`. Those flags are Bayer-path only and are ignored in the RGB branch. |

---

## Open questions

_(Questions we haven't answered yet. Move to decision log with rationale once resolved.)_

- CSI-TX minimum bitrate at 480p60. Linux driver's `tc_data` CSI-TX PLL config for 480p60 is the authoritative reference — pull those numbers during Phase 3 and check they're within SAM9X75 ISC RX tolerance.

---

## Session log

### 2026-05-01 — Planning kickoff

- Surveyed marvin and emirror to map existing I2C / CSI / ISC scaffolding and template patterns. (See "Current focus" above for findings.)
- Confirmed mainline Linux TC358743 driver is present locally in `linux-at91` and usable as the primary reference.
- Agreed phased plan (Phases 0–6 above) and the initial decisions in the decision log.
- Journal started; added project-wide rules in `CLAUDE.md` (always read/update the journal; keep code comments lean).

### 2026-05-01 — Phase 2 implemented

Replaced Phase 1's ~10-state async machine with sync helpers + imperative probe. `tc358743.c` went from ~200 lines to ~180 with far less state. API matches the kernel driver naming convention (`tc358743_wr8/16/32`, `tc358743_rd8/16/32`, `tc358743_wr16_and_or`, and generic `tc358743_wr/rd`). Phase 1's observable behavior is unchanged — same DBGU output, same probe semantics. `TC358743_Tasks` is now empty, reserved for Phase 4 format-change polling.

Ready for flash verification before starting Phase 3.

### 2026-05-01 — Phase 1 passes on hardware

With the shim header and `CAMERA_ENABLE_DEBUG=0` in place, build succeeds and first boot produces:

```
TC358743: probe starting
TC358743: present (chipid=0x0000)
```

`chipid=0x0000` satisfies the kernel driver's probe rule (high byte must be zero). I2C path from SAM9X75 FLEXCOM6 → J29 pins 20/21 → 22→15 adapter → Waveshare → TC358743 is fully validated. Software reset over I2C confirmed working. Non-blocking state machine and `SYS_TIME` delays behave as designed.

Phase 1 is closed. Ready to start on Phase 2 (register access helpers) when we come back.

### 2026-05-01 — Post-regen build break: ISC depends on image-sensor enums

After MCC regen removed libcamera + image-sensor driver + vision camera library, the build breaks because:

- `drv_isc.c:15` still `#include`s the deleted `drv_image_sensor.h`.
- `drv_isc.c:246, 257, 363-365` use `DRV_IMAGE_SENSOR_*` enum values.
- `configuration.h:106-107` uses `DRV_IMAGE_SENSOR_RGB` and `DRV_IMAGE_SENSOR_8_BIT`.

MCC's vision ISC component carried a dependency on the image-sensor driver's types without providing them itself when image_sensor is removed. Not something we can fix in MCC.

**Fix applied:** minimal shim `vision/drivers/image_sensor/drv_image_sensor.h` containing only the enum declarations used, with numeric values preserved. Since MCC no longer knows about the image_sensor component, it won't regenerate over this path.

**Also confirmed during this investigation:** `drv_isc.c:363-365` auto-bypasses the Bayer blocks when `inputFormat == DRV_IMAGE_SENSOR_RGB`, so one of our earlier open questions is now answered without needing config changes.

### 2026-05-01 — Phase 1 first flash: I2C handle conflict; libcamera removal decided

First flash revealed two issues:

1. **I2C open conflict.** MCC-configured libcamera (`drv_image_sensor.c:343`) opens `DRV_I2C_INDEX_0` with `DRV_IO_INTENT_READWRITE` during `SYS_Initialize`, before our `APP_Initialize` runs. Our `DRV_IO_INTENT_EXCLUSIVE` open then fails because Harmony's I2C driver rejects `EXCLUSIVE` when any other client is already attached. **Fix applied:** changed `tc358743.c` to use `DRV_IO_INTENT_READWRITE`. The driver's `DRV_I2C_CLIENTS_NUMBER_IDX0 = 2` in `configuration.h` already allows two simultaneous clients, so coexistence is fine.
2. **Libcamera is running and noisy.** `initialization.c:363` calls `CAMERA_Initialize()` which probes IMX219/OV5640/OV2640/OV5647 on the bus, fails (no such sensor present), and logs "`Image Sensor probe failed.`", "`Camera Open Error`". Beyond console noise, libcamera also configures ISC/CSI/CSI2DC for its own expected pipeline — this would conflict with Phase 3 when we need to drive those blocks from TC358743 settings.

**Decision:** Greg will remove Camera Module, Image Sensor Driver, and Vision Camera Library from MCC and regenerate. Keeping CSI / ISC / CSI2DC low-level drivers since we'll drive them directly from our TC358743 module in later phases. After regen, I'll verify the new config still has I2C, CSI, ISC, CSI2DC available and our `tc358743.c` still builds.

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
