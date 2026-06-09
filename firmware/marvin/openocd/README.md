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
| `sam9x75-chybrid.cfg` | OpenOCD config: FTDI channel A + ARM926EJ-S target + reset/WDT |
| `load-ram.cfg` | OpenOCD proc that loads marvin into DDR via at91bootstrap (`marvin_load_ram`) |
| `load-ram.sh` | Load + run marvin over JTAG — the dev loop, repeatable with no power-cycle |
| `make-sdcard.sh` | Prepare a bootable microSD on macOS (standalone boot, no JTAG) |

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

`reset`, `reset halt`, and `reset init` reset the SoC over the nRST line (AD5) —
including a *running* marvin — and `reset init` additionally disables the watchdog
and MMU/caches. This relies on driving nSRST push-pull (`-data`/`-oe`) with a
500 ms pulse and `srst_pulls_trst`; a too-short pulse or `-oe`-only definition
does **not** actually reset the chip. (Config follows the Microchip class-material
example for this board.)

A `Warn : libusb_detach_kernel_driver() failed with LIBUSB_ERROR_ACCESS` line on
macOS is harmless — OpenOCD claims the interface anyway.

## Load marvin into RAM over JTAG (no SD card)

`load-ram.sh` boots marvin entirely over JTAG — no microSD required. It reuses a
purpose-built **"init-and-stop" at91bootstrap**
(`../binaries/sam9x7-boot-none-4.0.13.elf`, built from the
`sam9x75_curiosity_pro_bkptnone_defconfig` — `CONFIG_INIT_AND_STOP`) to bring up
the 266 MHz clocks and initialize the in-package DDR3L, then loads marvin's ELF
into DDR and jumps to it. (Reusing at91bootstrap avoids re-implementing the
SAM9X75D2G DDR3L init by hand — see `load-ram.cfg` and the journal.) To rebuild
the bootstrap from source, see [`../binaries/README.md`](../binaries/README.md).

```sh
./load-ram.sh                            # ../binaries/sam9x7-boot-none-4.0.13.elf + ../out/marvin/default.elf
FTDI_SERIAL=W16-2026-413 ./load-ram.sh   # target a specific board
```

It is **repeatable with no physical power-cycle** — edit, rebuild, re-run:

```sh
./load-ram.sh        # reset init → at91bootstrap (DDR) → load + run marvin
# ... edit, rebuild marvin ...
./load-ram.sh        # again, from the running marvin — no power-cycle
```

**Prerequisites:**

1. **Both memory CS jumpers OUT** (JP3 = NAND, JP4 = QSPI). With no boot media,
   RomBOOT drops into the SAM-BA monitor when no marvin is running.
2. Nothing else may hold the FT4232H — kill any debug OpenOCD server first.

Mechanism (`marvin_load_ram`, also the by-hand recipe):

`reset init` (resets the SoC from any state via the 500 ms nSRST pulse, then
disables the watchdog + MMU/caches) → `adapter speed 0` (RTCK adaptive clocking —
**required**; at fixed TCK, OpenOCD loses JTAG sync when at91bootstrap switches
the master clock) → load the init-and-stop at91bootstrap → resume → brief wait →
halt (clocks + DDR now up, MMU/caches off; the bootstrap just loops) → load
marvin → resume at `0x23f00000`. An I-cache invalidate (`arm mcr 15 0 7 5 0 0`)
follows each `load_image`. The reset re-initializes the MPDDRC, so the bootstrap
brings DDR3L up fresh each run — that is what makes the loop repeatable without a
power-cycle. (An `INIT_AND_STOP` bootstrap halts itself after DDR init, so no
breakpoint or disassembly is needed.)

**Headless note:** with no display/maXTouch panel connected, marvin's maXTouch
driver init fails gracefully (driver → ERROR) and the rest of the system runs.
This relies on the bounded-retry fix in `drv_maxtouch.c` (journal re-apply patch
#10) — without it, an absent panel hangs the system at the FreeRTOS malloc-fail
hook.

## Debugging in VS Code (or gdb CLI)

`arm-none-eabi-gdb` (Arm GNU Toolchain) attaches to OpenOCD's gdb server on
`:3333`. marvin is **loaded** by `load-ram.sh`; the debugger only **attaches** (it
does not reflash), so the loop is: *load → attach → debug*.

**VS Code** — `.vscode/launch.json` + `.vscode/tasks.json` (repo root) provide:

- a background task `openocd: sam9x75 gdb server` that starts the server, and
- two attach configs (pick whichever extension you have installed):
  - **marvin: attach over JTAG (cppdbg)** — Microsoft C/C++ (`ms-vscode.cpptools`)
  - **marvin: attach over JTAG (Native Debug)** — `webfreak.debug`

Both auto-start the server (`preLaunchTask`), attach, load symbols from
`out/marvin/default.elf`, and stop the server on exit (`postDebugTask`). Set a
breakpoint, run the config, and step/inspect. (cortex-debug is Cortex-M-centric
and is intentionally not used for this ARM926 target.)

**gdb CLI** equivalent:

```sh
openocd -f sam9x75-chybrid.cfg &                       # gdb server on :3333
arm-none-eabi-gdb firmware/marvin/out/marvin/default.elf \
    -ex 'set architecture arm' -ex 'target remote :3333'
```

## Bootable microSD (standalone, no JTAG) — `make-sdcard.sh`

For a board that boots on its own (no debugger), `make-sdcard.sh` prepares a card
on macOS: it FAT-formats the card and writes the SD bootstrap
(`../binaries/sam9x7-sdcardboot-harmony-4.0.13.bin`) as **`boot.bin`** (what the
ROM looks for) and marvin (`../out/harmony.bin`) as **`harmony.bin`** (the
bootstrap's `CONFIG_IMAGE_NAME`) — the layout the SAM9X75 ROM SD-boot path expects.

```sh
diskutil list                  # find the card, e.g. /dev/disk4
./make-sdcard.sh /dev/disk4    # ERASES the card (asks you to confirm the id)
```

It refuses a fixed internal disk and requires you to retype the disk identifier
before erasing. Then set the board boot jumpers for SD, insert the card, and
power-cycle. SD is the highest-priority boot source (see `../binaries/README.md`),
so a valid card boots ahead of NAND/QSPI. Not yet exercised on a real card.

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
| nSRST  | AD5 | system reset, push-pull `-data 0x20 -oe 0x20`; 500 ms pulse; `srst_pulls_trst` |
| RTCK   | AD7 | return clock; enables adaptive clocking via `adapter speed 0` |
| nTRST  | — | AD4 is N/C on this board; not defined (TAP re-validates via `srst_pulls_trst`) |

## Scope / limitations

This supports **attach, halt, resume, memory/register access, reset**
(`reset` / `reset halt` / `reset init` — resets a running marvin; `reset init`
also disables the watchdog + MMU/caches), **adaptive clocking** (`adapter speed 0`,
using RTCK), **loading + running marvin from DDR over JTAG, repeatable with no
power-cycle** (`load-ram.sh` — validated booting marvin headless and reloading
from a running marvin), **source-level debug** (gdb/VS Code on `:3333`), and a
**macOS microSD prep** for standalone boot (`make-sdcard.sh`).

It does **not** program on-board NAND/QSPI flash from macOS: writing a bootable
image to NAND/QSPI so the board boots standalone is a separate task, best done
with **SAM-BA** (see `../binaries/*.bat`, on a Linux/Windows host) or MPLAB. A
macOS-native NAND path via OpenOCD's `at91sam9` driver is planned R&D (QSPI has no
OpenOCD driver). See the journal for status.
