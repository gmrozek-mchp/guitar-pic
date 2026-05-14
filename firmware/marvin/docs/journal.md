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

**End-to-end pipeline working as of 2026-05-02: source → TC358743 → CSI-2 @ 972 Mbps/lane → SAM9X75 → ISC DMA → DDR (BGRX32) → XLCDC OVR1 → LVDSC → 10.1" 1280×800 LVDS panel.**

> **Configuration references:**
> - [`capture_pipeline.md`](capture_pipeline.md) — HDMI → DDR capture stage (CSI2DC, ISC, register settings, datasheet citations).
> - [`display_path.md`](display_path.md) — DDR → LCD display stage (XLCDC OVR1 wiring, pillarbox, cache coherence).

Phase 5b (byte-level content verification) ✅. Phase 6 MVP (captured video on the LCD) ✅, now displaying 480p sources pillarboxed/letterboxed and 720p sources letterbox-only on the 1280×800 panel. Phase 7a/c (480p via Pi and Wii) ✅. Next: Phase 8 (vision stage) — HEO scaling no longer needed for 720p since the bigger panel accommodates native, only relevant if 1080p sources come into scope.

### Original focus (for context — this is done)

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

#### Phase 3 — Initialization sequence ✅ PASSES ON HARDWARE (2026-05-01; EDID deferred to 3b)

