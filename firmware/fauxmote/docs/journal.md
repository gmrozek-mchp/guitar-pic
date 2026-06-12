# fauxmote — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for fauxmote. Newest entries at the top. For *what fauxmote is* (purpose, hardware, interfaces, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

## Current focus

**Phase 1 — Bluetooth identity / pairing (highest risk).** Phase 0 is done: the
Feather V2 boots clean on ESP-IDF v6.0.1, brings up the BR/EDR controller +
Bluedroid, and is discoverable over Classic as `Nintendo RVL-CNT-01` (verified with
`blueutil --inquiry` on macOS — saw the board's BD_ADDR `c0:cd:d6:37:fd:0a` with the
name resolved). Next: bring up the Bluedroid BT-Classic HID device, publish the SDP
record the Wii expects (name + VID `0x057e` / PID `0x0306` + Wiimote HID descriptor +
Class-of-Device), and answer the GAP PIN request so a real Wii will pair. Pairing PIN
for the 1+2 flow = this board's BD_ADDR reversed → raw bytes `0a fd 37 d6 cd c0`.

Phase progression and success criteria are in [`../SPEC.md`](../SPEC.md) §6.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-12 | **Build fauxmote as a parallel, independent proof-of-concept subproject; the fretboard GPIO-press path stays authoritative.** New top-level firmware subproject `firmware/fauxmote/`. | Device-side Wiimote *emulation* to a real Wii is rare/novel (host-side use is common), so de-risk it in isolation before touching marvin/fretboard/edge-ai. Nothing in the existing actuation path changes until fauxmote is proven. |
| 2026-06-12 | **Framework = ESP-IDF (v6.x; v6.0.1 installed), Bluedroid in Bluetooth-Classic-only mode (BLE disabled).** | Wiimote uses Bluetooth Classic HID; ESP-IDF exposes the BT-Classic HID-device API (`esp_hidd_api.h`), custom SDP records, BD_ADDR control, and raw L2CAP — all of which emulation needs. Arduino-ESP32's BT-Classic HID-device support is too thin for the custom SDP/descriptor the Wiimote requires. |
| 2026-06-12 | **Board = Adafruit ESP32 Feather V2 (product 5400); its *original* ESP32 is the load-bearing choice.** | The original ESP32 has a Bluetooth Classic (BR/EDR) radio. ESP32-S3/C3 are BLE-only and cannot emulate a Wiimote. Confirmed the product 5400 is the Feather V2 (not a QT Py S3). |
| 2026-06-12 | **marvin↔fauxmote command link is deferred; early phases use a local test command source.** | Lets the hard part (ESP32↔Wii Bluetooth emulation) be developed and validated standalone. The likely eventual link mirrors the fretboard 1-byte fret/strum bitmask over UART, but that's a later-session decision. |

---

## Open questions

- **Q1 — Does ESP-IDF's `esp_hidd` BT-Classic HID device let us publish the exact SDP record the Wii expects** (name `Nintendo RVL-CNT-01`, VID `0x057e`, PID `0x0306`, Wiimote HID descriptor, matching Class-of-Device)? If its default SDP is too rigid, fall back to raw L2CAP on PSM 0x11/0x13 with a hand-built SDP record. Resolve in Phase 1.
- **Q2 — Which pairing flow to target first?** The 1+2 temporary-pair flow uses the Wiimote's *own* BD_ADDR (reversed) as the PIN, which fauxmote knows and can answer in the GAP PIN-request callback. The sync-button permanent flow uses the *host (Wii)* address. Start with 1+2.
- **Q3 — Exact Class-of-Device value** the Wii matches a Wiimote on (lift from a real unit / wiibrew). Phase 1.
- **Q4 — Which Wiimote data report carries the guitar extension** (e.g. `0x34` = core buttons + 19 extension bytes vs `0x3d` = 21 extension bytes) and what the game expects. Phase 3.
- **Q5 — marvin↔fauxmote link** (UART bitmask mirror of the fretboard protocol vs USB CDC vs other). Deferred; revisit before Phase 4 integration.

---

## Session log

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
