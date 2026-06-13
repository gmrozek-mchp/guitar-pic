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

**Phase 2 DONE — the Wii fully initializes fauxmote as an assigned Wiimote.**
`wiimote.c` answers the Wii's output reports (`0x17` read→`0x21`, `0x15`→`0x20`
status, `0x16` write→`0x22` ack, `0x11` LEDs, `0x12` reporting mode, `0x13`/`0x1a`
IR-enable acks) and streams `0x30` core-button reports (~15 ms) for keep-alive.
On hardware: clean full handshake, the Wii assigns a player slot (`0x11`), and the
GPIO13 status LED (no real player LEDs on a Feather) goes **solid = assigned**
(heartbeat=waiting, fast-blink=connected). IR is acked but not implemented (only
needed for the pointer, not guitar gameplay).

**Core-Wiimote + test-CLI workstream (before guitar).**

- **Step A DONE — manual test CLI + connection management.** `esp_console` REPL
  (`console_cli.c`): `pair`/`stop`/`reconnect`/`unlink`/`status`/`btn`/`tap` — all
  thin pass-throughs over the module API (logic lives in `wiimote.c`/`bt_hid_device.c`,
  so Marvin can drive the same API later). Boots idle; `pair` arms the HID listeners
  for that sync window and `stop` tears them down; servers are **armed per pairing
  window**, not at boot or auto-re-armed (re-arming leaked L2CAP slots, since
  `reconnect` uses client connects). Honors the Wii's requested reporting mode
  (`0x12` → builds that report ID; buttons populated, accel/IR/ext zeroed). Disconnect
  fully resets state (`Wiimote_NotifyDisconnected`) and readers exit by L2CAP handle
  (robust to fd reuse). Bond persists in NVS (recalled at boot via
  `esp_bt_gap_get_bond_device_list`); `unlink` removes it. Status LED on GPIO13:
  blip/~3 s = idle, fast blink = pairing/connecting, N flashes = assigned player N.
- **Step B DONE — IR pointer.** Pointer `(x,y)` is a state variable in `wiimote.c`
  (`Wiimote_SetPointer`/`ClearPointer`), CLI `point x y` / `point off`. `build_ir_extended`
  synthesizes two sensor-bar dots into mode `0x33`'s 12-byte extended-IR field + a
  level accel. **Verified on hardware** — navigated the Wii menu and launched GH3 with
  it. Calibration constants (`IR_X/Y_CENTER/HALF`, from on-hardware edge measurement)
  map pointer 0..1 to the full screen, (0,0)=top-left. Basic-IR modes `0x36`/`0x37`
  are now filled too (`build_ir_basic` + shared `ir_dots`); the byte packing is
  verified against Dolphin's `IRBasic` struct and the Linux `hid-wiimote` decoder.
  Pointer tracks correctly on the main Wii screen and its Home overlay (basic `0x37`),
  so one calibration covers both modes (see the GH3-pointer note under Phase 3).

**Phase 3 DONE — guitar extension plays GH3 (detection + frets + strum + whammy).**
On hardware: GH3 detects the guitar and frets/strum/whammy register in-game.

- **Module split.** The guitar lives in its own `guitar.c` behind a small interface
  (`wiimote_ext.h`: `wiimote_extension_t` = register bank + `build_report`/`set_button`/
  `reset`; `Wiimote_RegisterExtension`). `wiimote.c` is now extension-agnostic (serves
  the registered bank for `0xa4xxxx` reg I/O, calls `build_report` for the ext field,
  delegates unknown button names to `set_button`). Buttons drive by name through
  `Wiimote_SetButton`/`TapButton` (green/red/yellow/blue/orange/strumup/strumdown/
  gplus/gminus/pedal); CLI `btn`/`tap`/`whammy`/`ext` are thin pass-throughs. Adding a
  nunchuk/classic later is just another self-registering `*.c`.
- **Report bit layout** (buttons **active-low**, rest = 1; verified against wiibrew +
  Linux `hid-wiimote`): byte0/1 = stick X/Y (bits5-0); **byte0/1 bits7-6 = 1 to
  identify a GH3 Les Paul** (0 = GHWT — with them clear GH3 treats us as GHWT and
  ignores the strum bar, which cost real debugging time); byte2 touchbar; byte3
  whammy; byte4 bit6 BD / bit4 B− / bit2 B+; byte5 bit7 BO / bit6 BR / bit5 BB /
  bit4 BG / bit3 BY / bit2 pedal / bit0 BU. GH3 streams mode **`0x37`** (not `0x34`);
  the ext field is filled for every ext-bearing mode (`0x32/34/35/36/37/3d`).
