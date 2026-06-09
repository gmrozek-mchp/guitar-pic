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
| `load-ram.cfg` | OpenOCD proc that boots marvin into DDR via at91bootstrap |
| `load-ram.sh` | Driver script for `load-ram.cfg` (resolves ELF paths + addresses) |

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

## Load marvin into RAM over JTAG (no SD card)

`load-ram.sh` boots marvin entirely over JTAG — no microSD required. It reuses
the validated **at91bootstrap** (`../binaries/at91bootstrap.elf`) to bring up the
266 MHz clocks and initialize the in-package DDR3L, then loads marvin's ELF into
DDR and jumps to it. (Reusing at91bootstrap avoids re-implementing the
SAM9X75D2G DDR3L init by hand — see `load-ram.cfg` and the journal.)

```sh
./load-ram.sh                            # ../binaries/at91bootstrap.elf + ../out/marvin/default.elf
FTDI_SERIAL=W16-2026-413 ./load-ram.sh   # target a specific board
```

**Prerequisites — the flow depends on these:**

1. **Both memory CS jumpers OUT** (JP3 = NAND, JP4 = QSPI). With no boot media,
   RomBOOT drops into the SAM-BA monitor: a clean state, DDR uninitialized.
2. **Power-cycle before each run.** SAM9X75D2G DDR3L init only completes cleanly
   on a *fresh* MPDDRC — it is not re-runnable on an already-initialized
   controller (re-running corrupts the trained DDR). So: one load per power-cycle.
3. The FT4232H is USB-bus-powered, so a power-cycle that drops USB re-enumerates
   the adapter — restart any OpenOCD session afterward.

Mechanism (also the by-hand recipe): `reset halt` → `adapter speed 0` (RTCK
adaptive clocking — **required**; at fixed TCK, OpenOCD loses JTAG sync when
at91bootstrap switches the master clock) → load at91bootstrap → break at the
return of `hw_init()` (clocks + DDR up, watchdog disabled, MMU/caches still off)
→ load marvin → resume at `0x23f00000`. An I-cache invalidate
(`arm mcr 15 0 7 5 0 0`) follows each `load_image`.

**Headless note:** with no display/maXTouch panel connected, marvin's maXTouch
driver init fails gracefully (driver → ERROR) and the rest of the system runs.
This relies on the bounded-retry fix in `drv_maxtouch.c` (journal re-apply patch
#10) — without it, an absent panel hangs the system at the FreeRTOS malloc-fail
hook.

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

This supports **attach, halt, resume, memory/register access, reset**
(`reset` / `reset halt` / `reset init` via nSRST), **adaptive clocking**
(`adapter speed 0`, using RTCK), and **loading + running marvin from DDR over
JTAG** (`load-ram.sh`, above — validated booting marvin headless).

It does **not** program on-board flash: writing a bootable image to NAND/QSPI so
the board boots standalone (no JTAG) is a separate task, best done with **SAM-BA**
(see `../binaries/*.bat`, on a Linux/Windows host) or MPLAB. Current per-iteration
limitation: each RAM load needs a power-cycle (fresh DDR); a no-reset reload that
keeps clocks/DDR live (for faster dev iteration) is a planned improvement.
