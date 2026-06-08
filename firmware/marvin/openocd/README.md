# SAM9X75 cHybrid — JTAG debug with OpenOCD

Debug the SAM9X75 (ARM926EJ-S) over JTAG using the board's onboard **FTDI
FT4232H**, whose **channel A** is wired to the JTAG bus (TCK/TDI/TDO/TMS on
ADBUS0–3). No special FTDI driver is required.

> The FT4232H also has a configuration EEPROM; tooling to program board
> identity/serials lives in [`../ftdi/`](../ftdi/). It's independent of this
> debug config, but the serial numbers it writes are what `FTDI_SERIAL` (below)
> selects on.

## Prerequisites

```sh
# macOS (Homebrew)
brew install openocd libftdi libusb

# Debian/Ubuntu
sudo apt install openocd libftdi1-2 libusb-1.0-0
```

On macOS the built-in `AppleUSBFTDI` driver creates `/dev/cu.usbserial-*` nodes
for the FT4232H channels — that is normal and does **not** interfere; libusb
still claims channel A for JTAG. Just don't open the channel-A serial node in a
terminal while debugging.

## Files

| File | Purpose |
|------|---------|
| `sam9x75-chybrid.cfg` | OpenOCD config: FTDI channel A + ARM926EJ-S target |

## Usage

```sh
openocd -f sam9x75-chybrid.cfg
```

Expected output (gdb server on port 3333):

```
Info : JTAG tap: sam9x75.cpu tap/device found: 0x0792603f (mfg: 0x01f (Atmel), part: 0x7926, ver: 0x0)
Info : Embedded ICE version 6
Info : sam9x75.cpu: hardware has 2 breakpoint/watchpoint units
Info : starting gdb server for sam9x75.cpu on 3333
Info : Listening on port 3333 for gdb connections
```

Attach a debugger:

```sh
arm-none-eabi-gdb -ex 'target remote :3333'
```

`reset`, `reset halt`, and `reset init` work via the nRST line (AD5). For
example, `reset halt` resets the SoC and stops in boot ROM (`pc ≈ 0x44`).

A `Warn : libusb_detach_kernel_driver() failed with LIBUSB_ERROR_ACCESS` line on
macOS is harmless — OpenOCD claims the interface anyway.

## Selecting a specific board

When several boards are connected at once, pick one by its FT4232H serial
(requires the EEPROM to have been flashed — see [`../ftdi/`](../ftdi/)):

```sh
openocd -c "set FTDI_SERIAL W16-2026-413" -f sam9x75-chybrid.cfg
```

Leave `FTDI_SERIAL` unset to use the first FT4232H found.

## Channel A reset / clock pinout

| Signal | FT4232H pin | Notes |
|--------|-------------|-------|
| nSRST  | AD5 | open-drain system reset (`reset_config srst_only srst_open_drain`) |
| RTCK   | AD7 | return clock; enables adaptive clocking via `adapter speed 0` |
| nTRST  | — | not wired (AD4/AD6 are N/C); TAP reset uses TMS |

## Scope / limitations

This config supports **attach, halt, resume, memory/register access, reset**
(`reset` / `reset halt` / `reset init` via nSRST), and **adaptive clocking**
(`adapter speed 0`, using RTCK). It does **not** yet initialise external DDR or
define flash banks, so loading code to RAM and programming NAND/QSPI/SD over
JTAG are not set up — use SAM-BA or MPLAB for production programming, or extend
this config with a `reset-init` event handler that brings up the DDR controller.