- **ENCRYPTION is required and now implemented** (`ext_crypto.c/.h`). GH3's actual
  handshake (from a register trace), *not* the old `0→0x40` init the plan guessed:
  `0x55`→`0xf0` (disable) → read ID at `0xfa` in the clear (decide GH3 vs GHWT) →
  `0xAA`→`0xf0` (re-enable) → 16-byte key to `0x40`-`0x4f`. Everything the game then
  reads (streamed ext bytes + the `0x20` calibration) is decrypted with that key, so
  plaintext reads as garbage. The base Wiimote captures the key, derives the ft/sb
  tables, and encrypts outgoing ext data (streamed bytes at reg offset `0x08`; reg
  reads at `addr & 7`); the guitar module stays plaintext. Cipher = the marcan Wii
  extension cipher; **rnconrad's `wm_crypto.c` tables are corrupted** (sbox[3]@0x60,
  [4]@0xd8, [6]@0x88/0xec — `0xD0`/`0xF0` & `0xD2`/`0xE2` flips, not valid
  permutations), so the tables were taken from **Dolphin's 1st-party** set and the
  algorithm verified term-for-term against Dolphin; round-trip validated against GH3's
  real key (idx 0 matched, encrypt→decrypt identity). Earlier "green/red work without
  encryption" was a decryption fluke.
- **Guitar-mode pointer (informational, not pursued).** Basic-IR pointer is correct on
  the main Wii screen + its Home overlay. *Inside GH3*, the Home-overlay cursor scales
  wrong — X over-sensitive, Y under, **opposite per axis** — which is GH3 applying its
  own pointer transform, not our IR (packing is verified identical to extended). Moot
  in practice: **with the Wiimote seated in the guitar the IR camera is physically
  blocked**, so guitar-mode Home nav uses the **analog stick (SX/SY)** instead. Kept a
  single IR calibration. Deferred idea: drive `SX/SY` (guitar module already owns them,
  fixed at center `0x20`) for guitar-mode Home navigation if Marvin ever needs it.

**Link stability (done):** idle disconnects are fixed — Bluedroid's JV idle→sniff
delay (default 5 s) is overridden to 65 s via a `-D BTA_FTC_OPS_IDLE_TO_SNIFF_DELAY_MS`
in the top-level CMake (`#ifndef`'d `#define`; the timeout field is UINT16 + a 197 ms
offset, so stay < ~65338). With sniff effectively off the link stays active (Wii keeps
polling), so no supervision timeouts and no BTA PM-slot leak (the leak that previously
forced a power-cycle). A persistent **auto-reconnect** task re-initiates the link on
any unexpected drop (3 s × 8 then every 8 s), suppressed by `stop`/`unlink`.

**OPEN — GH3 game-launch handoff (needs a BT sniffer):** when a game disc boots, the
Wii goes HID-silent toward fauxmote for many seconds, then drops the link (`rsn 0x08`).
Auto-reconnect re-establishes it, but the session isn't reliably usable in-game.
A real Wiimote rides the handoff with a quick ~2 s LED off/on. fauxmote's own logs
don't show *why* the Wii goes silent on us vs a real Wiimote — there's no command we
visibly mishandle (zero RX reports during the gap). Diagnosing needs a Bluetooth
sniffer to diff a real Wiimote vs fauxmote through the GH3 launch. **Workaround for
now: restart fauxmote after the game has started** (a fresh boot reconnects cleanly).
The recurring `mode 3 … BASIC mode` reconnect warning is likely a red herring (real
Wiimotes use Basic-mode L2CAP for HID). Revisit after the guitar extension — GH3 may
engage the controller differently once it sees a guitar.

**Other notes:**
- **Keep-awake:** the Wii idle-disconnects on lack of *input*; streaming reports +
  occasional button activity keeps it alive (free during gameplay).
- **Caveat:** a Wiimote cannot power a Wii on from standby (BT radio off); "wake" only
  means reconnecting while the console is already running.