Port the minimal set of `tc358743_initial_setup()` + callees from the kernel driver to bring the chip from "responsive on I2C" (end of Phase 2) to "configured and awaiting HDMI source lock" (ready for Phase 4's stream-enable).

**Landed in this pass:** reset hold (IR/CEC), CTX/HDMI reset, wake, FIFOCTL, `set_ref_clk` (27 MHz), DDC_CTL delay, EDID_MODE=E-DDC, `set_hdmi_phy` (PHY enable, CTL1/2, BIAS, CSQ, AVM, HDMI_DET, HV_RST), VI_MODE (RGB/DVI), VOUT_SET2/3 (auto color mode), `set_pll` (PRD=4, FBD=88, FRS=0 for 594 Mbps/lane), `set_csi` (2 lanes, D-PHY timings, CSI_START, CSI_CONFW sequence), `set_csi_color_space_rgb888`.

**Deferred to Phase 3b:** EDID blob load into EDID_RAM. Hand-constructing a valid 128-byte EDID with correct checksum is an error-prone step, and getting the init logic right is a separate concern. Without an EDID, the HDMI source (ElectronWarp) will likely see "no sink" on DDC and either refuse to output or fall back to a failsafe. Phase 3 flashed state will let us observe that and confirm the init side of things is correct.

**Deferred to Phase 4:** HPD enable (`HPD_CTL`), `init_interrupts`, `enable_stream()`. HPD really belongs with EDID — the source re-reads EDID on HPD assertion, so we enable HPD after EDID is programmed.

**Pass criteria for this commit:** build succeeds; DBGU shows each step succeeding with a final `SYS_STATUS=0xXX` readout. Exact `SYS_STATUS` bits depend on source state — without HPD asserted yet, `DDC5V` bit may read 1 (we'd detect source 5V) but TMDS/PHY lock bits will be 0. That's fine.

**Target configuration (hardcoded, not parameterized):**

| Parameter | Value | Source |
|---|---|---|
| REFCLK | 27 MHz | Waveshare v1/v2 on-board XTAL |
| CSI lanes | 2 | marvin CSI_NUM_LANES and Waveshare v1/v2 |
| PLL_PRD | 4 | kernel driver + emirror working baseline |
| PLL_FBD | 44 | **changed from 88 during Phase 5 debug** (594 → 297 Mbps) |
| CSI bits/lane | 297 Mbps | `(27 MHz / 4) * 44`, pairs with host HSFREQRANGE band 0x14 |
| FIFO_LEVEL | 374 | kernel default; works at 297 Mbps too |
| CSI output format | RGB888 (MIPI DT 0x24) | marvin ISC expects RGB888 |

**D-PHY timing counts for 594 Mbps** (from `tc358743.c:2113-2125`):
`LINEINITCNT=0xE80`, `LPTXTIMECNT=0x003`, `TCLK_HEADERCNT=0x1403`, `TCLK_TRAILCNT=0x00`, `THS_HEADERCNT=0x0103`, `TWAKEUP=0x4882`, `TCLK_POSTCNT=0x008`, `THS_TRAILCNT=0x2`, `HSTXVREGCNT=0`.

**Minimal init sequence (in order):**

1. `tc358743_reset(MASK_CTXRST | MASK_HDMIRST)` — reset CSI-TX ctx + HDMI RX blocks (`tc358743.c:2-line helper`).
2. `tc358743_sleep_mode(false)` — clear CONFCTL.SleepM so chip wakes.
3. `wr16(FIFOCTL, 374)`.
4. `set_ref_clk()` — `SYS_FREQ0/1`, `SYS_SYSCLK_IND`, PHY lock-detect counts, NCO_F0_MOD (for 27 MHz).
5. `set_hdmi_phy()` — `PHY_EN`, `PHY_CTL1/2`, `PHY_BIAS`, `PHY_CSQ`, `AVM_CTL`, `HDMI_DET`, `HV_RST`.
6. VI_MODE, VOUT_SET2/3 — RGB DVI detect + color-mode auto-detect.
7. `set_pll()` — `PLLCTL0/1` with PRD/FBD, lock, enable.
8. `set_csi()` — program all D-PHY timing counts, enable lanes 0/1 (CLW/D0W/D1W), `CSI_START`, `CSI_CONFW`.
9. `set_csi_color_space(RGB888)` — `VOUT_SET2` (clear 422), `VI_REP` (color full RGB), `CONFCTL` (clear YCbCrFmt).
10. Load EDID into EDID_RAM (0x8C00..), set `EDID_LEN1/LEN2`.
11. Enable HPD via `HPD_CTL` so source sees a valid sink.
12. `init_interrupts()` — clear all status registers (no IRQ wiring yet; we'll poll in Phase 4).

Phase 3 stops at step 12 — no `enable_stream()` yet (that's Phase 4 after we confirm source lock).

**EDID:** single 128-byte block, hand-constructed, declaring 640x480@60 (480p) as the only detailed timing, plus standard VESA timings. Sufficient for the Wii → ElectronWarp to see "480p sink" and emit 480p60. If source refuses or falls back, we expand EDID to 256 bytes / add 720p60.

**Register definitions:** add only the ~30 registers and masks we actually use to `tc358743.c` as `#define`s. Not worth importing the kernel's 782-line `tc358743_regs.h` wholesale.

**Explicitly skipped (matches Phase 3 scope cuts in the Agent's survey):**

- CEC subsystem — firmware has no use for HDMI CEC.
- Audio subsystem — we ignore HDMI audio entirely; no I²S out.
- HDCP — source is a Wii, no content protection to bypass or enforce.
- V4L2 DV timings framework — we hardcode 480p60.
- Infoframe parsing — don't need AVI/SPD/ACP parsed out.
- HPD delayed-work / debouncing — direct register write, no worker.
- Interrupt handler — not wired yet (Phase 4 decision: poll vs wire INT GPIO).

**Pass criteria for Phase 3:**

- Build succeeds, flash boots.
- DBGU prints a trace of each init step with success/fail.
- No I2C errors during init.
- After Phase 3 completes, `SYS_STATUS` reads a non-zero value (exact bits depend on whether HDMI source is connected and locked — but the register itself must be readable without error).

**Where Phase 3 lives:**

Monolithic — keep everything in `tc358743.c` / `tc358743.h`. File is currently ~180 lines; after Phase 3 it'll be ~500. Split only if Phase 4 pushes past 700 or so. The EDID blob is the natural first split candidate (own file / generated header).

#### Phase 3b — EDID load + HPD enable ✅ PASSES ON HARDWARE (2026-05-01)

**Scope:** program a valid EDID into the chip's EDID RAM and raise HPD so the HDMI source sees a sink. Goes beyond "pure EDID" because the source won't re-read EDID unless HPD toggles — the two steps are tightly coupled.

**EDID blob (256 bytes = base + CEA-861-D extension):**

Two-block EDID. Base block is VESA EDID 1.3, detailed/preferred timing = **720x480 @ 60p** (SMPTE 293M / CEA VIC 2), pixel clock 27.000 MHz — matches what Wii → ElectronWarp emits. Established-timings byte flags 640x480@60 as a secondary. Monitor-range descriptor declares 50-75 Hz V, 30-75 kHz H, 150 MHz max pclk. Monitor name = `marvin-hdmi`.

CEA-861-D extension is required for HDMI sources to recognise us as an HDMI sink (vs DVI) and to know that VIC 2/3 are acceptable modes. Contents:
- Video Data Block with **VIC 2** (720x480p@60 4:3) marked native, **VIC 3** (720x480p@60 16:9), **VIC 1** (640x480@60) fallback.
- HDMI VSDB with OUI `0x000C03` (HDMI Licensing LLC, LE-encoded) and source physical address `1.0.0.0`.
- No audio descriptors (we don't process HDMI audio).
- No YCbCr advertised (RGB-only).

Sink chromaticity is sRGB / BT.709 primaries (red=(0.64,0.33), green=(0.30,0.60), blue=(0.15,0.06), white=D65). Feature byte sets RGB display type + pref-timing-is-native + sRGB default.

Checksum computed at runtime: byte 127 is patched so sum[0..127] ≡ 0 mod 256. Avoids arithmetic-error risk in the hand-layout.

**Sequence (mirrors kernel driver `tc358743_s_edid`):**

1. Deassert HPD (`HPD_CTL.HPD_OUT0 = 0`) — tells source to disconnect.
2. Compute checksum, patch byte 127.
3. Write `EDID_LEN1 = 1`, `EDID_LEN2 = 0` (one block).
4. Write all 128 bytes to `EDID_RAM` (0x8C00) in a single I2C transaction.
5. Delay ~150 ms so source registers the HPD drop.
6. Assert HPD (`HPD_CTL.HPD_OUT0 = 1`) — source re-reads EDID, starts negotiation.
7. Delay ~200 ms, then re-read and print `SYS_STATUS` so we can see whether source reacted.

**Buffer sizing:** `TC358743_TX_BUF_SIZE` bumped from 8 to 132 so the EDID write fits in one transaction (2 bytes register address + 128 bytes data). Matches kernel driver's `I2C_MAX_XFER_SIZE = EDID_BLOCK_SIZE + 2`.

**Pass criteria:** build succeeds; after `init complete`, DBGU shows `EDID loaded, HPD asserted` followed by a second `SYS_STATUS=0xXX`. With a live Wii → ElectronWarp chain, the post-HPD `SYS_STATUS` should have at minimum `DDC5V` set (splitter has asserted 5V after handshake) and ideally `TMDS`/`PHY_PLL` set as well, meaning we have a locked input stream. `SYNC` lock is separate and typically takes longer — we'll still see it mostly in Phase 4.

#### Phase 4a — Runtime status watcher (observation only) ✅ PASSES ON HARDWARE (2026-05-01)

**Confirmed source format:** **720x480p @ 60 Hz, RGB limited-range** (SMPTE 16-235). This is Wii → ElectronWarp native output. Full lock sequence observed: `0x09 → 0x1F → 0x9F`. Noting for Phase 4+ that the pipeline will need range expansion (or accept slightly dim/gray output) since the ISC/display side defaults to full-range.

Prerequisite for Phase 4 proper (streaming). Goal: see what the chain is doing in real time without turning on CSI output yet.

- `TC358743_Tasks()` polls `SYS_STATUS` every ~100 ms.
- On each transition of `SYS_STATUS`, print the before→after bitmask with human-readable bit names.
- When `S_SYNC` becomes set, read the detected-timing registers and print: active width × height, scan type (p/i), fps, color space, range (full/limited). Registers: `DE_WIDTH_H_LO/HI`, `DE_WIDTH_V_LO/HI`, `FV_CNT_LO/HI`, `VI_STATUS1`, `VI_STATUS3`. Color-space lookup table matches the kernel driver's `input_color_space[]`.
- Polling only; no interrupts wired (the TC358743 INT pin isn't exposed on our 22→15 adapter).

Intended use: turn on Wii, watch DBGU transitions in real time — confirms what resolution/format ElectronWarp is emitting, and gives us ground-truth for Phase 4 (stream enable) when we trust the lock semantics.

#### Phase 4 — Format detection & start CSI streaming ✅ PASSES ON HARDWARE (2026-05-01)

CSI_ERR = 0 and HLT = 0 across every sample we've seen; TC358743 is fully locked on HDMI side (SYS_STATUS=0x9F) and our `enable_stream()` writes all succeed. TXACT (bit 9 of CSI_STATUS) has been observed at 1 but is not reliably set — a run at 150 ms after enable read TXACT=0, after an earlier run had it set at 60 ms. Best interpretation: **TXACT is momentary** (indicates actively transmitting a packet right now, not "stream is on") and whether a given sample catches it depends on timing vs. packet boundaries. Bits 2, 6, 12 of CSI_STATUS are consistently set across runs — meaning is unknown without the TC358743 reference manual; kernel driver ignores them.

The only way to definitively verify CSI-TX is transmitting valid data is Phase 5 (ISC counting frame-complete interrupts). Proceeding.

**Scope:** enable the TC358743's CSI-TX stream and tie it to `S_SYNC` state so pixel data starts/stops with source lock. ISC-side capture is still Phase 5.

- `tc358743_enable_stream(true)` (mirror of kernel's `enable_stream()`):
  - `TXOPTIONCNTRL ← 0` then `← MASK_CONTCLKMODE` — the non-continuous→continuous toggle is how the CSI-TX triggers the LP11→HS transition that the receiver expects.
  - `VI_MUTE ← MASK_AUTO_MUTE` — clear manual mute, leave auto-mute on.
  - `CONFCTL` set `VBUFEN | ABUFEN` — open video (and audio; harmless, we ignore the audio path) buffer enables.
- `tc358743_enable_stream(false)` — manual mute + clear VBUFEN/ABUFEN.
- Watcher: on `S_SYNC` 0→1, enable stream; on `S_SYNC` 1→0, disable. Also enables on first-poll if SYNC is already set (warm-boot path).
- `CSI_STATUS` (reg 0x0410) dumped after enable so we can see `WSYNC`/`TXACT`/`RXACT`/`HLT` bits. `TXACT=1` after enable is the proof-of-life signal.

**No ISC / CSI2DC programming yet.** The SAM9X75 CSI receiver is present (from MCC), but we're not arming DMA or counting frame interrupts — that's Phase 5.

**Pass criteria:** boot shows `CSI stream enabled` after the SYNC transition, followed by `CSI_STATUS=0xXXXX [TXACT]` (or similar bit combo). Power cycling the source should show stream disable/enable pairs. No regressions to the Phase 4a output.

#### Phase 5 — Hook into marvin's ISC/CSI capture ✅ PASSES ON HARDWARE, 60 FPS (2026-05-01)

**Scope:** Wire up the SAM9X75 RX pipeline (CSI D-PHY → CSI2DC → ISC → DMA → SDRAM) and prove frames are landing by counting ISC DDONE interrupts. No display yet (that's Phase 6).

**What MCC already provides (we don't write):**
- `plib_csi`, `plib_csi2dc`, `plib_isc` — raw register-level code.
- `drv_csi`, `drv_csi2dc`, `drv_isc` — Init/Configure/Start wrappers with object structs.
- Config constants in `configuration.h`: CSI_NUM_LANES=2, CSI_DATA_FORMAT_TYPE=RGB888, ISC_INPUT_FORMAT_TYPE=RGB, ISC_OUTPUT_FORMAT_TYPE=ARGB32, layout packed32, etc.

**What we write (the thin wiring that libcamera used to do):**

- New module `default/src/isc_capture.{c,h}`, separate from `tc358743.c`. Public API:
  - `void ISC_Capture_Initialize(void)` — one-time driver init (calls `DRV_ISC_Initialize()`, `DRV_CSI2DC_Initalize()`, `DRV_CSI_Initalize()` in order). Not start yet.
  - `bool ISC_Capture_Start(uint32_t w, uint32_t h)` — fill CSI/ISC obj fields with width/height, point ISC DMA at framebuffer, register frame-done callback, configure + start.
  - `void ISC_Capture_Stop(void)` — stop capture (for re-sync when source drops).
  - `uint32_t ISC_Capture_FrameCount(void)` — read count.
- Framebuffer in `.region_cache` section with 32-byte alignment, 720×480×4 (ARGB32) × 2 buffers ≈ 2.7 MB. Conservative max allocation for now.
- Frame-done callback wired to `iscObj->dma.callback`, increments a `volatile uint32_t`.

**Wiring from tc358743.c:**

- In `TC358743_Initialize`, after `DRV_I2C_Open`, also call `ISC_Capture_Initialize()`.
- In the watcher, on `S_SYNC` 0→1 (after `tc358743_enable_stream(true)` succeeds): read detected width/height, call `ISC_Capture_Start(w, h)`.
- On `S_SYNC` 1→0: call `ISC_Capture_Stop()`.
- In `TC358743_Tasks`, once per second when capture is active, read `ISC_Capture_FrameCount()` and print the delta — that's our "are we actually receiving frames" evidence.

**Init/Configure order** (from emirror `camera.c`):

- Init: `DRV_ISC_Initialize()` → `DRV_CSI2DC_Initalize()` → `DRV_CSI_Initalize()`.
- Configure (called on `S_SYNC` acquire): `DRV_CSI_Configure()` → `DRV_CSI2DC_Configure()` → `DRV_ISC_Configure()` → `DRV_ISC_Configure_DMA()`.
- Start: `DRV_ISC_Start_Capture()` (may block up to ~1 s polling for VD — acceptable for bring-up, revisit if problematic) → `SYS_INT_SourceEnable(ID_ISC)`.

**Known blocker (flagged by user):** `plib_csi.c` has known bugs / changes needed before CSI RX will actually work. We write the app-level wiring first, then Greg guides through the plib_csi fixes before first flash.

**Pass criteria:**
- Build succeeds with the new module.
- After SYNC acquired, watcher prints a per-second frame count somewhere in the range of 55-65 (target 60 fps).
- No `DRV_*` configure step returns a failure code.
- Re-toggling source on/off cleanly shows capture stop/restart.

**Explicitly out of scope:**
- Displaying the captured frames (Phase 6).
- Cache invalidation for CPU-side buffer reads (only needed once we actually read pixel data; Phase 6).
- Double-buffered swap semantics / backpressure (Phase 6).
- Vision / actuation logic (later phases).

#### Phase 5b — Frame content verification ✅ PASSES ON HARDWARE (2026-05-01)

**Verdict: RGB888 capture path is deterministic and invertible.** Byte order in memory is GRB per pixel (not BGR or RGB). Analog chain is linear with gain ≈ 0.753 (0xFF → 0xC0). See the 2026-05-01 session-log entry "Phase 5b PASS" for full measurements.

**Working config:** BYPASS + PACKED8 + RMS=1 + BPP=3. Other paths explored: RMS=0 + any RLP collapses to 1 sample per pixel for RGB888 DT; ARGB32 RLP requires upstream pipeline stages we bypass.

**Remaining structural quirks (content-independent, documented for Phase 6 / upstream work):**
- `VPROW=241` (half frame delivered from TC358743/bridge; likely interlaced).
- Only ~60 source rows captured per frame (RMS=1 byte-budget cliff at ~129,600 bytes).
- ~50% of each row has real pixel data; the rest is zeros (likely TC358743 active-area narrower than raster).

These are NOT blockers for Phase 6 — we can render whatever is captured. They're additional work items if we want the full 480-row 720-pixel-wide frame.

#### Phase 5b — Original scope (now superseded — see verdict above)

**Goal:** validate captured bytes in SDRAM match the known source output *before* investing effort in display rendering. Previous bring-up attempts fell apart at display because something in the pipeline was silently garbling pixel data; by probing bytes directly we can find the misinterpretation (channel order, stride, endianness, partial frame) at the earliest possible layer.

**Mechanism:** `ISC_Capture_ProbeFrame()` in `isc_capture.c`, called once per second from `app_report_fps` alongside the fps print.

Per call, probe:
- **9 sample points**: corners + mid-edges + center of the last completed buffer.
- **Cache invalidation** (`SYS_CACHE_InvalidateDCache_by_Addr`) over the sampled frame so CPU reads see what DMA wrote.
- **Range scan across the center row** — surfaces per-pixel variation a 9-point sample would miss.
- **Byte output in memory order** (b0 b1 b2 b3) so the user can mentally pattern-match against known source without pre-committing to a channel layout.
- **Layout legend line** reminding how bytes map under ARGB / BGRA / RGBA / ABGR.

**Test plan — iterate through these with the source:**

1. **Solid mid-gray** (e.g. 0x808080) — all 9 samples should be (near) identical; each byte either ~0x80 or the alpha value.
2. **Solid pure red** (R=0xFF, G=0, B=0 — or 0xEB/0x10 in limited-range). Distinctive byte pattern: exactly one of b0..b3 is high, the rest are low (or near the range-expansion values). Tells us which byte is R.
3. **Solid green / blue** — cycle through to confirm all three channels land correctly.
4. **Test pattern with known structure** — color bars (e.g. SMPTE bars). Verify that bytes at specific x-coordinates match the expected bar color.

**Known pitfalls to look for:**

- **Limited-range vs. full-range RGB.** Wii is 16-235 SMPTE limited. Expect 0x10-0xEB, not 0x00-0xFF. If min/max are 0x00/0xFF, something's doing range expansion we didn't expect; if the max is below 0xEB, range-clipping somewhere.
- **Channel swap** — e.g. red source but the high byte is in b3 instead of b1 means we're reading BGRA or ABGR.
- **Stride mismatch** — if row N and row N+1 don't look continuous, stride = (width × 4) arithmetic may be off by a byte, or ISC is writing with padding we don't know about.
- **Garbled / noise** — if every byte varies wildly, CSI2DC or ISC is packing data incorrectly (possibly the RMS bit decision was wrong, or format tag mismatch between bridge and CSI2DC).
- **Partial capture** — if the first N rows are valid and the rest are zeros, ISC/DMA isn't driving the full frame.

**Exit criteria:** we can look at the probe output and, for a solid-color source, identify the correct byte-to-channel mapping and confirm pixel values match the source within limited-range expectations. Once that's locked, **Phase 6 (display on LCD)** knows exactly how to interpret the buffer.

#### Phase 6 — First rendered frame

Goal: pixels in SDRAM (Phase 5 output) shown on the Legato LCD surface already brought up in commit `3bb441a`.

**Scope / known items to work through:**

- **Cache coherence.** Framebuffer is `.region_cache_aligned` (cached DDR). DMA writes bypass CPU cache, so CPU-side reads will see stale data unless we invalidate the cache range before reading each frame. Use `SYS_CACHE_InvalidateDCache_by_Addr(addr, size)` keyed off the frame-done callback. Alternative: move framebuffer to `.region_nocache` — simpler but trades off uncached-read latency.
- **Double-buffered swap.** ISC DMA writes alternately to buffer 0 and buffer 1 (`dmaDescSize=2`, `frameIndex` wraps in `ISC_Handler`). Renderer should read the *previously completed* buffer (`(frameIndex + 1) & 1` from the callback's perspective) so it doesn't race the in-flight DMA write.
- **Limited-range → full-range RGB.** Wii outputs 16-235 (SMPTE limited). Legato renders 0-255 full range. Without expansion, blacks look gray and whites dim. Simple linear expand: `out = ((in - 16) * 255) / 219`, clamped. Decide whether to do this in a fast SIMD loop on ARM9 or use the 2D GPU (commit `5cd5a90` brought that in) for a blit-time conversion.
- **Legato integration.** Probably an `Image Widget` pointed at our framebuffer, set to paint on each `le_RedrawAll()`. Or a custom widget if we need per-pixel control. Need to check Legato's pixel-format support — ARGB32 is standard, but its buffer descriptor conventions are worth looking up.
- **Redraw cadence vs. capture cadence.** ISC is pushing frames at 60 Hz. LCD refresh is likely also 60 Hz (Legato default, depends on `drv_gfx_xlcdc` config). Want to request a redraw *on* each new-frame callback rather than polling Legato's own tick. Avoid double-buffer tears.
- **Display resolution vs. source resolution.** Wii is 720×480. LCD module `AC69T88A` (from commit `3bb441a`) is 1024×600 per the emirror reference. Options: center + letterbox (simplest), stretch-to-fit (use 2D GPU scaler), or 1:1 pixel map with blanks. Start with letterbox for sanity, revisit if we want fullscreen.
- **Frame counter / metrics on screen.** Optional: overlay `ISC: N fps` on the display for at-a-glance debugging without needing DBGU.
- **Source mode robustness.** If the Wii resets and outputs a different mode (e.g. 1440×240p from an earlier observation), the capture pipeline re-configures automatically, but the renderer needs to handle a resolution change mid-run. Either re-create the image widget on `TC358743_GetDetectedFormat` change, or make the renderer resolution-aware.

None of this blocks anything — it's just things to think about before wiring up the render path.

#### Phase 7 — 480p source revisit ✅ 7a PASSED 2026-05-02; 7c (Wii) still outstanding

Phase 7a **passed first try** with Pi forced to 720×480p60 via config.txt. Zero firmware changes needed; today's pipeline is fully resolution-agnostic. Historical "480p phase drift" was a symptom of the broken BPS=EIGHT/PACKED8/RMS=1 pipeline, not intrinsic to the resolution. See 2026-05-02 Phase 7a session log entry.

Phase 7b (297 Mbps) now deferred indefinitely — not needed.

Phase 7c (Wii through ElectronWarp) remains: pure test of the component→HDMI chain. If drift returns there, the smoking gun is definitively ElectronWarp, not 480p or our pipeline.

---

**Original plan kept below for history:**

**What changed since 480p was last tested (commit `3bb441a` era):**

- `BPS=FORTY` (commit `2908679`) — was EIGHT before; ISC now correctly interprets the 40-bit CSI2DC word. This is the fix most likely to have affected 480p phase behavior because BPS=EIGHT slicing at bit boundaries plausibly produced some of what looked like drift.
- `PFE_CFG1/2` + `COLEN/ROWEN` — journal line 611 notes the PFE crop bug "has almost certainly been happening on 480p too". Fixed.
- `DCFG = PACKED32 + BEATS32` — resolution-agnostic, applies.
- `RMS=0` + `COLMAX = W - 1` (commit `eb8b109`) — BGRX32 layout, resolution-agnostic.
- **HSFREQRANGE is bitrate-specific.** 480p's prior 297 Mbps config used `0x14`; that may or may not be the correct DWC Gen3 value. Need to look up the Gen3 band for 200–300 Mbps.

**Test plan (cheap → expensive):**

1. **Phase 7a — Pi @ 480p, 972 Mbps retained.** Keep TC358743 D-PHY at 972 (it'll pad blanking, per kernel driver + journal line 460). Add 480p back to EDID (re-introduce VIC 1/2/3 CEA VDB; add a 640x480 or 720x480 DTD in the base block). Pi config.txt → `hdmi_mode=1` (640×480) or `hdmi_mode=2` (720×480). Reduce `ISC_Capture_Configure(w,h)` to match. If clean: 480p is solved with zero extra TC358743/D-PHY work.
2. **Phase 7b — Pi @ 480p, 297 Mbps CSI.** Only if 7a shows drift. Revert TC358743 PLL to 297 Mbps output, update D-PHY timing constants (kernel driver has the table), find the Gen3 HSFREQRANGE value for that band. Compare drift behavior.
3. **Phase 7c — Wii @ 480p through ElectronWarp.** Only after Pi 480p is clean. Tests whether drift is intrinsic to the Wii/ElectronWarp chain (pixel-doubling, component→HDMI timing jitter, etc.) vs. the 480p resolution itself.

**Open sub-questions for Phase 7:**

- Is the 5/11 16-row drift eliminated by BPS=FORTY alone? (Hypothesis: partially or fully — worth testing before touching anything else.)
- CSI2DC GCFGR.ULC=1 and the Data Pipe + XDMAC alternate path — journal line 712 notes these were never tested at 480p. Backup options if Phase 7a/b still show drift.
- Correct HSFREQRANGE (Gen3) for 297 Mbps. Our old `0x14` value may have been a Gen2 guess that worked by coincidence.

**Carry-forward probe-interpretation note:** the `phase` scanner in `ISC_Capture_ProbeFrame` uses `b % ISC_CAP_BPP`. Historical 480p drift logs were captured with `BPP=3` (RMS=1), so their `phase: row N → {0,1,2}` readings are NOT directly comparable to new logs which use `BPP=4` and report `{0,1,2,3}`. Keep that in mind when comparing old-vs-new drift behavior.

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
| 2026-05-01 | `csiBitRate = 0x14` set from `isc_capture`, not MCC | `drv_csi.c` hardcodes `csiBitRate = 0x16` (wrong — testing showed 0x14 works). Rather than modifying MCC-generated code, we treat the MCC value as a default and override in `isc_capture` alongside the already-necessary `csiFrameWidth/Height/Fps` overrides. Only one MCC-file modification stays (`plib_csi.c` `CSI_Analog_Init` bug fix). |
| 2026-05-01 | ISC Bayer blocks auto-bypass for RGB input (previously an open question) | `drv_isc.c:363-365` disables CFA/WB/Gamma/CSC/Sub422/Sub420 whenever `inputFormat == DRV_IMAGE_SENSOR_RGB` — no MCC config changes needed despite `ISC_ENABLE_DPC/GDC/WHITE_BALANCE/GAMMA` remaining `true` in `configuration.h`. Those flags are Bayer-path only and are ignored in the RGB branch. |
| 2026-05-02 | Capture pipeline outputs BGRX32 natively (CSI2DC RMS=0 + ISC RLP BYPASS + DMA PACKED32) | Datasheet §49.6.54 + §50.6.19 + §50.6.20 show this is the only in-pipeline path to 32 bpp for MIPI RGB888 bypass. ARGB32 RLP mode is unusable on this path (requires CSC module output format). Alpha=0x00 is acceptable trade for zero-CPU capture; 0xFF can be added post-hoc if needed. Full rationale in `capture_pipeline.md`. |
| 2026-05-02 | Limited→full-range RGB expansion will be done in HEO CSC block on the display side only; capture buffer stays unexpanded for vision consumers | Both Wii and Pi emit RGB limited-range (16–235). Expanding on capture would cost CPU and break vision consumers by inflating values without adding information (linear rescale carries no new signal). LCD display needs full-range 0–255 or it looks muted, so the HEO CSC matrix (programmable `M × in + offset`) will be configured as an identity RGB→RGB with limited→full gain/offset. Zero CPU, display-only, capture untouched. See `display_path.md` §5. |

---

## Open questions

_(Questions we haven't answered yet. Move to decision log with rationale once resolved.)_

- ~~CSI-TX minimum bitrate at 480p60~~ — **resolved 2026-05-01**: we ended up dropping to 297 Mbps/lane (below the kernel's tabulated 594 Mbps) with no problems. 297 pairs with host HSFREQRANGE band 0x14 and the kernel's 594 Mbps D-PHY timings happen to still be safe at this rate.
- ~~`TXACT` absent in CSI_STATUS after Phase 4 enable~~ — **deferred 2026-05-01**: TXACT has been observed at both 0 and 1 at various times after enable. Likely momentary (active-packet indicator, not stream-on flag). CSI_ERR=0 and HLT=0 consistently. Phase 5 frames-in-memory confirmed the chain works regardless.
- ~~Source mode variability~~ — **explained 2026-05-01**: 1440x240p observation was Wii post-reset default. Put Wii in 480p mode; we stay on 720x480p@60.

**Carried into future sessions:**

- **MCC-file modifications maintenance risk.** Two MCC-generated files have local modifications that will be clobbered if MCC re-emits them:
  1. `plib_csi.c` — `CSI_Analog_Init` refactor (Lane 1 bit-rate write + Lane 2 addr typo + 3/4-lane Lane-1 skip).
  2. `plib_csi2dc.c` — `CSI2DC_Configure_VideoPipe` does **not** OR in `CSI2DC_VPCFGR_RMS_1`. MCC default sets RMS=1, we need RMS=0 for the BGRX32 pipeline. If MCC regenerates, the `| CSI2DC_VPCFGR_RMS_1` will come back and capture output will silently shift to dense 3 B/pixel BGR layout — alpha lane disappears, frame size changes, display will show garbage.

  Recovery plan: if the build breaks post-regen, re-apply both (small, self-contained diffs documented in decision log). Long-term options are (a) file MCC bugs, (b) shim into our own files, (c) live with periodic re-application.

- **drv_image_sensor.h shim.** We keep a minimal enum-only shim at `default/src/config/default/vision/drivers/image_sensor/drv_image_sensor.h` so `drv_isc.c` and `configuration.h` still compile after libcamera removal. MCC shouldn't touch this path since the image_sensor component is disabled, but worth a check if something weird happens.

- **TC358743 code comment discipline.** The current `tc358743.c` is ~1000 lines with decent inline comments per CLAUDE.md rules. When we revisit for Phase 6+, check that no comments have drifted into dev-diary territory.

- **`log_csi_status` was removed** (Phase 5 restructure — tc358743 no longer auto-enables stream). If we ever want to re-query TC358743 CSI_STATUS/CSI_ERR bits, re-add the helper. The register addresses and masks are still defined in the file.

- **ISC_Capture framebuffer sized for max 1920×1080 × 2 (~16 MB)** — wastes DDR at our current 720×480 use, but trivial at 256 MB total DDR and gives headroom for different sources. Reconsider if DDR becomes tight later.

- **Retry semantics on source loss.** `app_coordinate_capture` uses a `capture_attempted` flag to prevent retry spam. On unlock it resets, so the next lock triggers a fresh Configure+Start. Untested for fast lock/unlock cycles — if the Wii toggles power, watch for state-machine glitches.

- **Frame counter wraps at UINT32_MAX.** `g_frame_count` is 32-bit; at 60 fps it wraps after ~2.3 years continuous run. Not an immediate issue.

---

## Session log

### 2026-05-13 — HEO hardware scaler upscaling 720×480 → 1200×800 ✅

Aspect-preserving fit on the 1280×800 panel: 720×480 source scales to 1200×800 via HEO with 40 px black pillarbox each side. 1280×720 sources still bypass the scaler (1:1 with 40 px letterbox top/bottom). Per datasheet Table 44.59 (Progressive ARGB), 4-tap polyphase filter, all four scaler enables on, factors computed as `round(2^20 × (memsize-1) / (winsize-1))`.

**Tap encoding confirmed:** 13-bit signed Q2.10. 1.0 = `0x400`. Datasheet doesn't state explicitly; verified empirically with nearest-neighbor pass-through. Documented as `LCD_TAP_ONE` in app.c.

**Current filter:** bilinear (16-phase). TAP1 = 1−φ, TAP2 = φ, TAP0=TAP3=0. Coefficients sum to exactly 1.0 per phase. Soft but smooth — visibly better than nearest-neighbor for camera-style content.

**Possible future polish (not blocking anything):**
- **Bicubic / Mitchell-Netravali** (B=1/3, C=1/3) — uses all 4 taps, requires signed coefficients (some negative). Sharper edges than bilinear without the ringing of pure cubic.
- **Lanczos-2** — windowed sinc, all 4 taps signed, gold standard for video upscale. Diminishing returns at modest 1.667× ratios; main benefit is at larger ratios or for downscale.
- **Different filter for downscale vs upscale** if we ever do downscale (e.g., 1080p → 1280×800).

For now bilinear is the resting place. Revisit only if image quality becomes a complaint.



### 2026-05-02 — Panel swap: 7" 800×480 → 10.1" 1280×800

Greg moved to a larger LVDS panel and updated the MCC display component to match. MCC regenerated `XLCDC_HOR_RES/VER_RES`, `LCDCFG1..4` (sync widths/porches/active region), and the per-layer setup defaults to 1280×800. `LCD_PANEL_W/H` macros in `app.c` updated to match (those are not MCC-generated).

Side effect: Pi 720p (1280×720) now fits the panel natively — it's letterboxed (40 px top/bottom) instead of being rejected. 480p sources land in a 720×480 inset with 280 px pillarbox each side, 160 px letterbox top + bottom.

Auto-allocated layer buffers grew from 800×480 × 2 = 768 KB to 1280×800 × 2 = 2 MB per layer (RGB565 mode from the previous session). Total of all four layers is ~8 MB of `.region_nocache` DDR permanently allocated, of which we use exactly 0 bytes (OVR1 reads our capture buffer instead). Trivially recoverable later if memory tightens.

No code changes other than `LCD_PANEL_W/H`. `lcd_bind_capture()` math is parametric on those macros, so the pillarbox/letterbox positions update automatically.

### 2026-05-02 — Phase 6 MVP: captured video on the LCD ✅

First pixels from the capture pipeline displaying on the 7" AC69T88A LVDS panel. 720×480 Wii feed pillarboxed (40 px black each side) on the 800×480 panel. No scaling, no CPU post-processing.

**MCC additions (staged from Greg's Harmony Configurator session):**
- gfx_display_comp_ac69t88a (panel timings)
- le_gfx_driver_xlcdc (high-level layer driver + 4 auto-allocated .region_nocache framebuffers)
- le_gfx_gfx2d (2D engine driver)
- plib_gfx2d (2D engine peripheral lib)
- gfx_bridge_lvdsc_plib (LVDS serializer)
- Wired into `SYS_Initialize()` via auto-generated initialization.c

**Key discovery — BASE layer has no window registers.** `XLCDC_SetupBaseLayer` configures BASECFG0..6 but no equivalents to OVRxCFG2/3 (XPOS/YPOS/XSIZE/YSIZE). BASE layer is always the full configured panel size. Pointing BASE at a 720-wide buffer on an 800-wide panel produced a characteristic line-to-line skew (LCDC reads 800×4 = 3200 B/row, source has only 720×4 = 2880 B/row, every display row drifts 80 px left). Fix: use OVR1 instead, which has real window position/size registers.

**Pixel format:** `XLCDC_RGB_COLOR_MODE_ARGB_8888` reads memory in byte order `B, G, R, A` (low to high). That's exactly our BGRX32 layout byte-for-byte. Our X=0x00 would imply transparent, but we set OVR1 layer global alpha to 255 via `SetLayerOpts(layer, 255, true, false)` which overrides per-pixel A.

**Code changes (small):**
- `isc_capture.c/h` — new `ISC_Capture_GetBufferAddress()` accessor exposes buffer 0 base.
- `app.c:APP_Initialize` — calls `XLCDC_EnableBacklight()` (MCC auto-init leaves it off).
- `app.c:lcd_bind_capture()` — reconfigures OVR1 to point at the capture buffer, pillarboxed. Called from `app_coordinate_capture` once per TC358743 lock, right after `ISC_Capture_Configure` succeeds.

**Known trade-offs (documented in `display_path.md`):**
- LCDC stuck on buffer 0; ISC alternates writes between buffer 0 and 1. Effective display refresh ~30 Hz, possible scanline tear. Upgrade path: pointer swap on ISC frame-done ISR.
- 1280×720 sources (Pi 720p) exceed the 800×480 panel; bind bails. Downscale via HEO scaler is future work — plib doesn't expose HEO scaling so would need direct `HEOCFG*` register writes.
- Limited-range Wii red (~0xC8) displays slightly dim. Could range-expand via CPU or GFX2D if needed.

Phase 6 MVP complete. Next: decide whether to pursue HEO scaling for Pi 720p support or proceed to Phase 8 (vision stage) on the existing 480p path.

### 2026-05-02 — Phase 7c PASSES: Wii→ElectronWarp→TC358743 fully characterized ✅

Same pipeline, Wii (component → ElectronWarp → HDMI → TC358743) as source instead of Pi. All Phase 7 objectives now complete.

Capture quality:
- TC358743 locks: `detected 720x480p @ 60 Hz, RGB limited-range; raster 858x525`
- IDS: `DT=0x24 WC=2160 rows=480` identical to Pi-at-480p.
- Full frame written (rows 0..479), no cliff, ~70 fps sustained.
- **No phase drift** — scanner reports only 2 transitions (`row 0 → 2`, `row 479 → -1`), both expected edge cases. The historical 5/11 16-row drift is definitively pipeline-caused (BPS=EIGHT + PACKED8 + RMS=1), not an ElectronWarp/Wii property.

Content geometry (from bbox scanner):
- Horizontal: `x=[36..675]` → **640 active px**, asymmetric pillarbox (36 L, 44 R). That's the Wii's internal 640×480 4:3 render, slightly left-shifted by ElectronWarp relative to NTSC HSYNC.
- Vertical: `y=[0..478]` → 479 active rows, just 1 px black at bottom (row 479). Effectively full-height.
- **Wii content crop (future vision consumer):**
  ```c
  CONTENT_X = 36,  CONTENT_Y = 0
  CONTENT_W = 640, CONTENT_H = 479
  ```

Saturation:
- Peak red ≈ `0xC8` (≈78% of 0xFF, ≈85% of 0xEB limited-range peak). Expected from the analog component → RGB chain; Pi digital through TC358743 peaks at `0xFE`. If full range matters, the vision or display stage can linearly expand.
- Per-pixel noise ±3 LSB (`C5..CC`) — normal analog jitter, not a pipeline issue.

Phase 7 now fully closed: 7a (Pi 480p) ✅, 7b (297 Mbps) indefinitely deferred — not needed, 7c (Wii/ElectronWarp) ✅.

### 2026-05-02 — Phase 7a PASSES first try: 480p is clean on today's pipeline ✅

Pi forced to 720×480p60 via `config.txt` (`hdmi_group=1 hdmi_mode=2 hdmi_force_hotplug=1 hdmi_drive=2`). Flash, boot, capture. **Zero firmware changes** — `ISC_Capture_Configure` already reads width/height from `TC358743_GetDetectedFormat`.

Results with solid red source:
- TC358743 locks: `detected 720x480p @ 60 Hz, RGB limited-range; raster 858x525`
- IDS authoritative: `DT=0x24 VC=0 WC=2160 rows=480` (2160 = 720 × 3 MIPI bytes)
- Frame size: `1,382,400` (= 720 × 480 × 4) ✓
- 9-point probe: every sample `00 00 FE 00` (B=0, G=0, R=0xFE, X=0) — correct BGRX32 red
- `vertical extent: rows 0..479 written, first sentinel at row 480 (of 480)` — **full frame, no cliff**
- **Phase scanner: ONE entry, `row 0 → 2`. The 5/11 16-row drift is gone.**
- 70 fps sustained

**Conclusion: the historical 480p phase drift was NOT intrinsic to 480p.** It was caused by the broken capture-side pipeline (BPS=EIGHT + PACKED8 + RMS=1 + no PFE crop + BEATS8). When the ISC misinterprets the 40-bit CSI2DC word as 8-bit samples, it slices pixels at bit boundaries and produces apparent "drift" that's actually stable-but-misaligned reading. BPS=FORTY eliminates the slicing; RMS=0 + PACKED32 + PFE crop + BEATS32 take care of the rest. All resolution-agnostic.

**Phase 7b (drop to 297 Mbps) is now unnecessary for 480p correctness** — the 972 Mbps pipeline works fine for 480p (TC358743 pads blanking). Still worth testing if we hit CSI-2 latency/buffering issues at some source rate in the future.

**Phase 7c (Wii through ElectronWarp) is now an isolated test** of the component→HDMI chain specifically. If drift shows up on that path it's ElectronWarp, not 480p.

Carry-forward:
- The capture_pipeline.md reference doc applies unchanged to 480p — just different PFE crop values. Consider adding a sentence noting this explicitly.
- Phase 7 plan in the Phased plan section can be reduced to "7c only" — 7a done, 7b deferred until there's a reason.

**Follow-up validation (same session):**
- EDID extended with VIC 3 + VIC 2 so real HDMI sources (Wii/ElectronWarp) can negotiate 480p naturally. Pi still locks to 720p60 natively; `hdmi_force_hotplug=1` + `hdmi_mode=2` forces the 480p path for testing. Extension-block byte layout kept at 128 bytes (VDB grew by 2, padding shrank by 2).
- Probe extended with 4 absolute-corner samples (`FIRST`, `TopR`, `BotL`, `LAST`) = `(0,0)`, `(W-1,0)`, `(0,H-1)`, `(W-1,H-1)`. All four read clean BGRX-red at 720×480: PFE-crop and DMA first/last-word alignment are exact. Combined with the interior 9-point grid, 13 sample locations confirm every corner/center of the active region.
- Framebuffer sizing strategy documented in `capture_pipeline.md`: static 1920×1080×4×2 = 15.8 MB pool, per-capture only the DMA descriptor's `frame_size` changes. Trivial idle waste at low resolutions; zero reallocation on source-resolution change.

### 2026-05-02 — BGRX32 in-pipeline via CSI2DC RMS=0 + ISC PACKED32 ✅

**Milestone:** capture now lands directly as `B G R 00` per pixel (BGRX32) in DDR — no CPU post-pass, no GFX2D blit. Confirmed across R/G/B/W/K solid-color sweep at 720p60. 70 fps sustained. 3,686,400 B/frame.

**Root-caused from datasheet sections 48/49/50:**

- **49.6.54 VPCFGR.RMS** is the lever. RMS=1 (MCC hard-coded default) packs 4 BGR pixels into 12 bytes per CSI-2 "Recommended Memory Storage" (Table 49.27) — that's what our previous "dense 3 B/pixel" capture was. RMS=0 makes CSI2DC emit **one pixel per 40-bit VP word** as `demux_data = 0x00_00RRGGBB` (Table 49.25).
- **50.6.19 RLP BYPASS (mode 15)**: "32-bit input is sampled and written to the rlp output port. Select this mode for MIPI RMS mode." With RMS=0 input, RLP BYPASS passes `sub420_data[31:0] = 0x00RRGGBB` straight through.
- **50.6.20 DMA IMODE=PACKED32**: stores `rlp_data[31:0]` as 4 bytes/sample → little-endian memory order `B G R 00` = BGRX32.

**Why ARGB32 RLP mode fails on our path (confirmed, not just empirical):** Table 50.19 row for ARGB32 specifies `R=sub420_data[29:22]`, `G=sub420_data[19:12]`, `B=sub420_data[9:2]` — those bit positions are the output of the ISC CSC module, not raw MIPI. With MIPI RGB888 bypass the R/G/B live at `[23:16]/[15:8]/[7:0]`, so ARGB32 RLP picks a 6-bit-shifted garbage slice. ARGB32 RLP is structurally unusable for MIPI bypass of RGB888; it would require routing through CSC (which is intended for Bayer/YCbCr). **Alpha = 0xFF in-pipeline is therefore not achievable** on the MIPI-RGB888 bypass path. We get X=0x00 for free; if alpha needs 0xFF a cheap CPU init of the framebuffer can set byte 3 once (DMA writes don't touch it if the pixel stride matches).

**Changes (3 lines of real substance):**

- `plib_csi2dc.c:71-79` — drop `CSI2DC_VPCFGR_RMS_1` (RMS=1 → RMS=0).
- `isc_capture.c:19` — `ISC_CAP_BPP` 3 → 4.
- `isc_capture.c:206` — `PFE_CFG1.COLMAX = width - 1` (was `(W*3)/4 - 1`).

**Sweep results (Pi 720p60 source; TC358743 in limited-range, peak = 0xFE not 0xFF):**

| Source | Memory (b0 b1 b2 b3) |
|---|---|
| Red   | `00 00 FE 00` |
| Green | `00 FE 00 00` |
| Blue  | `FE 00 00 00` |
| White | `FE FE FE 00` |
| Black | `00 00 00 00` |

Byte order = B, G, R, X (little-endian of `0x00RRGGBB`). All primaries clean across full 720×1280 extent.

**Carry-forward:**
- Peak value 0xFE: TC358743 is in `RGB limited-range`. EDID / colorimetry override to get full-range 0x00–0xFF is a separate task.
- HDTO still in INTSR; frameIndex still stuck at 0 with DDONE unfired — carried over from prior state, not caused by this change. Still harmless.
- Next: wire BGRX32 framebuffer into LCDC/Legato for live display (Phase 6).

### 2026-05-02 (earlier) — 720p60 full-frame capture WORKING; memory format reverse-engineered

Huge progress day. Pi as stable 720p60 source, worked through a chain of ISC config issues, ended with full 720-row capture at 60 fps with a deterministic (if quirky) memory layout.

**Issues resolved (in order):**

1. **PFE crop window never set** (MCC bug). `plib_isc.c` defines `ISC_PFE_Crop_Area()` but nobody calls it. `PFE_CFG1/2` stayed at reset value 0, PFE expected 1×1 frame, fired HDTO on first real line. Fix: write crop directly, AND set COLEN+ROWEN bits in PFE_CFG0 (per Linux mchp-isc driver) to activate.

2. **Cliff at 180 rows × 3840 bytes = 691,200 bytes per frame.** Ruled out: LCDC/AHB contention (LCDC removed — no change), burst size (YMBSIZE SINGLE vs BEATS32 — no change), VPCFGR.PA (no change). Diagnostic register dump showed `CSI2DC VPCOL=960 = WC/4`: CSI2DC VP emits 32-bit words to ISC. IMODE=PACKED8 was treating each word as a 1-byte sample → 960 samples/row × 720 rows = 691,200 bytes = the exact cliff.

3. **Fix: IMODE=PACKED32.** ISC now writes 4 bytes per CSI2DC word × 960 × 720 = 2,764,800 bytes = full frame. `DCFG=0x00000442`. **60 fps sustained, full 720 rows written per frame.**

**Memory layout decoded** (with R/G/B/white/black solid-color tests from Pi):

Each pixel occupies **12 bytes**. Channel order is **BGR**. Each 8-bit source byte becomes a 12-bit MSB-aligned value in memory (`0xFF` source → `0xEF0` stored — bridge subtracts 16 from full-range bytes without scaling, so decoded 8-bit equivalent is `0xEF`=239 for `0xFF` input).

```
Pixel at offset N*12:
  +0  B low 8 bits      +4  G low 8 bits      +8  R low 8 bits
  +1  G high 4 bits     +5  R high 4 bits     +9  B high 4 bits
  +2  0 (pad)           +6  0                 +10 0
  +3  0                 +7  0                 +11 0

Reconstruction:
  B12 = mem[+0] | (mem[+9] << 8);   B8 = B12 >> 4;
  G12 = mem[+4] | (mem[+1] << 8);   G8 = G12 >> 4;
  R12 = mem[+8] | (mem[+5] << 8);   R8 = R12 >> 4;
```

Quirk: each channel's high 4 bits land in the chunk BEFORE its own (Blue high → Red chunk, Green high → Blue chunk, Red high → Green chunk), a CSI2DC/ISC streaming artifact. Consistent and decodable.

**Data density consequence**: 12 bytes per pixel × 1280 × 720 = 11,059,200 bytes needed for full-resolution; framebuffer holds 2,764,800 bytes. So effective capture is **1/4 horizontal resolution (320 unique pixels × 720 rows)**. On solid colors this is invisible. On real images it's 4:1 horizontal downsampling.

**Remaining / carry-forward:**
- HDTO bit still fires in INTSR — harmless (informational only; not a stop signal).
- 1/4 horizontal resolution — either accept & decode in software (320×720 plenty for guitar fretboard), OR find CSI2DC VP config that emits denser source bytes per output word.
- Byte-value range offset: 0xFF → 0xEF. Recoverable via `out = min(255, in + 16)` if needed.

**Code state at end of day:**
- `isc_capture.c`: PFE crop + COLEN/ROWEN activate; DCFG override to PACKED32 + BEATS32 + CMBSIZE_BEATS32; `csi2dcObj->videoPipeAlign = false` (PA=0); redundant `DRV_ISC_Configure_DMA` call removed.
- LCDC removed from MCC config (no contention concerns).
- ISC 60fps sustained, full 720-row frames landing in DDR.

### 2026-05-01 — Branch `720p`: cliff root-caused to PFE crop; fix applied but unverified

**Diagnostic breakthrough**: expanded ISC_Capture_ProbeFrame with a `cliff:` block that reads PFE crop window, ISC DMA state, per-probe CSI2DC/ISC counter deltas, and decoded INTSR error bits. First flash pinpointed the problem:

```
cliff: PFE_CFG1=0x00000000 PFE_CFG2=0x00000000
cliff: PFE crop col[0..0] row[0..0]  expected col[0..1279] row[0..719]
cliff: INTSR=0x08000002 [HD HDTO!]
cliff: iscObj frameCount=1
```

**MCC's ISC driver never programs the PFE crop window.** `plib_isc.c` defines `ISC_PFE_Crop_Area()` but nothing calls it, so `PFE_CFG1/2` stay at reset value 0 → PFE expects a 1×1 frame → HDTO (Horizontal Detection Timeout) fires after the first line → ISC halts, no DDONE. This has almost certainly been happening on the 480p path too, but at 480p the bridge drops so many lines that the cliff overlapped with the bridge shortfall.

**Fix attempt 1**: program `PFE_CFG1 = COLMAX(w-1)` and `PFE_CFG2 = ROWMAX(h-1)` in `ISC_Capture_Configure` after `DRV_ISC_Configure`. Result: HDTO gone (`INTSR=0x00000002 [HD]` only) — but VD also stopped firing entirely (`frameCount=0` vs `=1` pre-fix). Partial memory writes, no DDONE.

**Cross-referenced the kernel mchp-isc driver** (`microchip-isc-base.c`): it programs PFE_CFG1/2 **AND** sets `COLEN`/`ROWEN` bits in PFE_CFG0 via `regmap_update_bits`. Without those enable bits the crop values are stored but inert, and the PFE's internal line/frame boundary detector doesn't activate.

**Fix attempt 2**: `PFE_CFG0 |= COLEN | ROWEN` (bits 12+13) after the crop-value writes. `PFE_CFG0` now reads `0x40007080` (was `0x40004080`). Flash confirms the bits take, and `DRV_ISC_Start_Capture` succeeds — but the source (NTSC → 720p converter) dropped SYNC before the 1-second probe interval, so we got no post-fix probe output to verify DDONE behavior. **Fix is unverified.**

**Session state at wrap:**
- PFE crop window + COLEN/ROWEN programming committed to `isc_capture.c`.
- Probe now has a `cliff:` diagnostic block and moved sample points to `x=w/4,w/2,3w/4` + `y=h/8,h/2,7h/8` (inside pillarbox, avoid edge overscan).
- Hex-dump rows rebased to 5/20/60/100/200 (span both 480p and 720p cliff locations).

**Carry-forward for next session:**

1. **Verify the PFE/COLEN fix**. Need a stable source that doesn't drop SYNC within the 1-second probe window. If fix works, expect:
   - `frameCount` delta ≈ 60 per probe
   - `frameIndex` alternating 0/1
   - `ISC: 60 fps`
   - `vertical extent: rows 0..719 written` (full frame)

2. **Restart-after-source-blink bug**. Second `DRV_ISC_Start_Capture` fails after source drops+recovers. Diagnosed but not fixed. Driver state cleanup on `Stop` is incomplete. Probably small, in `isc_capture.c` or requires partial `DRV_ISC_Software_Reset()` before re-configure.

3. **Source stability**. NTSC→720p converter dropping SYNC periodically. Could be cable / grounding / the converter itself. If it keeps being flaky, swap to a more stable source (PC HDMI output, chromecast, etc.) to isolate firmware vs. source issues.

### 2026-05-01 — Branch `720p`: drift GONE at 972 Mbps, D-PHY locks with Gen3 HSFREQRANGE

**Key results after two flashes on new branch:**

1. **Byte-phase drift is 480p-specific.** At 720p60 on a real HDMI source, the phase scanner shows exactly one entry (`row 0 → 2`) — flat across all captured rows. The 5/11 16-row drift we fought on 480p is gone. Confirmed: root cause was either the pixel-doubling path (now absent), or the 297 Mbps/lane CSI rate, or both.

2. **SAM9X75 D-PHY RX is DWC Gen3, not Gen2.** First attempt at 972 Mbps with HSFREQRANGE=0x1A (Gen2 band 950-1000) gave `CSI_INT_ST_PHY_FATAL=0x3` (SOT sync errors both lanes). Swapped to Gen3 band value 0x0A and it locked cleanly. Confirms the DT clue `snps,dw-dphy-rx` with hardware-detected `dphy_gen` — Gen3 path in Linux's DWC driver. Update `ISC_CAP_CSI_BITRATE` accordingly in the code.

3. **Bridge cleanly delivers full 720p60 frame**: IDS reports `DT=0x24 VC=0 WC=3840 rows=720` — full-width, full-height MIPI packets at 60 Hz. No halving, no field-weaving, no pixel repetition.

4. **RMS=1 cliff is not a fixed byte budget.** Captured ~108 destination rows per frame, = 414 KB/frame. At 480p the cliff was ~120 KB/frame. So the cliff scales (roughly 3.4×) with the input data rate, but still bites somewhere short of full frame.

5. **DDONE interrupts never fire.** `ISC: 0 fps` — frame-count callback is not being invoked despite memory clearly being written. ISC detects no "end of frame" because it's stopping mid-frame at the cliff. Fixing the cliff may fix DDONE as a side-effect.

6. **Secondary bug: re-start after source re-lock fails.** When source blinks (0x9F→0x19→0x9F), second `DRV_ISC_Start_Capture` times out. Driver state cleanup after `Stop` is incomplete. Not blocking, can revisit.

**Settings summary that work on this branch:**
- PLL FBD=144 → 972 Mbps/lane
- D-PHY timings from kernel's 972 Mbps table
- HSFREQRANGE **0x0A** (Gen3 band)
- VI_REP cleared (no pixel-rep de-replication)
- EDID advertises VIC 4/19/34 only
- CSI2DC / ISC unchanged from previous baseline

**Next focus:** ISC RMS=1 cliff. With the drift gone, there's no obstacle to capturing the full 720×1280 frame other than this throughput limit. Likely candidates: ISC PFE cropping, DMA descriptor chain size, AHB-matrix priority.

### 2026-05-01 — Branch `720p`: pipeline reconfigured for 720p60 / 1080p30

Fresh branch off `9x75take2`. All code changes coordinated to target 720p60 RGB888 at 972 Mbps/lane (kernel's tabulated rate).

**Changes in this commit:**

| File | Change |
|---|---|
| tc358743.c | `PLL_FBD=88` → `144` (297 Mbps → 972 Mbps/lane). D-PHY timings updated to kernel's 972 Mbps values (LINEINITCNT=0x1B58 etc.). |
| tc358743.c | `VI_REP` IN_REP_HEN/IN_REP cleared (real HDMI sources don't carry pixel doubling). |
| tc358743.c | Base-block DTD: 720x480p@60 → 1280x720p@60 (74.25 MHz pclk, pos-H/pos-V sync). |
| tc358743.c | CEA VDB: VIC 2/3/1 → VIC 4 (native) / VIC 19 / VIC 34. No 480p fallback — force source off pixel-doubled 480p. |
| isc_capture.c | `csiBitRate = 0x14` → `0x1A` (HSFREQRANGE band for 950-1000 Mbps, DWC Gen2 table). |
| app.c | Removed `cap_w = w/2` halving; use detected width directly. |

**Bandwidth math at 972 Mbps/lane × 2 lanes = 1.944 Gbps:**
- 720p60 RGB888: 1.327 Gbps (68% link utilization) ✓
- 1080p30 RGB888: 1.493 Gbps (77%) ✓
- 1080p60 RGB888: 2.985 Gbps ❌ (not advertised in EDID)

**Per the samsonx / RPi forum thread, 720p and 1080p are the "clean" paths on TC358743.** Hypotheses we're testing simultaneously on this branch:

1. Real HDMI source (no pixel doubling) → no need for VI_REP.IN_REP → full horizontal resolution.
2. Higher CSI rate (972 vs 297 Mbps/lane) → different D-PHY alignment behavior → maybe the 16-row byte-phase drift disappears.
3. 720p/1080p timings → no field-weaving or half-frame behavior → full vertical resolution captured.

**Expected post-flash (if all three hypotheses hold):**
- TC358743 detects 1280x720p@60 (or whatever the source picks from VIC 4/19/34).
- IDS: `DT=0x24 VC=0 WC=3840 (=1280×3) rows=720` for 720p60.
- Phase scanner: zero transitions (flat phase, no drift).
- `vertical extent: rows 0..719 written` (full frame captured, RMS=1 cliff may or may not still bite).
- Probe hex dumps show clean dense 3-byte pixel triples.

**Failure modes to expect and interpret:**
- `SYS_STATUS` never reaches 0x9F → source doesn't support any advertised VIC. Widen EDID to include 480p, test.
- D-PHY locks but CSI2DC `ARSTIP` stuck → 972 Mbps too fast for SAM9X75 D-PHY. Drop to 756 Mbps with new HSFREQRANGE.
- Frames captured but fps < 60 → FIFO overrun; bridge dropping frames because 594 Mbps/lane (prior config) was too slow, should be fixed here.
- Phase drift still present → drift is fundamental to CSI2DC VP, not source-specific. Falls to software realign or Data Pipe.

### 2026-05-01 — Session wrap: 480p pipeline works end-to-end at 360x~113; pivoting to 720p/1080p test

Committing current state and starting a fresh branch to test whether 720p or 1080p sources avoid the 480p-specific quirks we've hit (pixel-doubling, 16-row phase drift, RMS=1 byte cliff). RPi forum threads suggest 720p/1080p are cleaner paths on TC358743.

**Carried-forward state (in this commit):**
- TC358743 VI_REP = 0x11 (IN_REP_HEN=1 + IN_REP=1): strips 2x horizontal pixel repetition from the Wii/ElectronWarp chain. 360 unique px/row emitted.
- ISC capture geometry set to `w/2 × h` in `app_coordinate_capture()` to match de-replicated width.
- `ISC_Capture_ProbeFrame` has phase-scanner diagnostic added (shows per-row byte alignment).
- All other pipeline settings at Phase 5b "working" baseline.

**Known issues deferred (don't try to fix on main branch):**
1. **16-row byte-phase drift.** CSI2DC Video Pipe introduces a deterministic 5/11 split in every 16-row cycle. Ruled out: ISC YMBSIZE (tested SINGLE vs BEATS8), CSI2DC VPCFGR.PA (tested 0 vs 1). Not tested: GCFGR.ULC=1, Data Pipe + XDMAC path.
2. **ISC RMS=1 row cliff.** Only ~113 of 458 delivered rows land in memory per frame. Byte budget ~120 KB/frame regardless of row width or burst size.
3. **458 vs 480 rows delivered.** Bridge delivers 458 per "frame" — 22 short of 480. Unclear whether true content is 240 field-weaved or 480 with drops.

**Hypothesis for new branch test:** Wii → ElectronWarp pixel-doubling is analog-NTSC-induced (13.5 MHz native sample rate → 27 MHz HDMI with doubling). A 720p60 or 1080p24 source device (non-Wii, HDMI-native) would have no such doubling. Expected results if hypothesis holds:
- No IN_REP needed (clear VI_REP back to 0x00).
- IDS `WC = width × 3` (full width, no halving).
- IDS `rows` matches full vertical (720 or 1080).
- Possibly no 16-row phase drift if the drift is a byproduct of the odd input-clock situation.

If 720p/1080p works clean, decide whether 480p path is worth fixing or if marvin permanently uses a higher-res source and downsamples in software for vision work.

### 2026-05-01 — Row-alignment drift is periodic: 5/11 split every 16 rows

Added per-row byte-phase scanner to `ISC_Capture_ProbeFrame`. For a solid-color source, it finds which mod-3 offset within a pixel carries the content byte, and logs rows where the phase transitions.

**Result — exactly periodic across every frame, every run:**

```
phase: row   0 -> 2   \
phase: row   5 -> 0    \  5 rows phase 2, 11 rows phase 0 — 16-row cycle
phase: row  16 -> 2    /  repeats for all 113 captured rows
phase: row  21 -> 0   /
...
phase: row 112 -> 2
```

Period = **16 rows = 17,280 bytes exactly.** 5:11 split within each cycle. Net phase shift per cycle = 0 (stable). Transition jumps are ±2 bytes mod 3.

**Rules out:** random bit errors (would be irregular), constant per-row drift (would give period 3), content-driven phase (bytes can't rotate within pixels for solid color).

**Points at:** structured byte misalignment introduced by the CSI/CSI2DC/ISC data path on a fixed 17,280-byte cycle. 5:11 and 16 don't obviously map to any HDMI 480p line structure (525 total / 858 per line / 45 vblank).

**Next diagnostic candidates** (in priority order):

1. **ISC DMA burst size.** `ISC_DCFG=0x00000020` → YMBSIZE=BEATS8. At 8-beat bursts, 1080-byte rows don't divide evenly into the burst granularity. Try YMBSIZE=SINGLE (no bursting) or BEATS16 and see if period changes or disappears. Single-line config change in `isc_capture.c`.
2. **CSI2DC Data Pipe vs Video Pipe.** We use VP. DP has different packing semantics and may not have this periodic realignment. Bigger pivot — requires re-checking how ISC consumes from DP.
3. **CSI2DC FIFO level.** `FIFOCTL=0x0176` (374) in TC358743 is unrelated; that's a bridge-side FIFO. But CSI2DC has its own internal FIFO governed by different settings — worth checking datasheet.

Not yet ruled out: MIPI CSI-2 LS/LE short packets leaking into the RMS=1 byte stream. But if that were the cause, the drift would be per-row (every line has LS+LE), not period-16.

**Update — two experiments later, both negative:**

1. **ISC DMA YMBSIZE SINGLE (vs BEATS8 default).** `ISC_DCFG` went from `0x20` → `0x00`. Phase pattern bit-for-bit identical. Rules out ISC DMA burst granularity. fps and row-cliff unchanged — BEATS8 wasn't bottlenecking throughput either.
2. **CSI2DC VPCFGR.PA=0 (vs PA=1 default).** `VPCFGR` went `0x6024` → `0x2024`. Phase pattern bit-for-bit identical. Rules out 12-bit-bus MSB alignment.

So the drift is inherent to CSI2DC's Video Pipe output for this data type + RMS combo. No VPISR overflow bits set (RATEOVF=0, CTLOVF=0, STE=0), so no buffer-fill issue.

**Remaining levers (ease descending):**
- `GCFGR.ULC=1` — tell CSI2DC to use LS/LE packets for line-boundary detection. If bridge emits LS/LE and CSI2DC currently ignores them, turning ULC on might re-sync line boundaries. Single-bit change.
- Switch VP → Data Pipe. Bypasses ISC entirely; CSI2DC writes memory via its own DMA. Much larger change, affects capture/interrupt plumbing.
- Accept drift and software-realign at display time. Pattern is 100% deterministic (5/11 split in 16-row cycle) so a render-time byte-offset LUT would work. Trades cleanliness for done-ness.

### 2026-05-01 — Tried forcing 640x480p via EDID; Wii/ElectronWarp refused

Hypothesis was: if the source can be pushed off 720x480 onto 640x480 (VIC 1), we'd avoid the pixel-doubling path entirely and get 640 unique pixels directly. Updated EDID to advertise ONLY VIC 1 (native, single VIC in CEA VDB) plus matching base-block detailed timing (VGA 25.175 MHz). Result: **source still reports 720x480p**. Either the ElectronWarp has no 640x480 output path, or the Wii won't downsample its internal render. Reverted EDID to original 720x480-preferred (VIC 2/3/1 in VDB).

Takeaway for future: the Wii → ElectronWarp chain only emits 720x480p@60 HDMI; if we want different CEA resolutions we'd need a different source device. Don't retry this knob. The pixel-doubling behavior documented in the VI_REP session is the permanent nature of this source.

### 2026-05-01 — VI_REP IN_REP=1 flipped the halving: clean 360x~480, not 720x~240

Applied the samsonx-inspired fix: `VI_REP.IN_REP_HEN=1, IN_REP=1` (2x horizontal pixel-repetition de-replication). Single-line change in `tc358743_set_csi_color_space_rgb888`. VI_REP reads back as 0x11, confirmed.

**Result — before vs. after:**

| Metric       | Before (VI_REP=0x00) | After (VI_REP=0x11) |
|--------------|----------------------|---------------------|
| IDS WC       | 2160 (= 720 px × 3)  | 1080 (= 360 px × 3) |
| IDS rows     | 241                  | 458                 |
| Bytes/frame  | ~520,560             | ~494,640            |
| Content      | scrambled, ~358/720 non-zero, shifting row-to-row | clean 3-byte pixel triples, solid color for solid source |

Total bytes per frame stayed roughly constant (both ~half of 1.04 MB for full 720×480×3), but the organization flipped from "half-rows-of-full-width garbage" to "full-rows-of-half-width clean data".

**Hex dump quality check** (solid red source, 0xFF R input):
```
r  0 x  0+: 000000 00 000000 00 000000 00 0000C0 0000C0 0000C0 0000C0 ...
r 30 x  0+: b0=[00-00] b1=[00-C0] b2=[00-00]  (R channel only)
```

GRB byte order (Phase 5b) with R=C0 (= 0xFF × 0.753 A2D gain) repeating cleanly every 3 bytes. Exactly what solid red should look like. Content is **correct**.

**Interpretation — what the Wii/ElectronWarp chain is actually doing:**

Wii analog component video has native horizontal resolution of ~340-360 pixels (standard NTSC analog). In 480p mode, Wii outputs 360 unique pixels × 480 progressive lines of analog. The ElectronWarp re-samples this to HDMI with **2x horizontal pixel repetition** — 720 HDMI pixel clocks per line, each unique pixel sent twice — and flags it as CEA VIC 2/3 (720x480p). TC358743 detects this correctly (`DE_WIDTH_H=720, VI_STATUS1=0x00`, i.e., progressive) but, without `IN_REP_HEN`, passes the pixel-doubled stream straight through to CSI-2. With `IN_REP_HEN=1, IN_REP=1`, the bridge strips the duplicates and the real 360-pixel content falls out cleanly.

So the horizontal "halving" isn't a bug — **360 px IS the Wii's actual horizontal resolution**. No amount of register tweaking recovers more pixels because they don't exist on the input side.

**Vertical story — slightly open:** we now get 458 rows instead of 241, close to 480 but not quite. Could be (a) real — 22 lines lost to the bridge's internal blanking handling; (b) each frame alternates between 240-line and 218-line captures due to some deinterlacer/frame-counter quirk; (c) IN_REP=1 happens to also enable field-weave which double-maps one 240-line field into 480 rows (so true unique content is still 240). Need another frame-level sample to tell. For marvin's purposes, 458 or 480 rows are both "full frame vertically" — good enough unless the vision layer demands exactly 480.

**Status of the two journal-flagged structural quirks:**
- ~~VPROW=241 half-frame~~: **resolved** by `IN_REP_HEN=1`. Now 458 rows per frame.
- ~~~50% zero-pad per row~~: **resolved** by `IN_REP_HEN=1`. Rows are dense now.
- **ISC RMS=1 byte cliff persists** — capture still stops at ~56 dest rows (≈ 120K bytes). With narrower source rows (1080 vs 2160) this now equals ~111 source rows captured of the 458 delivered. Independent issue.

**Cosmetic follow-up on next code pass:** set `ISC_Capture_Configure` width to 360 instead of 720 so destination-row layout matches source-row layout 1:1 (currently each 720-wide dest row holds 2 source rows side-by-side). Doesn't change correctness of the captured bytes, just makes the layout reason-about-able.

**Code change this session:** single-line edit in `tc358743_set_csi_color_space_rgb888` enabling VI_REP IN_REP_HEN | IN_REP=1. Comment updated. Stale "BGR byte order" text in `isc_capture.c` probe print fixed to "GRB" + A2D gain note.

### 2026-05-01 — 480i root-cause investigation (model-switch session, no code changes)

No code changes this session. Investigated the two remaining structural quirks from Phase 5b:

**Issue 1: TC358743 delivers only 241/480 lines per CSI-2 frame (IDS-confirmed)**

Hypothesis now: the source (Wii → ElectronWarp) is likely outputting **HDMI 480i** (two interlaced fields of ~240 active lines each), not 480p — and the TC358743 is outputting each field as a separate CSI-2 frame at 60 Hz (= 60 fields/second).

Evidence:
- IDS shows 241 rows per CSI frame. 480i field 1 has 241 active lines, field 2 has 240. Exactly matches.
- Linux driver investigation confirms `MASK_INTER` in `CSI_CONFW` is an **interrupt-enable bit** (CSI_INT_ENA), not a line-count control. The driver has no CSI-output line-count register for interlaced mode — it just outputs whatever fields the bridge provides.
- Linux driver `tc358743_s_dv_timings()`: detects `V4L2_DV_INTERLACED` via `VI_STATUS1.MASK_S_V_INTERLACE` (bit 0), but never changes any CSI output config based on it. Bridge hardware handles the field→packet mapping autonomously.
- `VI_REP.IN_REP_HEN` and `IN_REP` confirmed 0x00 (no horizontal pixel-repetition). This was the last register candidate; all config registers are now ruled out.

**Why TC358743 reports 480p**: Our `read_detected_format()` checks `VI_STATUS1.MASK_S_V_INTERLACE` and prints 'i' or 'p'. If this reads 0 (progressive), it may be that the TC358743 is misdetecting the field structure, OR the ElectronWarp is doing bob-deinterlace internally so the HDMI output truly appears progressive to the bridge while still only having 240 unique lines per field delivered to CSI.

**Issue 2: ~358/720 pixels of real content per row**

Hypothesis: 480i uses 2× HDMI pixel repetition (each unique pixel clocked twice). Active content ≈ 360 unique pixels, doubled to 720 pixel clocks. But VI_REP=0x00 (IN_REP_HEN=0) means the bridge is NOT de-replicating — it should be sending both copies of each pixel, giving 720 non-zero bytes. This conflicts with the observation of zeros in ~half the row. Need more data.

**Alternate hypothesis for both issues**: The Wii via ElectronWarp is outputting CEA-861 VIC 6/7 (720×480i, 2× pixel rep) but the TC358743's internal HDMI decode somehow only captures the first field and only the first half of the pixel-rep. Requires measuring whether VI_STATUS1.INTERLACE = 1 (currently unknown — we only checked for 'p' in the print).

**Next session action items:**
1. Add VI_STATUS1 raw value print to `read_detected_format` (it already reads `vi1` — add a raw hex dump).
2. Check `VI_STATUS3` for AVI InfoFrame pixel-repetition field (bits [3:0] of AVI byte 5 via HDMI RX).
3. Consider: if source is confirmed 480i, accept 241 rows as correct behavior and move on to Phase 6 with partial frame.

**Web search status**: Attempted on claude-sonnet-4-6 but API returned 400 error. Model returning to claude-opus-4-7.

**User-provided web search summary (post-model-switch)** — Common TC358743 480p issues:
1. Width not divisible by 32 → distorted images / "green bars". Ours: 720 not div by 32, but IDS WC=2160 shows full-width packets are being sent. Rules this out for our specific symptom.
2. First-frame corruption (discard frame 0). Not our issue — we see steady-state.
3. MIPI lane-count mismatch (source on 1 lane, host expects 2). Not our issue — both sides 2-lane and locked.
4. Link frequency too high. We're at 297 Mbps/lane, the article's recommended fallback. Good.
5. **EDID not forcing true 480p60** → source falls back to 480i or another mode. **This is our remaining lever**. If Wii → ElectronWarp is outputting 480i despite our EDID, bridge correctly outputs each field as 241-row CSI frame.

**Updated next-session plan (post web search):**
1. Instrument: print `VI_STATUS1` raw hex byte in `read_detected_format` (currently only checks 1 bit for 'i'/'p'). Confirms whether INTERLACE bit is actually 1. The raw value disambiguates "progressive with bridge bug" from "interlaced as expected".
2. If INTERLACE=1: accept 241 rows as correct for 480i. Options to get 480p: (a) update EDID to force VIC 2 only and deny 480i, (b) confirm Wii's output mode (Wii has 480p setting in system menu — worth re-verifying), (c) ElectronWarp may have a 480p/480i switch.
3. If INTERLACE=0 AND VI_STATUS1 shows no unexpected bits: escalate — bridge internally doing something unusual. Next candidates: VI_STATUS2 registers, AVI InfoFrame raw read, FIFO underrun signalling.

### 2026-05-01 — RPi forum find: 480p on TC358743 is pixel-doubled (samsonx report)

Greg shared https://forums.raspberrypi.com/viewtopic.php?t=120702 — the canonical RPi TC358743 thread (30 pages, 737 posts). Two user reports match our symptom exactly:

**Samsonx (Sept/Oct 2016):**
> "for some reason 720x480p60 is giving me some strange issues. If I set the width and height to 720x480 the video is scrambled..."
>
> "setting the width and height to 1440x480 will give me unscrambled video but with two images wide and the height is squished."
>
> "I notice I get the same appearance of the second image if I view 1280x720 HDMI video with MMAL set to 2560x720."

**Orbital6 / 6by9 (page 7):** 480p fails with `MMAL_ERROR_ENOSPC` on RPi; 6by9 suspected "timing registers in the EDID." Thread shows 480p issue was **never definitively root-caused** by the RPi team.

**Interpretation for marvin:** the bridge emits 480p content as if it were **1440×240 on the wire** — i.e., 2× horizontal pixel repetition without de-replication. Maps directly to what our IDS reports:

- `rows=241` ≈ 240 source lines actually emitted (matches "1440×240" output)
- ~358/720 non-zero pixels per 720-wide CSI row ≈ half a 1440-pixel source line landing in each 720-wide window
- "Pattern start-position shifts row-to-row by constant amount" = what you see when a 1440-pixel scanline rotates across a 720-wide view
- 241 rows × 60 fps ≈ 14460 lines/sec ≈ source delivering ~240-line fields at 60Hz

So both halvings (H and V) are actually the **same 2× pixel-doubling phenomenon** manifested differently, NOT two independent bugs.

**Why VI_STATUS1 reports progressive (`p`)**: detected-timing registers reflect the HDMI wire format (480p as declared by source). The CSI-TX path count of 240-ish lines is what the bridge actually emits after its internal video pipe. Detection vs. emission can legitimately disagree when pixel repetition is in play.

**Fix candidates, in priority order:**
1. **VI_REP.IN_REP_HEN=1, IN_REP=1** — tell the bridge to de-replicate 2× horizontal pixel repetition. Currently VI_REP=0x00 so the bridge passes both copies of each pixel through. Easiest to try.
2. Read AVI InfoFrame pixel-repetition field (CEA-861 byte 5, bits [3:0] — "PR" pixel repetition). If PR=1, source is advertising 2× rep. Bridge should respect it.
3. Capture at 1440×240 and verify samsonx's "two side-by-side images" observation reproduces. If yes, confirms the 1440-wide emission.
4. Modify EDID to only advertise VIC 2 (720×480p, no pix-rep) and drop VIC 3 / 1 / unspecified — may push source to emit true 480p instead of 2×-doubled 480p.
5. Investigate if ElectronWarp has a 480p/480i/pix-rep switch (hardware-side root cause, since Wii native 480p component has no HDMI pixel repetition concept).

**Caveat**: Claude could only read portions of the RPi thread via WebFetch (30 pages, 737 posts total; sampled pages 1, 4-8, 11, 16, 21). Further useful context may exist on pages not yet read.

### 2026-05-01 — Phase 5b PASS: RGB888 capture path characterized

Found, verified, and documented the working RGB888 capture config and its behavior. Key facts:

**Pipeline (iter 6 / final working config):**
- CSI2DC VP: `RMS=1, PA=1, DT=0x24`, `CSI2DC_VPCFGR = 0x00006024`
- ISC RLP: `MODE=BYPASS (0xF), ALPHA=0, YMODE=0`, `ISC_RLP_CFG = 0x0000000F`
- ISC DMA: `IMODE=PACKED8, YMBSIZE=BEATS8`, `ISC_DCFG = 0x00000020`
- ISC PFE: `BPS=EIGHT, MIPI=1, CONT=1`, `ISC_PFE_CFG0 = 0x40004080`
- Memory: 3 bytes per pixel, packed, `W*H*3` bytes per frame.

**Byte order per pixel in memory is GRB (not BGR, not RGB):**
```
byte 0 = G, byte 1 = R, byte 2 = B
```
Verified by feeding the pipeline solid R, G, B, and white. Every primary hits exactly the predicted byte position with 0x00 on the other two. Deterministic across hundreds of frames.

**A2D chain is linear with gain ≈ 0.753:**
- Input 0xFF → output 0xC0 (= 0xFF × 0.753)
- Input 0xC0 → output 0x90
- Input 0x80 → output 0x60
- No clipping anywhere below 0xC0. Fully invertible in software if needed. This is the analog component-video → HDMI gain through ElectronWarp, not an ISC/pipeline artifact.

**What does NOT work in RMS=0 mode (for the record):**
- `ARGB32` RLP requires upstream pipeline stages (CFA/WB/CSC) to deliver pre-grouped per-pixel RGB. With those bypassed for direct-dump, RLP=ARGB32 writes one sample into a 4-byte slot and zero-pads the other three. Looks like "181 rows of b0 only". Prior engineer hit this in their D0–D9 cycles.
- CSI2DC in RMS=0 on DT=0x24 collapses to one 12-bit sample per pixel clock (not 3 component samples). Appears as "single byte of data per pixel, row-dependent phase." RMS=0 does NOT decompose RGB888 — only YUV formats (per Linux driver conventions).

**What DOES work: RMS=1 byte stream + BYPASS RLP + PACKED8 IMODE.** Datasheet's explicit recommendation: "Select BYPASS for MIPI RMS mode" (DS60001813D, RLP_CFG MODE field description).

**Structural quirks that remain (content-independent, confirmed across R/G/B/white):**
- `VPCOL=540, VPROW=241` per frame from CSI2DC. VPROW=241 ≈ half of 480 source rows. Suggests interlaced delivery or TC358743 output-mode issue. VPCOL=540 counted in units other than pixels (540 × 4 = 2160 = full-row byte count).
- ISC writes only 60 source-rows of memory per frame with RMS=1 byte stream. Prior-session data shows RMS=1 hits a fixed-byte-budget cliff regardless of burst size (D27/D31/D34 in the prior journal). Equivalent to 60 × 720 × 3 = 129,600 bytes.
- Within each captured row, pattern covers roughly the middle half with zeros on the edges; pattern start-position shifts row-to-row by a constant amount.

None of the above varies with source content. All three are pipeline-structural.

**Decision log additions to check:**
- Byte order is GRB. Render code must swap accordingly (or CSC matrix can absorb it).
- A2D linear gain ≈ 0.753. If full-contrast display matters, apply inverse gain clamped to [0, 255].

**Files at Phase 5b pass:**
- `plib_csi2dc.c`: `RMS=1` restored (local fix, re-apply after MCC regen)
- `isc_capture.c`: `rlpMode = ISC_RLP_CFG_MODE_BYPASS`, `layout = ISC_LAYOUT_PACKED8`, `ISC_CAP_BPP = 3`. Probe instrumentation kept (sentinel prefill, dense hex dump, VPCOL/VPROW readback).
- `app.c`: `ISC_Capture_ProbeFrame()` called from fps tick.

### 2026-05-01 — Drifted away from 60fps baseline; restored and reverted to ARGB32 direct-dump

Worked Phase 5b across multiple configuration hypotheses: YUV422 end-to-end, RGB888 PACKED8 direct-dump with DAT8, with and without PFE cropping, with and without RMS=1, various COLMAX multipliers. All of these edited `isc_capture.c` pipeline config AND `plib_csi2dc.c` simultaneously. By the time the probe showed "80 rows written, top 1/6 of frame, byte values 0x66/0x76/0x86", the live config had drifted so far from the committed `a0f9950` 60fps-working baseline that no single observation told us anything useful about what was broken.

**Key miss:** the journal's "carried into future sessions" section explicitly calls out `plib_csi2dc.c: CSI2DC_Configure_VideoPipe adds CSI2DC_VPCFGR_RMS_1 bit` as a **required local fix** (known to be needed for the 60fps-working state). I removed that during YUV422 experimentation (see prior session entry "RMS_1 reverted" which was wrong) and did not restore it. Combined with simultaneously switching BPP 4→3 and RLP ARGB32→DAT8 and adding PFE crop programming, the pipeline was in a never-tested state.

**Reset.** Reverted `plib_csi2dc.c` and `vision_isc.yml` to `HEAD` (RMS_1 restored, YAML back to MCC default). Rewrote `isc_capture.c` to match the committed baseline pipeline config (ARGB32 / PACKED32 / MCC defaults, no PFE crop programming — ISC captured 720×480×4 at 60 fps without manual PFE cropping on `a0f9950`, so crop programming isn't actually required). Kept only the probe instrumentation (`ISC_Capture_ProbeFrame`, sentinel pre-fill, VPISR/FNVC0 dump, `app.c` probe call). Probe now interprets bytes as ARGB32 in memory order (b0=A, b1=R, b2=G, b3=B).

**Next flash** should restore 60 fps, probe should show the full 480 rows written. Only *then* can we reason about byte content against the known source.

### 2026-05-01 — Planning kickoff

- Surveyed marvin and emirror to map existing I2C / CSI / ISC scaffolding and template patterns. (See "Current focus" above for findings.)
- Confirmed mainline Linux TC358743 driver is present locally in `linux-at91` and usable as the primary reference.
- Agreed phased plan (Phases 0–6 above) and the initial decisions in the decision log.
- Journal started; added project-wide rules in `CLAUDE.md` (always read/update the journal; keep code comments lean).

### 2026-05-01 — YUV422 probe showed garble; back to RGB888 with correct direct-dump config

Flashed the YUV422 pipeline. Source is verified solid `R=FF G=C0 B=80` on a separate HDMI display (Wii game rendering → component → ElectronWarp → HDMI). In our captured buffer:

- Most of the frame: `AC 0F AC 0F` repeating. Decodes to nonsense colors regardless of UYVY / YUYV / YVYU / VYUY interpretation (best attempt → near-black luma, single chroma value; should be Y≈0xBF, Cb≈0x5A, Cr≈0xA0).
- Top-row samples: `20 20 20 20` / `26 26 26 26` (different from mid/bot).
- `buf=0` mid-rows have zeros; `buf=1` mid-rows have `AC 0F` pattern. Alternate-buffer inconsistency.
- Full-row range scan: `b0=[28-FC] b1=[06-AF]` — way too wide for a solid source. Pipeline is producing positionally-variable bytes from a uniform input.

Conclusion: the issue isn't "wrong format", it's something more fundamental with the pipeline that YUV422 didn't fix and may have obscured.

Greg pointed out that `plib_csi2dc.h` explicitly defines `CSI2DC_DATA_FORMAT_RGB888 = 0x24` (as well as RGB444/555/565/666). **Hardware supports RGB888 end-to-end**; the "RGB888 not supported" claim in the earlier Linux driver survey was a driver-policy artifact, not a hardware limitation.

Going back to RGB888 with the canonical direct-dump config that we hadn't tried:

- TC358743 color space: RGB888 (`set_csi_color_space_rgb888`).
- CSI data type: `CSI2_DATA_FORMAT_RGB888` (0x24).
- CSI2DC data type: `CSI2DC_DATA_FORMAT_RGB888` (0x24). RMS=0 unchanged.
- ISC RLP: `DAT8` (keep direct-dump).
- ISC layout: `PACKED8` (keep).
- ISC input format: `DRV_IMAGE_SENSOR_RGB`.
- Bytes/pixel: **3** (packed RGB888, no alpha padding).

For a solid source, the expected memory pattern is unambiguous: `FF C0 80 FF C0 80 FF C0 80 ...` repeating every 3 bytes. If we see that, pipeline works. If we see something else, the specific deviation points at the specific bug.

### 2026-05-01 — Switched pipeline to YUV422 end-to-end (matches Linux + emirror)

Surveyed the Linux DWC MIPI CSI-2 RX driver (`drivers/media/platform/dwc/`) and the Microchip CSI2DC / ISC drivers (`drivers/media/platform/microchip/`). Findings that overturn several of my earlier guesses:

- **CSI2DC video pipe does NOT support RGB888.** Its format table in `microchip-csi2dc.c:100-148` only contains RAW (6-14 bit) and YUV422 8-bit. Setting `VPCFGR.DT = 0x24` is a dead end. This is exactly why emirror's working config chose YUV422. I had wanted to preserve RGB888 for simplicity, but the hardware doesn't let us.
- **`CSI2DC_VPCFGR.RMS` is unconditionally 0 in Linux** (`microchip-csi2dc.c:398`). Our earlier hack to OR-in `RMS_1` was wrong in both the architectural and the I-hacked-an-MCC-file senses. Reverted.
- **`CSI2DC_VPCFGR.PA` is unconditionally 1 in Linux** (`microchip-csi2dc.c:399`). Matches MCC's `videoPipeAlign = CSI2DC_POST_ALIGNED` default. No change needed.
- **For any non-RAW input coming through CSI2DC, Linux ISC runs in "direct dump" mode**: `isc_try_configure_rlp_dma()` at `microchip-isc-base.c:850-854` sets `rlp_cfg_mode = DAT8 (0x0)`, `dcfg_imode = PACKED8`, `dctrl_dview = PACKED`. Not ARGB32 (our original), not BYPASS (0xF, my second guess — and BYPASS isn't even a defined mode in the Microchip ISC RLP register per the driver header). DAT8 is the answer for pre-decoded bytes.
- **Pipeline stages are all disabled for non-RAW** (`microchip-isc-base.c:873-879`: `bits_pipeline = 0`). Our code already does this for RGB-input; correct.

**Applied changes** (Phase 5b, next flash):
1. `plib_csi2dc.c` — dropped the `RMS_1` OR (match Linux default RMS=0).
2. `tc358743.c` — replaced `set_csi_color_space_rgb888` with `set_csi_color_space_yuv422` (`VOUT_SET2 |= SEL422 | 422FIL_100`; `VI_REP = VOUT_COLOR_601_YCBCR_LIMITED`; `CONFCTL.YCBCRFMT = 422_8_BIT`). Mirrors the UYVY path in the kernel driver.
3. `isc_capture.c` — `iscObj->inputFormat = YUV_422`, `rlpMode = DAT8`, `layout = PACKED8`. Also overrides `csiObj->csiDataType = CSI2_DATA_FORMAT_YUV422_8 (0x1E)` and `csi2dcObj->{videoPipeDataType,dataPipeDataType} = CSI2DC_DATA_FORMAT_YUV422_8`. ISC_CAP_BPP dropped from 4 to 2.
4. Probe layout legend updated: UYVY / YUYV / YVYU / VYUY interpretations instead of ARGB/BGRA/RGBA/ABGR.

**Expected probe pattern** for source R=FF G=C0 B=80 converted to BT.601 limited YCbCr: Y≈0xBF (191), Cb≈0x5B (91), Cr≈0xA0 (160). If CSI-2 order is UYVY, memory bytes should read ~`5B BF A0 BF` repeating. If YUYV, ~`BF 5B BF A0`.

### 2026-05-01 — Phase 5b: probe reveals byte-level garble; RLP mode mismatch suspected

Source set to solid R=0xFF G=0xC0 B=0x80. Probe output:

```
ISC probe (frame=N, buf=B, 720x480):
  TL (   0,   0) mem: 00 00 00 00           ← row 0 mostly zero
  TM ( 360,   0) mem: B0/70 00 00 00        ← sparse non-zero, varies between frames
  TR ( 719,   0) mem: E4/A4/64/E4/24 ...    ← ditto
  ML (   0, 240) mem: AC 0F AC 0F            ← consistent across rest of frame
  CT ( 360, 240) mem: AC 0F AC 0F
  ... every mid/bottom sample: AC 0F AC 0F
  mid-row rng: b0=[2C-EC] b1=[03-8F] b2=[2C-EC] b3=[07-8F]
```

Expected if ARGB32 layout + source FF/C0/80: something like [FF FF C0 80] or equivalent. Observed `AC 0F` clearly isn't ARGB32 of the source.

**Analysis:** the `AC 0F AC 0F` pattern is 2-byte-repeating, characteristic of YUV422 not ARGB32. Most frame is consistent garbage with this pattern. Row 0 is additionally broken (zeros + sparse variable bytes).

**Hypothesis:** ISC RLP mode is wrong. We set `iscObj->rlpMode = ISC_RLP_CFG_MODE_ARGB32` (0xA) which expects RGB from the ISC's internal processing pipeline (Bayer demosaic etc.). But we also set CSI2DC's RMS=1 (byte-stream to memory format). Per `isc.h` comment at `ISC_RLP_CFG_MODE_BYPASS` (0xF): *"32-bit input is sampled and written to the rlp output port. Select this mode for MIPI RMS mode."*

The two settings are mutually exclusive — RMS byte-stream should pair with BYPASS, not ARGB32. Our ISC is trying to re-pack already-packed bytes, mangling them.

**Applied fix:** override `iscObj->rlpMode = ISC_RLP_CFG_MODE_BYPASS` in `isc_capture.c`. Next flash should show bytes that pattern-match the source (FF/C0/80 somewhere in the buffer). If still garbled, next candidate is to align more carefully with emirror's working YUV422 end-to-end (which uses RMS=1 + RLP=YYCC at `0xB`, despite the datasheet's BYPASS hint).

### 2026-05-01 — Phase 5b implemented (frame content probe)

Added `ISC_Capture_ProbeFrame()` to dump raw bytes at 9 sample points plus a full-center-row range scan, once per second alongside the fps report. Sits at the pre-display layer so we can verify byte-level correctness against known source patterns (solid colors, test patterns) before rendering.

Uses `SYS_CACHE_InvalidateDCache_by_Addr` over the sampled buffer so CPU reads see DMA's writes. Samples from the most-recently-completed buffer (`(frameIndex - 1) & 1`) to avoid racing the DMA writer.

Next flash: source = a known solid color, read the probe output, confirm byte layout matches expectations. Any mismatch (wrong channel order, range clipping, partial capture) becomes a concrete fix before Phase 6.

### 2026-05-01 — Session wrap-up

End-to-end HDMI capture pipeline working: Wii → ElectronWarp → splitter → Waveshare TC358743 → MIPI CSI-2 @ 297 Mbps × 2 lanes → SAM9X75 D-PHY RX → CSI2DC → ISC → DMA → SDRAM at sustained 60 fps, 720×480 ARGB32 (~83 MB/s).

**New code this session:** Phases 0-5 implemented from scratch (`tc358743.c/h`, `isc_capture.c/h`, `app.c` wiring, user.cmake, drv_image_sensor.h shim) + targeted fixes to two MCC-generated files (`plib_csi.c`, `plib_csi2dc.c`).

**All Phase 5 fixes that were needed together** (any one missing and it doesn't work):
1. TC358743 PLL 594 → 297 Mbps/lane (match HSFREQRANGE band 0x14).
2. plib_csi.c `CSI_Analog_Init` — Lane 1 missing bit-rate write (the 3rd test-code transaction).
3. plib_csi2dc.c — OR `CSI2DC_VPCFGR_RMS_1` (byte-stream memory layout).
4. isc_capture.c — `enableMIPIFreeRun = true` override (MIPIFRN=0, matches TC358743 continuous clock).
5. app.c — ordering: Configure CSI-RX → TC358743 EnableStream(true) → ISC Start_Capture. The D-PHY must be listening when TC358743's `TXOPTIONCNTRL 0→CONTCLKMODE` produces the LP11→HS edge.

**Next session:** Phase 5b (frame content verification — probe captured bytes in SDRAM against known-source patterns). Then Phase 6 (display on LCD via Legato). Detailed scope notes in Phase 5b / Phase 6 sections above; carried items in Open questions. Nothing is in-progress / partially done — all code is committed-ready.

### 2026-05-01 — Phase 5 passes on hardware: 60 fps sustained 🎉

```
TC358743: detected 720x480p @ 60 Hz, RGB limited-range
ISC_Capture: configured 720x480 ARGB32 (1382400 bytes/frame)
ISC_Capture diag (pre-start):
  CSI_PHY_RX=0x00030000      ← bit 16 (clock out-of-ULP) + bit 17 (RXCLKACTIVEHS) SET
  CSI_INT_ST_MAIN=0x00000004
  CSI_INT_ST  PHY_FATAL=0    PKT_FATAL=0    FRAME_FATAL=0
  CSI2DC_GSR=0x00000000      ← ARSTIP cleared
  ISC_INTSR=0x08000003
ISC_Capture: capture started
ISC: 37 fps                   ← partial first-second window
ISC: 60 fps                   ← steady-state 60 fps sustained
ISC: 60 fps
  ...
```

D-PHY RX acquired the incoming DDR clock; ARSTIP released on its own confirming the journal's note that it self-clears once HS traffic is seen. ISC DMA is delivering ~83 MB/s of ARGB32 pixel data into the framebuffer in DDR3 at 60 Hz.

**The full set of fixes needed beyond the initial Phase 5 port:**
1. TC358743 PLL 594 → 297 Mbps/lane (match HSFREQRANGE band 0x14).
2. CSI2DC VPCFGR.RMS = 1 (byte-stream memory layout for pre-decoded RGB888).
3. CSI2DC MIPIFRN = 0 (free-running; matches TC358743 continuous-clock mode).
4. **RX-configure-before-source-transmit ordering** — the single biggest fix. D-PHY has to be out of reset and listening when TC358743's `TXOPTIONCNTRL 0→CONTCLKMODE` toggle produces the LP11→HS edge.
5. plib_csi.c `CSI_Analog_Init` — 3rd transaction (bit-rate data write) added for Lane 1 (was missing; journal's "Test O"). Lane 2/3 typo fixes + missing-lane-1-init in 3/4-lane paths also cleaned up, though not exercised by our 2-lane setup.
6. plib_csi2dc.c `CSI_Configure_VideoPipe` — OR in `CSI2DC_VPCFGR_RMS_1` bit.

Also noted but turned out not to matter for this debug: TC358743 init order (EDID before or after PLL). Our order (EDID last) worked; emirror's order (EDID before PLL) also worked. Not a critical distinction for bring-up.

Next: Phase 6 — display captured frames via Legato on the LCD.

### 2026-05-01 — Cross-referenced Greg's prior debug journal (commit 1d0d3d4)

Key takeaway from the prior debug journal: **ARSTIP is never "fixed" directly** — it's a downstream status that clears on its own once the D-PHY actually sees HS traffic. No register write makes it clear; the D-PHY has to lock first. That reframes our debugging: if our ordering fix causes the D-PHY to finally catch the LP11→HS edge, ARSTIP will clear as a side effect.

Confirmed our current state matches the working baseline from the journal's "Test W" (297 Mbps + band 0x14). Remaining deltas from emirror's order: we load EDID *after* set_pll/set_csi whereas emirror does it before. Kernel driver doesn't load EDID in initial_setup at all (ioctl-driven), so it's not inherently wrong — but if current fixes don't clear ARSTIP, try swapping EDID before PLL next. Also noted: `CSI_PUSR`/`CSI_PCR`/`PMR` readback was suggested by the journal's author as the next diag step if D-PHY lock is still mysterious.

### 2026-05-01 — Phase 5 fourth fix: ordering of RX-config vs. source-stream-enable

Third flash with MIPIFRN=0 still showed `CSI2DC_GSR=0x02` (ARSTIP stuck). Root cause: we were configuring CSI-RX **after** TC358743 had already enabled its stream. Comparing emirror's working flow:

```
CAMERA_Open:
    DRV_CSI2DC_Configure()   ← RX side ready first
    DRV_CSI_Configure()
    DRV_ISC_Configure()
CAMERA_Start_Capture:
    DRV_ImageSensor_Start()  ← *then* source transmits
    DRV_ISC_Start_Capture()  ← *then* ISC arms + polls VD
```

The TC358743 LP11->HS transition is triggered by its `TXOPTIONCNTRL 0->CONTCLKMODE` toggle inside `enable_stream()`. The SAM9X75 D-PHY RX has to be out of reset and actively listening to catch that edge — otherwise it never locks onto the byte clock, downstream CSI2DC's async-reset domain never releases (ARSTIP stuck at 1), and VD never fires at ISC.

**Restructured:** (a) split `ISC_Capture_Start(w, h)` into `ISC_Capture_Configure(w, h)` (all CSI2DC/CSI/ISC + DMA config) and `ISC_Capture_Start()` (just `DRV_ISC_Start_Capture` + IRQ enable). (b) Exposed `TC358743_EnableStream(bool)` publicly and removed auto-enable from tc358743's watcher — watcher now only tracks state. (c) `app.c` coordinates the correct order: `Configure -> EnableStream(true) -> Start`. (d) Flipped internal configure order to CSI2DC → CSI → ISC to match emirror.

### 2026-05-01 — Phase 5 third flash: clock-mode mismatch found

Second flash (after PLL 594→297 + RMS=1) moved the needle:

```
CSI_PHY_STOPSTATE=0x00000000  ← was 0x03 (LP11). Data lanes are now transitioning HS.
CSI_INT_ST_PHY_FATAL=0        ← no fatal PHY errors
CSI_INT_ST_PKT_FATAL=0        ← no packet decode errors either
CSI2DC_FNVC0R=0               ← but still zero frames at CSI2DC
CSI2DC_GSR=0x00000002         ← **ARSTIP=1 (async-reset stuck)**
```

`ARSTIP` stuck is the exact symptom the emirror working code's comment calls out for clock-mode mismatch:

> *csiContinuousClock=true (camera layer programs CSI2DC.MIPIFRN=0 = free-running) AND with the tc358743_start() pulse. Mismatched bridge/host clock modes leave CSI2DC.GSR.ARSTIP stuck.*

MCC's `CSI2DC_ENABLE_MIPI_CLOCK_FREE_RUN = false` sets MIPIFRN=1 (gated). But TC358743 transmits continuous-clock (TXOPTIONCNTRL = CONTCLKMODE). Mismatch. **Fix:** override `csi2dcObj->enableMIPIFreeRun = true` in `isc_capture.c` (same override pattern as csiBitRate — leaves MCC config untouched).

### 2026-05-01 — Phase 5 first flash: D-PHY not locking; applying fixes from working commit

Phase 5 code built and ran. TC358743 locked HDMI (720x480p60 RGB limited) and reported TXACT. But ISC `DRV_ISC_Start_Capture` timed out on VD interrupt. Diagnostic dump showed:

```
CSI_PHY_RX=0x00010000        ← only bit 16 (clock lane out-of-ULP); bit 17 (PHY_RXCLKACTIVEHS) NOT set
CSI_PHY_STOPSTATE=0x00000003 ← both data lanes stuck in LP11 stop state
CSI_INT_ST_* all 0           ← PHY never even tried to lock
CSI2DC_FNVC0R=0              ← zero frames at CSI2DC
```

So SAM9X75 D-PHY RX is blind to the incoming HS clock from TC358743. Greg pointed at a working commit pair in git history (31a967c → af69a79) of emirror experiments. Diff surfaced two critical mismatches vs. our Phase 1-4 config:

1. **HSFREQRANGE band vs. bridge PLL rate mismatch.** Our TC358743 PLL outputs 594 Mbps/lane (PLL_FBD=88). Our SAM9X75 HSFREQRANGE code is `0x14` (270-299 Mbps band, what the working commit settled on). Those don't match — the D-PHY is set to expect ~300 Mbps but the bridge is emitting ~600 Mbps. **Fix:** drop the bridge PLL to 297 Mbps (PLL_FBD=44). Working commit's comment explicitly identifies this as the calibrated standard point: `v13 (current, Test W): 0x14 (270-299) paired with bridge PLL at 297 Mbps/lane (FBD=44, FRS=1)`. D-PHY timing counts (LINEINITCNT et al.) from the kernel driver's 594 Mbps table remain valid at 297 Mbps (LP timings are conservatively long).

2. **CSI2DC VPCFGR.RMS not set.** Default is RMS=0 ("one pixel per component per clock, ISC-engine-compliant"), which is correct for Bayer-sensor input where ISC performs demosaic. For pre-decoded RGB888 from TC358743 we need RMS=1 ("byte stream compliant with CSI-2 spec memory format"). Without this, even if the D-PHY locked, the video pipe would malformat pixels into memory. **Fix:** OR `CSI2DC_VPCFGR_RMS_1` into the value written by `CSI2DC_Configure_VideoPipe()` in plib_csi2dc.c.

Also fixed: `app.c` retry semantics — `capture_attempted` flag (not `capture_started`) prevents the ~1s retry loop when `DRV_ISC_Start_Capture` fails.

Two new MCC-file modifications joining the set we're maintaining: `plib_csi2dc.c` RMS bit. (plib_csi.c lane-1 fix was already there.)

Re-flashing next.

### 2026-05-01 — Phase 5 implemented (isc_capture + app.c coordination)

Added `default/src/isc_capture.{c,h}` as a pure SoC-side capture driver:
- Initializes `DRV_ISC` / `DRV_CSI2DC` / `DRV_CSI` in order.
- Populates ISC object fields from `configuration.h` constants; overrides `DRV_CSI`'s hardcoded `csiBitRate = 0x16` with `0x14`.
- Frame-done callback increments `g_frame_count`; public `ISC_Capture_FrameCount()` + `ISC_Capture_IsRunning()` expose state.
- `ISC_Capture_Start(w, h)` configures CSI/CSI2DC/ISC, arms DMA against a `.region_cache_aligned` 32-byte-aligned framebuffer sized for 1920x1080 ARGB32 × 2 (double-buffered; ~16 MB in DDR3), enables `SYS_INT_SourceEnable(ID_ISC)`.
- `ISC_Capture_Stop()` does the inverse.

`tc358743.c` stays a pure bridge-chip driver. Added public state-query API (`TC358743_IsLocked()`, `TC358743_GetDetectedFormat(w, h)`) backed by module-scope caches (`s_sysStatus`, `s_detectedWidth/Height`) updated by the watcher. Internal SYNC 0↔1 logic continues to toggle TC358743's own CSI-TX stream.

`app.c` is the orchestrator. Per `APP_Tasks` tick:
1. `TC358743_Tasks()` — pump the watcher.
2. `app_coordinate_capture()` — observe TC358743 lock state; on 0→1 (with a cached format), call `ISC_Capture_Start(w, h)`; on 1→0, call `ISC_Capture_Stop()`. Uses `capture_started` flag (not just "was_locked") to handle the case where lock goes high before the format is cached.
3. `app_report_fps()` — once per second (gated by `SYS_TIME`), if capture is running, print `ISC: N fps` as delta from the last poll.

Pass criteria:
- No build errors.
- With source chain live, DBGU eventually shows `ISC_Capture: started 720x480 ARGB32 (1382400 bytes/frame)` after TC358743 watcher reports `SYNC`, followed by `ISC: ~60 fps` once per second.

### 2026-05-01 — plib_csi.c `CSI_Analog_Init` refactor (pre-Phase 5)

Fixed known bugs in `CSI_Analog_Init` before wiring up Phase 5. Greg flagged that Lane 1+ weren't getting a bit-rate-range write matching Lane 0's lines 134-136. Investigation surfaced more bugs:

- Lane 1 missing bit-rate write (primary, in 2-lane path).
- Lane 2 had a typo `TESTDIN(0x24)` vs the commented `code = 0x64` (two places — 3-lane and 4-lane paths).
- Lane 2 / Lane 3 missing bit-rate writes.
- 3-lane and 4-lane paths never initialized Lane 1 (the if/else-if structure skipped it).

**Clock lane check against Linux reference (`linux-at91/drivers/media/platform/dwc/dw-dphy-rx.c:344-351`)**: Linux driver writes `HS_RX_CTRL_LANE0..3` per data lane but NOT a clock lane HS RX control. The clock lane is globally configured via HSFREQRANGE / OSC_FREQ_TARGET. Therefore we left the clock lane init (the 0x34 block) unchanged — don't add bit-rate data write there.

**Fix shape:** extracted a helper `csi_phy_hs_rx_init(lane_code, bit_rate)` that does addr-select + 0x94 + bit_rate. Called once per active data lane, gated by `nlanes >= CSI_DATA_LANES_N`. Corrects the Lane 2 address (0x24 → 0x64). The if/else-if chain is gone; 2-lane, 3-lane, 4-lane all fall out correctly.

**File is MCC-generated.** If MCC regenerates `plib_csi.c` it will clobber this fix. Noted in open questions so we remember to re-apply. At least the fix is self-contained and easy to redo.

### 2026-05-01 — Phase 4 passes on hardware; source mode variability noted

Multi-sample diagnostic confirms CSI-TX is transmitting:

```
TC358743: CSI stream enabled
TC358743: CSI_STATUS=0x00001044 [] CSI_ERR=0x00000000          ← 10 ms: no TXACT yet
TC358743: CSI_STATUS=0x00001244 [TXACT ] CSI_ERR=0x00000000    ← 60 ms: TXACT set
TC358743: CSI_STATUS=0x00001244 [TXACT ] CSI_ERR=0x00000000    ← 260 ms: still set
```

TXACT is momentary by design — it's "currently transmitting a packet", not "stream is on". It clears during LP11 gaps between packets/frames. Our 10 ms sample lands in a gap, the later samples catch active packets.

Also observed: this boot detected `1440x240p @ 60 Hz` (CEA VIC 8/9, NTSC pixel-doubled) instead of the previous run's `720x480p @ 60 Hz`. **Cause was Wii-side**: the Wii had been reset and reverted to a default output mode; it needs to be put back into 480p. Expected source mode is 720x480p@60 under normal operation; not something our pipeline needs to accommodate.

### 2026-05-01 — Phase 4 first flash: TXACT absent in CSI_STATUS

First flash of Phase 4 passed functionally — full lock/unlock/relock cycles work correctly:

```
TC358743: init complete; SYS_STATUS=0x1F
TC358743: post-HPD SYS_STATUS=0x9F
TC358743: watcher start; SYS_STATUS=0x9F
TC358743: detected 720x480p @ 60 Hz, RGB limited-range
TC358743: CSI stream enabled
TC358743: CSI_STATUS=0x1044 []
TC358743: SYS_STATUS 0x9F->0x1F [DDC5V TMDS PLL SCDT HDMI ]
TC358743: CSI stream disabled
...
TC358743: CSI stream enabled
TC358743: CSI_STATUS=0x1044 []
```

But `CSI_STATUS=0x1044` doesn't have TXACT (bit 9 / 0x0200) or any named bit set. Three unknown bits (2, 6, 12) are set. HLT is clear (not halted) which is reassuring. Added multi-sample + full 32-bit readout + CSI_ERR for next flash. See open questions.

### 2026-05-01 — Phase 4 implemented (CSI stream enable, still no ISC side)

Added `tc358743_enable_stream(bool)` (direct port of kernel driver's `enable_stream`) and wired it into the watcher: stream enables on `S_SYNC` 0→1, disables on 1→0, and also enables on first-poll if the chain is already locked at boot. After enable, the watcher dumps `CSI_STATUS` (reg 0x0410) so we can see whether `TXACT` flips on.

Nothing programmed on the SAM9X75 ISC side yet — the CSI-TX will just drive the CSI-2 bus into the SoC where the receiver either accepts or drops; we proceed to Phase 5 for the capture side.

### 2026-05-01 — Phase 4a passes on hardware; full HDMI lock

Boot output with Wii + ElectronWarp + splitter chain active:

```
TC358743: init complete; SYS_STATUS=0x09
TC358743: post-HPD SYS_STATUS=0x09
TC358743: watcher start; SYS_STATUS=0x09
TC358743: SYS_STATUS 0x09->0x1F [DDC5V TMDS PLL SCDT HDMI ]
TC358743: SYS_STATUS 0x1F->0x9F [DDC5V TMDS PLL SCDT HDMI SYNC]
TC358743: detected 720x480p @ 60 Hz, RGB limited-range
```

Confirmed:
- Full HDMI handshake completes.
- Source format is **720x480p @ 60 Hz, RGB limited-range** (Wii 480p native, matches our EDID's preferred timing).
- Limited-range RGB (SMPTE 16-235) — downstream pipeline will need range expansion, or we accept a slightly dim/gray display.

Ready to enable CSI streaming (Phase 4 proper) and then wire ISC capture (Phase 5).

### 2026-05-01 — Phase 4a watcher implemented

Filled in `TC358743_Tasks()` with a 100 ms-cadence SYS_STATUS poller. On each transition prints `0xAA->0xBB [DDC5V TMDS PLL SCDT HDMI SYNC]` with current-state bit names. On the first 0→1 edge of `S_SYNC`, reads `DE_WIDTH_H/V`, `FV_CNT`, `VI_STATUS1/3` and prints active resolution, scan type, fps, color space, and range. Color-space lookup table mirrors the kernel driver's `input_color_space[]`.

Pollling only (no IRQ). `log_detected_format` does 8 I2C reads so it only runs on the SYNC 0→1 edge, not every poll.

First-boot output now shows initial state as `watcher start; SYS_STATUS=0xXX` then transitions as the source negotiates.

### 2026-05-01 — Phase 3b implemented (with CEA extension)

Added EDID load + HPD enable to `tc358743_do_init()`:

- **256-byte EDID** = VESA 1.3 base + CEA-861-D extension. Base-block detailed timing = 720x480@60p (27 MHz pclk, matching Wii via ElectronWarp). Extension advertises **HDMI VSDB** (OUI 0x000C03, phys addr 1.0.0.0) and VICs 2/3/1 via a Video Data Block (VIC 2 native). Extension added up-front since prior attempts at this bringup indicated HDMI sources refuse without the VSDB + VIC advertising — saves an iteration cycle.
- Both checksum bytes (127 and 255) patched at runtime so hand-layout arithmetic can't break the load.
- Sequence: `HPD_OUT0=0` → write `EDID_LEN1=2, EDID_LEN2=0` → write 128 bytes to `EDID_RAM + 0` then 128 bytes to `EDID_RAM + 128` → 150 ms delay → `HPD_OUT0=1`.
- Single-shot post-HPD `SYS_STATUS` readback 200 ms after HPD rise.
- `TC358743_TX_BUF_SIZE` bumped 8 → 132 to fit the full 128-byte block write in one I2C transaction (matches kernel's `I2C_MAX_XFER_SIZE = 130`).

Clarified in the 3b plan that HPD is folded into this subphase (not Phase 4) because source EDID re-read depends on an HPD toggle.

### 2026-05-01 — Phase 3 passes on hardware

Boot output:

```
TC358743: probe starting
TC358743: present (chipid=0x0000)
TC358743: init (2 lanes, 594 Mbps/lane, RGB888)
TC358743: init complete; SYS_STATUS=0x00
```

All init steps succeed (no `init: X failed` message between the "init" and "init complete" lines). `SYS_STATUS=0x00` is consistent with "no HPD asserted, no EDID programmed" — nothing is expected to lock yet. Whether `DDC5V` eventually flips to 1 is partly up to the Waveshare's 5V-sense routing (version-dependent) and whether the source asserts +5V independent of HPD; we'll find out in Phase 4.

### 2026-05-01 — Phase 3 implemented (EDID deferred to 3b)

Ported `tc358743_initial_setup()` + callees from the kernel driver:

- Reset + sleep + FIFOCTL + `set_ref_clk` (27 MHz constants)
- DDC_CTL (100 ms 5V debounce), EDID_MODE = E-DDC
- `set_hdmi_phy` (PHY_EN, CTL1/2, BIAS, CSQ=0x0A, AVM=45, HDMI_DET, HV_RST)
- VI_MODE (RGB in DVI), VOUT_SET2 (auto color), VOUT_SET3 (ext count)
- `set_pll` (PRD=4, FBD=88 → 594 Mbps/lane, FRS=0 since >500 MHz)
- `set_csi` (disable D2/D3, program 9 D-PHY timing counts, HSTXVREGEN for C+D0+D1, CONTCLKMODE, STARTCNTRL, CSI_START, 4-step CSI_CONFW sequence for 2-lane HS mode)
- `set_csi_color_space_rgb888` (clear SEL422 / 422FIL / YCBCRFMT, set RGB_FULL color select)

Added `tc358743_wr8_and_or` helper missed in Phase 2 and a `delay_us` wrapper.

EDID load deferred (Phase 3b) — needs a hand-constructed 128-byte EDID with correct byte-layout and checksum; separate concern from init correctness.

HPD enable and `enable_stream` deferred to Phase 4.

After `TC358743_Initialize`, firmware prints `SYS_STATUS=0xXX`. On hardware without HPD asserted by the source, `DDC5V` bit may set if source is providing 5V on HDMI; other bits (TMDS, PHY PLL, SYNC) will be 0 pending HPD + EDID.

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
