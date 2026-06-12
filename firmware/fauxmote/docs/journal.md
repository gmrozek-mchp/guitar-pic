# fauxmote — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for fauxmote. Newest entries at the top. For *what fauxmote is* (purpose, hardware, interfaces, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Phase 1 DONE — the Wii pairs with fauxmote and opens both HID channels.** Custom
Wiimote SDP record (built via Bluedroid's internal `SDP_*` API in `wiimote_sdp.c`,
with SDP buffers raised to 512/1024) + raw `esp_bt_l2cap` servers on PSM 0x11/0x13.
On a red-SYNC press the Wii: accepts our SDP, completes legacy PIN pairing
(`auth OK`), and opens both HID L2CAP channels (control CID 0x40 / interrupt CID 0x41,
both `mtu 640`), then starts sending us data. Wii BD_ADDR `00:17:ab:07:2c:21`.

**Phase 2 next — keep the connection alive + behave like a Wiimote.** We don't yet
read the L2CAP fds. Read the control-channel fd (HIDP: input reports prefixed
`0xa1`, output `0xa2`), handle the Wii's output reports (status request `0x15` →
send status `0x20`; data-reporting-mode `0x12`; LED `0x11`; read/write register
`0x16`/`0x17`), and send core-button input reports (`0x30`). Success = a stable,
assigned Wiimote (player LED solid) in the Wii Home menu.

Phase 2 also owns the **sleep/wake + keep-awake** behavior:
- **Reconnect (device-initiated):** after the Wii drops us for idle (console still
  on), *we* must re-open the link — the Wii won't. Building blocks are in place
  (SDP `HIDReconnectInitiate=true`; we have the Wii BD_ADDR from pairing;
  `esp_bt_l2cap_connect(psm, bda)`). Store the bonded Wii address and reconnect
  PSM 0x11→0x13 on a trigger (a Marvin button), then resume reports.
- **Keep-awake:** the Wii's idle-disconnect watches for *input* activity, so the
  keep-alive must be an occasional **button input report**, not just any packet.
  During gameplay this is free (Marvin streams strum/fret input continuously).
- **Caveat (not a bug to fix):** a Wiimote cannot power a Wii on from full standby
  (red light) — the console's BT radio is off then. "Wake" only means reconnecting
  while the console is already running.

