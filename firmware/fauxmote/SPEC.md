# fauxmote — Specification

> What fauxmote *is* (purpose, hardware, interfaces, success criteria, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

## 1. Purpose

fauxmote is an **ESP32 firmware that emulates a Nintendo Wiimote with an attached
Guitar Hero guitar extension**, speaking Bluetooth straight to a real Wii console.
Rather than pressing buttons on a real controller, fauxmote *becomes* the
controller. It is the **controller node** (PLCA id 1) on the 10BASE-T1S bus and the
game-critical actuation path: [marvin](../marvin/docs/spec.md) drives it for menus,
manual control and its CV pipeline, and the [fretboard](../fretboard/SPEC.md)
detector drives it peer-to-peer while armed in a song. The
[guitar](../guitar/SPEC.md) node mirrors the same mask as an indicator. Bus roles:
[`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md) §7.1.

## 2. Hardware

| Component | Detail |
|---|---|
| Board | **Adafruit ESP32 Feather V2** (product 5400) — *original* dual-core ESP32 (Tensilica LX6), 8 MB flash, 2 MB PSRAM, USB-C. |
| Radio | ESP32 has a **Bluetooth Classic (BR/EDR)** radio — required, because the Wiimote link is Classic HID. (ESP32-S3/C3 are BLE-only and would *not* work.) |
| Wii console | Real retail Wii; pairing via the sync button or the controller's 1+2 button. |
| T1S MAC-PHY | **LAN8651** 10BASE-T1S Click on SPI2 — SCK 5 / MOSI 19 / MISO 21 / CS 33 / RST 27 / IRQ 32 (Kconfig-overridable), `T1S_SPI_HZ` in `main/mf_t1s.c`. |
| Status light | The board's onboard **NeoPixel** (data GPIO 0, power-enable GPIO 2, active high). Red heartbeat + blue Wii-link state, time-sliced so only one colour is lit — see `main/status_led.c`. The discrete GPIO 13 LED is unused. |

## 3. The link fauxmote emulates (verified protocol facts)

- **Transport:** Bluetooth Classic **HID** over L2CAP — PSM `0x11` (control),
  PSM `0x13` (data). Input reports (device→host) prefixed `0xa1`; output reports
  (host→device) prefixed `0xa2`.
- **Identity the Wii expects (SDP):** device name `Nintendo RVL-CNT-01`,
  VID `0x057e`, PID `0x0306`, matching Class-of-Device, and the Wiimote HID report
  descriptor.
- **Pairing PIN (legacy):** the PIN is a BD_ADDR in **reverse byte order** (raw 6
  bytes) — the *host's* for the red-SYNC bonding flow, the *Wiimote's own* for the 1+2
  temporary flow. Both are implemented: fauxmote knows its own address, so it answers
  either GAP PIN request itself (`pair` / `pair temp`). The temporary flow gives us a
  player slot without bonding, for re-slotting a controller.
- **Guitar extension:** identity bytes `00 00 A4 20 01 03` at register offset `0xfa`
  of the `0xa4` extension space. The 6-byte report (frets G/R/Y/B/O, strum up/down,
  whammy in byte 3, touch bar, analog stick, +/− buttons; **byte 0/1 bits 7-6 = 1**
  to identify a GH3 Les Paul) rides inside any extension-bearing data report — GH3
  streams mode `0x37`.
- **Guitar extension encryption (required for GH3):** GH3 writes `0x55`→`0xf0` and
  reads the ID in the clear (to tell GH3 vs GHWT), then `0xAA`→`0xf0` plus a 16-byte
  key to `0x40`-`0x4f`. After that the streamed report bytes *and* the `0x20`
  calibration must be encrypted with the standard Wii extension cipher; plaintext
  reads as garbage in-game.

References: wiibrew [`Wiimote`](https://wiibrew.org/wiki/Wiimote) and
[`Guitar Hero (Wii) Guitars`](https://wiibrew.org/wiki/Wiimote/Extension_Controllers/Guitar_Hero_(Wii)_Guitars).

## 4. Interfaces

- **Downstream (fauxmote → Wii):** Bluetooth Classic HID, as above.
- **Upstream (commands → fauxmote):** the **marvin command link** — a layered
  message protocol specified in
  [`docs/marvin-fauxmote-link.md`](../../docs/marvin-fauxmote-link.md), carried on
  T1S ethertype `0x88B7` (default) or a point-to-point UART (fallback). Senders are
  marvin and the armed fretboard detector. Slices: `GUITAR` (fret/strum mask + whammy
  + aux), `WIIMOTE` (core buttons/D-pad/stick), `ACCEL`, `POINTER`, and `LINK_CMD`
  (Bluetooth link management); fauxmote returns `STATUS` plus a `0x88B6` presence
  heartbeat. Absolute/latest-wins; a 200 ms link timeout releases all inputs. The
  link is a second front-end over the same module APIs the CLI uses.
- **Debug:** USB-C CDC serial for flashing + logs (`idf.py monitor`).

## 5. Software

- **Framework:** ESP-IDF (v6.x; developed against v6.0.1), Bluedroid stack in **Bluetooth-Classic-only** mode
  (BLE disabled).
- **BT architecture (settled in Phase 1):** `esp_hidd` is *not* usable — the Wii
  rejects its SDP record and its hardcoded HID attributes/descriptor limits don't
  fit a Wiimote. Instead fauxmote serves a **byte-exact Wiimote SDP record** built
  via Bluedroid's internal `SDP_*` database API (`wiimote_sdp.c`; needs
  `CONFIG_BT_SDP_PAD_LEN`/`ATTR_LEN` raised) and handles HID over **raw L2CAP**
  (`esp_bt_l2cap`) on PSM 0x11 (control) / 0x13 (interrupt). Legacy PIN pairing
  (SSP off; PIN = host BD_ADDR reversed). Detail + the record bytes in
  [`docs/wiimote-sdp.md`](docs/wiimote-sdp.md) and the journal.
- **Extension architecture:** extensions register with the base Wiimote through a
  small interface (`wiimote_ext.h`: register bank + report/button/reset hooks); the
  guitar lives in `guitar.c`, leaving `wiimote.c` extension-agnostic. The Wii
  extension cipher (`ext_crypto.c`) lives in the base — it captures the host's key
  handshake and encrypts outgoing extension data (streamed bytes + register reads),
  which Guitar Hero 3 requires.
- **marvin command link:** `main/mf_link.c` is the transport-neutral message layer
  (parse/dispatch, slice watchdog, `STATUS`). A Kconfig choice picks the backend that
  provides `MarvinLink_Start()` (called from `main.c`): `main/mf_t1s.c` (LAN8651 via
  the vendored OPEN Alliance TC6 library, default) or `main/marvin_link.c` (UART1,
  Feather RX=`GPIO7`/TX=`GPIO8`, 1 Mbaud, `SOF/LEN/CRC8` framing). `mf_proto.h` is
  shared byte-for-byte with marvin and the fretboard.
- **Allocation:** application code follows the project's **static-allocation**
  preference (no `malloc` in our code). Bluedroid's internal allocation is
  framework-owned and out of scope.

## 6. Milestones / success criteria

| Phase | Goal | Done when |
|---|---|---|
| **0** ✅ | Toolchain + radio bring-up | Builds + flashes to the Feather V2; the board is discoverable by name in a PC/phone Bluetooth scan. |
| **1** ✅ | Bluetooth identity / pairing (highest risk) | A real Wii authenticates and opens the HID channels (PSM 0x11 control + 0x13 interrupt) without immediately dropping. *Done via custom SDP + raw L2CAP (§5).* |
| **2** ✅ | Core Wiimote emulation | Wii shows one stable connected Wiimote; emulated buttons drive the Home-menu cursor; connection survives minutes. Includes device-initiated reconnect after idle + keep-awake (see journal). |
| **3** ✅ | Guitar extension emulation | A real Guitar Hero / Rock Band Wii title detects the guitar and registers scripted fret+strum notes. *Done: GH3 detects the guitar and frets/strum/whammy register in-game, through the extension cipher (§3). Known open: the GH3 game-launch handoff drops the link; a plain `reconnect` recovers it (see journal).* |
| **4** ✅ | Command source | Local test driver (serial console + canned patterns) exercises the emulator independently; a clean seam is left for the marvin link. *Done: protocol spec ([`docs/marvin-fauxmote-link.md`](../../docs/marvin-fauxmote-link.md)) built on both transports; marvin and the fretboard drive fauxmote over T1S. The CLI stays for manual bring-up.* |

## 7. Out of scope

- Game logic: fauxmote reflects whatever input it is sent; deciding what to play is
  marvin's and the fretboard's job.
- Powering a Wii on from standby (a Wiimote's radio can't).
