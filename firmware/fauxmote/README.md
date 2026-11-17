# fauxmote

ESP32 firmware that emulates a Nintendo Wiimote (with a Guitar Hero guitar
extension) to a real Wii console, on an **Adafruit ESP32 Feather V2** (product 5400).
On the 10BASE-T1S bus it is the **controller node** (PLCA id 1): marvin and the
fretboard detector send it controller input, and it is what actually plays the Wii.

See [`SPEC.md`](SPEC.md) for what this is and [`docs/journal.md`](docs/journal.md)
for the running plan, decisions, and open questions.

## Build & flash (ESP-IDF v6.x — developed against v6.0.1)

```sh
. $IDF_PATH/export.sh          # activate the ESP-IDF environment
idf.py set-target esp32        # original ESP32 (Feather V2) — has Bluetooth Classic
idf.py build flash monitor     # build, flash over USB-C, open the serial log
```

`idf.py menuconfig` → **Fauxmote marvin link** selects the transport (T1S default,
UART fallback), the PLCA node id, and the LAN8651 pins. `sdkconfig.defaults` is only
read when `sdkconfig` does not exist, so build the other transport with
`-D SDKCONFIG=<other path>` rather than editing defaults.

The board comes up as `Nintendo RVL-CNT-01`, idle. `pair` (CLI) or `MF_CMD_PAIR`
(link) opens a window for the Wii's red SYNC button; after that, `reconnect` brings a
bonded Wii back. The USB console's `help` lists the rest.
