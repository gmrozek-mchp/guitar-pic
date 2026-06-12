# fauxmote

ESP32 firmware that emulates a Nintendo Wiimote (with a Guitar Hero guitar
extension) to a real Wii console, on an **Adafruit ESP32 Feather V2** (product 5400).

See [`SPEC.md`](SPEC.md) for what this is and [`docs/journal.md`](docs/journal.md)
for the running plan, decisions, and open questions.

## Build & flash (ESP-IDF v6.x — developed against v6.0.1)

```sh
. $IDF_PATH/export.sh          # activate the ESP-IDF environment
idf.py set-target esp32        # original ESP32 (Feather V2) — has Bluetooth Classic
idf.py build flash monitor     # build, flash over USB-C, open the serial log
```

Phase 0 brings the Bluetooth-Classic radio up and advertises the board as
`Nintendo RVL-CNT-01`. Verify it appears by name in a PC/phone Bluetooth scan.