Phase progression and success criteria are in [`../SPEC.md`](../SPEC.md) §6.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-13 | **Extension encryption is mandatory for GH3 and implemented in the base Wiimote (`ext_crypto.c`); the guitar module stays plaintext.** GH3's real key handshake (register trace): `0x55`→`0xf0` (disable) → read ID `0xfa` in clear → `0xAA`→`0xf0` (enable) → 16-byte key to `0x40`-`0x4f`. The base captures the key, derives ft/sb, and encrypts outgoing ext data (streamed bytes @ offset `0x08`; reg reads @ `addr & 7`). | Everything GH3 reads from the extension is decrypted with that key, so plaintext is garbage (the "green/red worked unencrypted" observation was a decryption coincidence). Encryption is a generic Wiimote feature (nunchuk/classic encrypt too), so it belongs in the base, not the guitar. Corrects the plan's guess that GH3 used the old `0→0x40` init. |
| 2026-06-13 | **Cipher tables come from Dolphin's 1st-party set, not `rnconrad/WiimoteEmulator`'s `wm_crypto.c`.** rnconrad's S-boxes are corrupted (sbox[3]@0x60, [4]@0xd8, [6]@0x88/0xec are `0xD0`/`0xF0` & `0xD2`/`0xE2` transcription flips → not valid permutations). Used Dolphin's `keygen_sbox_1st_party` + `sboxes_1st_party[8]`; verified the key-schedule/encrypt algorithm term-for-term against Dolphin and round-tripped GH3's real key (idx 0, encrypt→decrypt identity). | Wrong table bytes silently corrupt the keystream → undebuggable "notes are garbage." Dolphin is GPL and validated against real games; the marcan tables are reverse-engineered hardware constants (facts), reproduced with our own code. |
| 2026-06-13 | **Guitar is a self-registering extension module behind `wiimote_ext.h`; `wiimote.c` is extension-agnostic.** `wiimote_extension_t` = register bank + `build_report`/`set_button`/`reset`; `Wiimote_RegisterExtension`. Buttons drive by name via `Wiimote_SetButton`/`TapButton`. | Plans for other extensions (nunchuk/classic) as drop-in `*.c` files, keeps the base generic, and keeps the CLI as thin pass-throughs over the same API Marvin will use. |
| 2026-06-13 | **Byte0/1 bits 7-6 must be set (GH3 Les Paul ident); single IR calibration kept.** Clear ident bits make GH3 treat us as a GHWT guitar and ignore the strum bar. Basic-IR pointer is correct on the main Wii screen with the same calibration as extended; the GH3-internal Home-overlay mis-scaling is GH3's own transform and is moot (IR is physically blocked when the Wiimote is in the guitar — guitar-mode Home nav uses the analog stick). | Found by hardware testing. No second/basic-mode calibration: the format packing is provably identical to extended (Dolphin + Linux), so the only real difference (GH3) isn't ours to fix and doesn't matter on real hardware. |
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
- **Q5 — marvin↔fauxmote link** (UART bitmask mirror of the fretboard protocol vs USB CDC vs other). Deferred; revisit before Phase 4 integration.

(Q4 resolved 2026-06-13: GH3 streams mode `0x37`; the guitar report is the first 6 ext bytes and must be **encrypted** — see decision log.)

---

## Session log

### 2026-06-13 — Phase 3 done: GH3 guitar plays (module + encryption + basic IR)

- **Guitar module split.** New `wiimote_ext.h` (`wiimote_extension_t` interface +
  `Wiimote_RegisterExtension`) and `guitar.c`; `wiimote.c` made extension-agnostic.
  CLI `btn`/`tap`/`whammy` unchanged (thin pass-throughs).
- **Detection + report.** ID `00 00 A4 20 01 03` at `0xfa`; GH3 streams mode `0x37`.
  Fixed strum being ignored — root cause was **byte0/1 bits7-6 cleared** (we looked
  like a GHWT guitar to GH3). Bit layout re-verified against wiibrew *and* the Linux
  `hid-wiimote` guitar parser (strum bits were correct all along).
- **Encryption (`ext_crypto.c`).** Captured GH3's real key handshake via a temporary
  register trace (`0x55`→`0xf0` → read ID → `0xAA`→`0xf0` → 16-byte key). Implemented
  the marcan cipher in the base Wiimote (encrypt streamed ext bytes + reg reads; guitar
  stays plaintext). Found rnconrad's `wm_crypto.c` S-boxes corrupted → used Dolphin's
  1st-party tables; verified the algorithm against Dolphin and round-tripped GH3's
  actual key. On hardware: **frets, strum, and whammy register in GH3.**
- **Basic IR.** Filled `0x36`/`0x37` (`build_ir_basic`, packing cross-checked vs
  Dolphin `IRBasic` + Linux decode). Pointer correct on the main Wii screen + Home
  overlay. GH3's in-game Home overlay mis-scales the cursor (GH3's own transform) but
  it's moot — IR is blocked when the Wiimote is in the guitar (analog-stick nav there).
- **Console cleanup.** Removed the temporary `WR`/`RD` register-trace logs; demoted the
  per-report RX log to `ESP_LOGD` (it was garbling the `esp_console` line editor).
