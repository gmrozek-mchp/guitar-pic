# SAM9X75 cHybrid — FT4232H EEPROM programming

The SAM9X75 cHybrid board has an onboard **FTDI FT4232H** "USB to JTAG & UART
bridge" with a **93LC46B** configuration EEPROM. This folder contains tooling to
program that EEPROM so each board reports a proper product name and a **unique
serial number**.

> JTAG debug of the SAM9X75 (which uses the same FT4232H, channel A) is
> configured separately in [`../openocd/`](../openocd/).

## FT4232H channel map (this board)

| Channel | USB interface | Function on cHybrid | EEPROM driver |
|---------|---------------|---------------------|---------------|
| A | 0 | JTAG (MPSSE) — TCK/TDI/TDO/TMS on ADBUS0–3 | D2XX |
| B | 1 | unused (bridge resistors DNP) | D2XX |
| C | 2 | DBGU console (UART, populated via R149/R151) | VCP |
| D | 3 | unused | D2XX |

(See schematic `SAM9X75 cHybrid-REV1_SCH.PDF`, sheet 14.)

## Prerequisites

```sh
# macOS (Homebrew)
brew install libftdi libusb

# Debian/Ubuntu
sudo apt install libftdi1-2 libftdi1-dev libusb-1.0-0
```

## Files

| File | Purpose |
|------|---------|
| `ft4232h-chybrid.conf` | FT4232H EEPROM image definition (`ftdi_eeprom` format) |
| `flash-eeprom.sh` | Flash one board's EEPROM with a given serial |

## Why

Boards ship with a **blank 93LC46B**, so the FT4232H falls back to internal
defaults: it enumerates as a generic "Quad RS232-HS" device with no serial
number, and the host names its ports arbitrarily. Flashing the EEPROM gives each
board:

- a product string (`SAM9X75 cHybrid`) and a **unique serial number**, so the
  host names ports predictably (e.g. `/dev/cu.usbserial-W16_2026_4130..3`) and
  OpenOCD can target a specific board with `FTDI_SERIAL` (see `../openocd/`);
- per-channel driver flags (channel A/B/D = D2XX, C = VCP).

## Procedure

> Flash **one board at a time, with only that board connected.** `ftdi_eeprom`
> selects the device by USB VID/PID, so multiple unprogrammed FT4232Hs are
> ambiguous.

1. Connect the target board (and only it) via USB.
2. Flash it with its serial number (use whatever scheme you like; we use the
   board's label, e.g. `W16-2026-413`):
   ```sh
   ./flash-eeprom.sh W16-2026-413
   ```
   Success prints `FTDI write eeprom: 0`. (A trailing `FTDI close: -1` is
   benign — the chip re-enumerates on write.)
3. Unplug/replug the board so the host reads the new descriptors.
4. Verify:
   ```sh
   # macOS
   ioreg -p IOUSB -l | grep -iE 'cHybrid|<your-serial>'
   ls /dev/cu.usbserial*

   # Linux
   lsusb -v -d 0403:6011 | grep -iE 'iProduct|iSerial'
   ```

`flash-eeprom.sh` uses `ft4232h-chybrid.conf` as the base image and overrides
only the `serial=` line, so you never have to edit the config per board.

## Platform behaviour of the per-channel driver flag

The EEPROM marks channels A/B/D as **D2XX** (no serial port) and C as **VCP**
(serial port). Whether the host honours that depends on the OS — the VCP/D2XX
flag is really a Windows-driver concept:

| OS | Result |
|----|--------|
| Windows | Honoured — only channel C gets a COM port; A/B/D are D2XX (libusb) only. |
| Linux | **Not** honoured by default — `ftdi_sio` creates `/dev/ttyUSB*` for all four channels. Suppress per-interface with a udev rule or by unbinding `ftdi_sio`. |
| macOS | **Not** honoured — `AppleUSBFTDI` creates a `/dev/cu.usbserial-*` node for all four channels. |

So only on Windows does the flag hide the unused channels. On Linux/macOS the
extra nodes are harmless — JTAG on channel A still works because libusb claims
the interface regardless of the serial node. The product string and unique
**serial number apply on every OS**, which is the portable benefit of flashing.

## Reference: editing the EEPROM image

`ft4232h-chybrid.conf` is in [libftdi `ftdi_eeprom`](https://www.intra2net.com/en/developer/libftdi/)
format. Key fields:

```ini
manufacturer="Microchip"
product="SAM9X75 cHybrid"
use_serial=true
serial="W16-2026-359"      # overridden by flash-eeprom.sh

cha_vcp=false   # channel A -> D2XX (JTAG)
chb_vcp=false   # channel B -> D2XX (unused)
chc_vcp=true    # channel C -> VCP  (DBGU console)
chd_vcp=false   # channel D -> D2XX (unused)
```

Useful one-off commands (run with a single board connected):

```sh
# Build the image without flashing (validates the config, writes the .bin)
ftdi_eeprom --device i:0x0403:0x6011 --build-eeprom ft4232h-chybrid.conf

# Flash directly from the config (what flash-eeprom.sh wraps)
ftdi_eeprom --device i:0x0403:0x6011 --flash-eeprom ft4232h-chybrid.conf

# Erase the EEPROM back to blank
ftdi_eeprom --device i:0x0403:0x6011 --erase-eeprom ft4232h-chybrid.conf
```

The 93LC46B holds 128 bytes; the image above uses ~114, so keep the strings
short if you extend it.