Phase progression and success criteria are in [`../SPEC.md`](../SPEC.md) §6.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-12 | **`esp_hidd` is a dead end for Wii emulation; the only working ESP-IDF path is a custom SDP record (internal Bluedroid `SDP_*` API) + raw L2CAP HID on PSM 0x11/0x13 (`esp_bt_l2cap`), with the hardcoded SDP buffer raised.** Two independent walls: (1) the Wii rejects esp_hidd's record even with a small descriptor (wrong HID attribute values — subclass/reconnect-initiate/etc., which esp_hidd hardcodes and doesn't expose); (2) the real 217-byte descriptor can't even be registered — `SDP_MAX_PAD_LEN` (the per-record attribute pad) is **hardcoded 300** (`sdp_db.c:495`, `bluedroid_user_config.h`; no menuconfig), and the full Wiimote record is ~463 B → `SDP_AddAttribute fail … ID 518`. Confirmed feasible building blocks in IDF v6.0.1: `SDP_CreateRecord`/`SDP_AddAttribute`/`SDP_AddSequence` (private `stack/sdp_api.h`) and `esp_bt_l2cap_start_srv(psm)`. | Bluedroid is the only Classic-BT stack on ESP32, and its public APIs can't host an arbitrary SDP record (`esp_sdp` RAW is search-only). So the path requires *unsupported internals*: raise `SDP_MAX_PAD_LEN` via an injected `CONFIG_BT_SDP_PAD_LEN` compile define, build the exact Wiimote record with the internal SDP DB API, skip esp_hidd, and serve HID over raw L2CAP + hand-rolled HIDP. Heavy + fragile across IDF updates, but it's the real route. The exact 463-byte record + 217-byte descriptor are saved at [`wiimote-sdp.md`](wiimote-sdp.md). |
| 2026-06-12 | **Q1 resolved: ESP-IDF `esp_hidd`'s auto-generated SDP record is NOT accepted by the Wii → pivot to raw L2CAP (listen on PSM 0x11 control / 0x13 data) + a hand-built SDP record matching a real Wiimote.** Verbose Bluedroid logs: the Wii opens an ACL link, connects to our SDP server (PSM 1), reads our records (we send 14/250/37-byte SDP responses), then disconnects the SDP channel and terminates the ACL (`rsn 0x13` = remote user terminated) **without ever opening the HID PSMs**. SSP was confirmed off this round (legacy pairing) and discovery works (limited-discoverable + COD `0x002504`), so SDP content is the only remaining blocker. Wii BD_ADDR observed: `00:17:ab:07:2c:21`. | The Wii validates the SDP record against a real Wiimote's; esp_hidd's generic HID record (right VID/PID, wrong attributes + HID descriptor) fails the check, so it never proceeds to HID. Matches why `rnconrad/WiimoteEmulator` replaces the host BT stack to serve the exact SDP. Open: whether ESP-IDF lets us serve a fully custom SDP record (esp_sdp API vs internal Bluedroid `SDP_*` API) and listen on the fixed HID PSMs via `esp_bt_l2cap`. |
| 2026-06-12 | **Phase 1 first attempt uses ESP-IDF's unified `esp_hidd` BT-classic HID device wearing the Wiimote identity, not a hand-rolled raw-L2CAP/SDP stack.** `bt_hid_device.c`: `esp_hidd_dev_init(ESP_HID_TRANSPORT_BT)` with VID `0x057e`/PID `0x0306`/version `0x0100`/name `Nintendo RVL-CNT-01`, COD set to `0x002504` (peripheral/joystick), a minimal vendor-defined report map (just report IDs 0x30 in / 0x12 out), **SSP disabled** (`CONFIG_BT_SSP_ENABLED=n`) for legacy PIN pairing, and a GAP `PIN_REQ` handler that replies with the requesting host's BD_ADDR reversed. | The stack already implements HIDP + L2CAP (PSM 0x11/0x13) + auth, so this is the cheapest probe of Q1: if a real Wii connects + authenticates against the auto-generated SDP, we avoid hand-building raw SDP entirely. If it rejects it, fall back to raw L2CAP + the exact SDP record from `rnconrad/WiimoteEmulator` (cloned locally for byte-exact bytes; WebFetch can't reproduce the ~463-byte blob). |
| 2026-06-12 | **Target the console-SYNC *bonding* flow, not the 1+2 temporary flow.** PIN handler returns the connecting host's (Wii's) BD_ADDR reversed (`param->pin_req.bda` byte-reversed). | Bonding (red SYNC button under the Wii's SD cover + controller in sync mode) is how a controller gets *persistently* registered, which is what a robot wants. The 1+2 flow is one-time/temporary and uses the controller's own address instead. Resolves Q2. |
| 2026-06-12 | **Build fauxmote as a parallel, independent proof-of-concept subproject; the fretboard GPIO-press path stays authoritative.** New top-level firmware subproject `firmware/fauxmote/`. | Device-side Wiimote *emulation* to a real Wii is rare/novel (host-side use is common), so de-risk it in isolation before touching marvin/fretboard/edge-ai. Nothing in the existing actuation path changes until fauxmote is proven. |
| 2026-06-12 | **Framework = ESP-IDF (v6.x; v6.0.1 installed), Bluedroid in Bluetooth-Classic-only mode (BLE disabled).** | Wiimote uses Bluetooth Classic HID; ESP-IDF exposes the BT-Classic HID-device API (`esp_hidd_api.h`), custom SDP records, BD_ADDR control, and raw L2CAP — all of which emulation needs. Arduino-ESP32's BT-Classic HID-device support is too thin for the custom SDP/descriptor the Wiimote requires. |
| 2026-06-12 | **Board = Adafruit ESP32 Feather V2 (product 5400); its *original* ESP32 is the load-bearing choice.** | The original ESP32 has a Bluetooth Classic (BR/EDR) radio. ESP32-S3/C3 are BLE-only and cannot emulate a Wiimote. Confirmed the product 5400 is the Feather V2 (not a QT Py S3). |
| 2026-06-12 | **marvin↔fauxmote command link is deferred; early phases use a local test command source.** | Lets the hard part (ESP32↔Wii Bluetooth emulation) be developed and validated standalone. The likely eventual link mirrors the fretboard 1-byte fret/strum bitmask over UART, but that's a later-session decision. |

---

## Open questions

- **Q1 — Does ESP-IDF's `esp_hidd` BT-Classic HID device let the Wii connect + authenticate against its auto-generated SDP?** Being tested now (see decision log). If the Wii rejects it, fall back to raw L2CAP on PSM 0x11/0x13 with the exact `rnconrad/WiimoteEmulator` SDP record.
- **Q4 — Which Wiimote data report carries the guitar extension** (e.g. `0x34` = core buttons + 19 extension bytes vs `0x3d` = 21 extension bytes) and what the game expects. Phase 3.
- **Q5 — marvin↔fauxmote link** (UART bitmask mirror of the fretboard protocol vs USB CDC vs other). Deferred; revisit before Phase 4 integration.

---

## Session log

### 2026-06-12 — Phase 1 complete: Wii pairs + opens HID channels

- Custom SDP path works. Replaced esp_hidd with: `wiimote_sdp.c` building the exact
  Wiimote record via internal `SDP_CreateRecord`/`SDP_AddAttribute`/… (modeled on
  Bluedroid's own `HID_DevAddRecord`, Wiimote-exact values), + `esp_bt_l2cap`
  servers on PSM 0x11/0x13. Raised `CONFIG_BT_SDP_ATTR_LEN=512` /
  `CONFIG_BT_SDP_PAD_LEN=1024` (defaults exist; depend only on `BT_CLASSIC_ENABLED`).
- On red-SYNC: SDP accepted → legacy PIN pairing `auth OK` → both HID L2CAP
  channels open (CID 0x40 control / 0x41 interrupt, mtu 640) → Wii starts sending us
  HID data (`BTA_JvL2capRead`). Phase 1 success criterion met on hardware.
- Build/integration lessons (durable):
  - To call the internal SDP API, add bt internal include roots in CMake:
    `<bt>/common/include`, `<bt>/host/bluedroid/{stack,common,osi}/include`,
    `<bt>/host/bluedroid/api/include/api`. `bdroid_buildcfg.h` is behind
    `#ifdef HAS_BDROID_BUILDCFG` (not defined externally) so it's skipped.
  - `esp_bt_l2cap` data path is fd/VFS-based: must `esp_bt_l2cap_vfs_register()`
    (after INIT_EVT) BEFORE `esp_bt_l2cap_start_srv`, else `l2cap_malloc_slot
    unable to register fd` / NO_RESOURCE.

### 2026-06-12 — Phase 1 debugging: discovery solved, SDP rejected

- esp_hidd HID-device first attempt — Wii initially saw nothing. Two fixes got it
  to connect: (a) the Wii's SYNC scan uses a *limited* inquiry → switched to
  `ESP_BT_LIMITED_DISCOVERABLE`; (b) `esp_hidd_dev_init` overrides the Class of
  Device → now set `0x002504` in the HIDD START handler (after init).
- **sdkconfig.defaults gotcha:** edits to `sdkconfig.defaults` do NOT apply once
  `sdkconfig` exists. `CONFIG_BT_SSP_ENABLED=n` + 8 MB flash silently didn't take
  effect until `idf.py set-target esp32` regenerated `sdkconfig`. Always regenerate
  (or menuconfig) after editing defaults.
- With legacy pairing active + verbose Bluedroid logs: Wii connects ACL → SDP
  (PSM 1) → reads our records → disconnects → ACL term `rsn 0x13`, never opening the
  HID PSMs. → Q1 resolved (see decision log). Pivoting to raw L2CAP + exact Wiimote
  SDP. Added a temporary verbose-BT-log block to `sdkconfig.defaults` (trim later).

### 2026-06-12 — Phase 0 verified on hardware

- Built + flashed on the Adafruit ESP32 Feather V2 (ESP-IDF **v6.0.1**, Apple
  Silicon macOS). Clean boot, no `ESP_ERROR_CHECK` abort: `BTDM_INIT` controller up,
  `Bluetooth MAC c0:cd:d6:37:fd:0a`, reached the "discoverable" log line.
- Discoverability confirmed Mac-only via `blueutil --inquiry 15`:
  `address: c0-cd-d6-37-fd-0a … name: "Nintendo RVL-CNT-01"`. (macOS System Settings
  doesn't expose a Classic inquiry; `blueutil` does. No Linux/Android needed.)
- Added a boot-time BD_ADDR log to `main/main.c` (`esp_bt_dev_get_address`) — also the
  basis for the Phase-1 pairing PIN (BD_ADDR reversed = `0a fd 37 d6 cd c0`).
- Bumped doc/version references v5.x → v6.0.1 across SPEC/journal/README/sdkconfig.
- Noted (not yet fixed): image header defaults to 2 MB flash on an 8 MB board
  (`CONFIG_ESPTOOLPY_FLASHSIZE="8MB"` to set later; harmless for now).

### 2026-06-12 — subproject created

- First mention of Wiimote emulation in the repo; confirmed no prior discussion.
- Verified the board (Feather V2 = original ESP32, has BT Classic) and the Wiimote
  protocol facts (Classic HID, PSM 0x11/0x13, SDP `Nintendo RVL-CNT-01` /
  `0x057e`/`0x0306`, PIN = reversed BD_ADDR, guitar extension ID `00 00 A4 20 01 03`
  + 6-byte report). Captured in [`../SPEC.md`](../SPEC.md) §3.
- Decisions above logged. Created the ESP-IDF scaffold: top-level + `main/`
  `CMakeLists.txt`, `sdkconfig.defaults` (Classic BT + Bluedroid HID device, BLE off),
  and a Phase-0 `main/main.c` (radio up + discoverable by name). Untested pending
  toolchain + hardware.
- Registered the subproject: journal added to `CLAUDE.md`, pointer row + repo-map
  entry added to root `SPEC.md`.