- Not committed yet (pending review).

### 2026-06-13 — Link stability (sniff + auto-reconnect); GH3 handoff open

- Idle disconnects root-caused to Bluedroid sniff: link idled → sniff → supervision
  timeout (`rsn 0x08`), and abnormal closes leaked BTA PM-profile slots (pool of 5 →
  reconnect fails → power-cycle). No clean public/repo override (PM spec is `const`),
  but the idle→sniff delay is a `#ifndef`'d `#define` → overrode it to 65 s via a
  global `-D` in the top-level CMake (`idf_build_set_property` before `project()`).
  Link now stays active; no timeouts, no PM leak.
- Added a persistent auto-reconnect task (device-initiated, retries with backoff,
  suppressed on `stop`/`unlink`) so unexpected drops self-heal like a real Wiimote.
- GH3 game-launch handoff still drops us and isn't reliably usable after reconnect;
  the Wii goes HID-silent then drops, and fauxmote's logs don't reveal why (no
  mishandled command). Documented as OPEN (needs a BT sniffer). Workaround: restart
  fauxmote after the game starts. Moving on to the guitar extension.

### 2026-06-12 — Step A: test CLI + connection management

- Added `esp_console` REPL (`console_cli.c`) — thin commands over the module API.
  Boot-idle; `pair` arms HID listeners + discoverable, `stop` tears down. Honors the
  Wii's reporting mode. Bond recall at boot + `unlink`. New LED scheme (idle blip /
  fast blink / N-flash player slot). `tap` logic moved into `Wiimote_TapButton`
  (non-blocking, sender auto-releases).
- Bugs found + fixed on hardware during cycle testing:
  - Disconnect left `connected=1` → `Wiimote_NotifyDisconnected` now clears
    `s_data_fd` immediately (not just on reader exit).
  - Reader tasks leaked their link slot when a re-armed server recycled their fd →
    readers now exit by **L2CAP handle** signalled from the CLOSE event, not by
    `read()` return.
  - **L2CAP slot exhaustion after ~4-5 reconnect cycles**: auto-re-arming servers on
    every disconnect leaked server slots (reconnect uses *client* connects, so the
    re-armed servers were never consumed). Fix: arm servers only per `pair` window,
    tear down on `stop`; no auto-re-arm.
- Open: device-initiated `reconnect` uses Basic L2CAP mode vs the Wii's ERTM (may
  misbehave) — reliable path is `pair`+SYNC.

### 2026-06-12 — Phase 2b done: assigned Wiimote + status LED

- `wiimote.c` report state machine (ported from rnconrad's `wm_reports`/handlers,
  explicit bytes, no bitfields): EEPROM read (`0x21`, calibration at 0x16/0x20,
  error for >0x16FF), status (`0x20`), ack (`0x22`), reporting-mode/LED/IR acks,
  and a static sender task streaming `0x30` every 15 ms. Register reads currently
  return an error (no extension yet → Phase 3).
- Wired into the L2CAP reader (`Wiimote_HandleRx` per frame, `Wiimote_NotifyFdClosed`
  on close). Added `status_led.c` driving GPIO13 (heartbeat/fast-blink/solid) since
  the Feather has no player LEDs; reflects `Wiimote_IsConnected/IsAssigned`.
- On hardware: full clean handshake (`0x17`×n, `0x11`, `0x15`, `0x12`, `0x13`/`0x1a`,
  `0x16` IR-register writes), Wii assigns a slot, **LED solid = assigned**. Turned
  off the verbose Bluedroid trace block in `sdkconfig.defaults` (needs `set-target`
  to re-apply) — monitor is now readable.

### 2026-06-12 — Phase 2a: HID read path working

- Added per-channel reader tasks (static `xTaskCreateStatic`) that `read()` the
  L2CAP fds from `ESP_BT_L2CAP_OPEN_EVT`. Gotcha: `esp_bt_l2cap`'s VFS `read()` is
  **non-blocking** — empty rx queue returns `0` (not EOF), peer-close returns `-1`
  (errno `EPIPE`). So poll on `0`, exit only on `<0` (first cut wrongly treated `0`
  as EOF and the reader exited instantly). No `select` support on this VFS.
- First output report from the Wii (data channel, PSM 0x13): `a2 17 00 00 17 70 00
  01` = report `0x17` Read Memory, EEPROM offset `0x001770`, size 1. The Wii then
  **blocks waiting for our `0x21` (Read Data) reply** — confirms the report
  responder is the gate to a live/assigned controller. → Phase 2b.

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
