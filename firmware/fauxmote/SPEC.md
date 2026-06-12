# fauxmote — Specification

> What fauxmote *is* (purpose, hardware, interfaces, success criteria, milestones).
> The running diary of decisions and progress lives in [`docs/journal.md`](docs/journal.md) — read it alongside this on any non-trivial task.

## 1. Purpose

fauxmote is an **ESP32 firmware that emulates a Nintendo Wiimote with an attached
Guitar Hero guitar extension**, speaking Bluetooth straight to a real Wii console.
It is a **parallel proof-of-concept** alternative to the existing actuation path:
today the [fretboard](../fretboard/SPEC.md) MCU physically presses buttons on a
*real* Wii guitar controller via open-drain GPIO. fauxmote instead *becomes* the
controller — eventually driven by [marvin](../marvin/docs/spec.md)'s high-level
fret/strum commands, removing the physical actuator + real controller from the loop.

The fretboard GPIO-press path stays authoritative until fauxmote is proven; nothing
in marvin/fretboard/edge-ai changes as part of this subproject.

## 2. Hardware

| Component | Detail |
|---|---|
| Board | **Adafruit ESP32 Feather V2** (product 5400) — *original* dual-core ESP32 (Tensilica LX6), 8 MB flash, 2 MB PSRAM, USB-C. |
| Radio | ESP32 has a **Bluetooth Classic (BR/EDR)** radio — required, because the Wiimote link is Classic HID. (ESP32-S3/C3 are BLE-only and would *not* work.) |
| Wii console | Real retail Wii; pairing via the sync button or the controller's 1+2 button. |

## 3. The link fauxmote emulates (verified protocol facts)

- **Transport:** Bluetooth Classic **HID** over L2CAP — PSM `0x11` (control),
  PSM `0x13` (data). Input reports (device→host) prefixed `0xa1`; output reports
  (host→device) prefixed `0xa2`.
- **Identity the Wii expects (SDP):** device name `Nintendo RVL-CNT-01`,
  VID `0x057e`, PID `0x0306`, matching Class-of-Device, and the Wiimote HID report
  descriptor.
- **Pairing PIN (legacy):** the PIN is a BD_ADDR in **reverse byte order** (raw 6
  bytes). The 1+2 temporary-pair flow uses the *Wiimote's own* address — which
  fauxmote knows — so fauxmote can answer the GAP PIN request itself.
- **Guitar extension:** identity bytes `00 00 A4 20 01 03` at register
  `0x(4)a400fa`; init = write `0x55`→`0xf0`, `0x00`→`0xfb` (disables encryption).
  6-byte report layout (frets G/R/Y/B/O, strum up/down, whammy in byte 3 bits 3-0,
  touch bar, analog stick, +/− buttons), carried inside a Wiimote data report that
  includes extension bytes (e.g. report `0x34`).

References: wiibrew [`Wiimote`](https://wiibrew.org/wiki/Wiimote) and
[`Guitar Hero (Wii) Guitars`](https://wiibrew.org/wiki/Wiimote/Extension_Controllers/Guitar_Hero_(Wii)_Guitars).

## 4. Interfaces

- **Downstream (fauxmote → Wii):** Bluetooth Classic HID, as above.
- **Upstream (commands → fauxmote):** **deferred.** Early phases use a local test
  command source (serial console + canned chord/strum patterns). The eventual marvin
  link is undecided; the likely candidate is mirroring the fretboard 1-byte fret/strum
  bitmask over UART (see [fretboard SPEC](../fretboard/SPEC.md) button-output section).
- **Debug:** USB-C CDC serial for flashing + logs (`idf.py monitor`).

## 5. Software

- **Framework:** ESP-IDF (v6.x; developed against v6.0.1), Bluedroid stack in **Bluetooth-Classic-only** mode
  (BLE disabled). Bluedroid's BT-Classic HID-device API (`esp_hidd_api.h`) is the
  starting point; raw L2CAP on PSM 0x11/0x13 with a hand-built SDP record is the
  fallback if the stack's default SDP is too rigid for the Wii.
- **Allocation:** application code follows the project's **static-allocation**
  preference (no `malloc` in our code). Bluedroid's internal allocation is
  framework-owned and out of scope.

## 6. Milestones / success criteria

| Phase | Goal | Done when |
|---|---|---|
| **0** | Toolchain + radio bring-up | Builds + flashes to the Feather V2; the board is discoverable by name in a PC/phone Bluetooth scan. |
| **1** | Bluetooth identity / pairing (highest risk) | A real Wii authenticates and opens the HID data channel on PSM 0x13 without immediately dropping. |
| **2** | Core Wiimote emulation | Wii shows one stable connected Wiimote; emulated buttons drive the Home-menu cursor; connection survives minutes. |
| **3** | Guitar extension emulation | A real Guitar Hero / Rock Band Wii title detects the guitar and registers scripted fret+strum notes. |
| **4** | Command source | Local test driver (serial console + canned patterns) exercises the emulator independently; a clean seam is left for the future marvin link. |

## 7. Out of scope

- Retiring or modifying the fretboard GPIO-press path.
- The concrete marvin↔fauxmote wiring/protocol (deferred).
- Any edge-ai retargeting toward fauxmote.
