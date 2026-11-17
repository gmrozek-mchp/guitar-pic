# Program QSPI NOR (splash / UI assets) over JTAG — no SD card (macOS)

Writes a blob (the raw splash framebuffer, or a UI-asset image) into the on-board
QSPI NOR (`SST26VF064B`, 8 MiB) from the host over JTAG, using **u-boot as an SF
flasher loaded over JTAG** — the same pattern as [`program-nand.md`](program-nand.md),
but with `sf` instead of `nand`, and **no PMECC/ECC/header** (NOR needs none). The
dev win over the SD path: assets reflash straight from the host, no card-swapping
each iteration.

Region offsets come from [`../default/src/flash/qspi_layout.h`](../default/src/flash/qspi_layout.h):

| Region        | QSPI offset | Size        | DDR stage    |
|---------------|-------------|-------------|--------------|
| reserved (lo) | `0x000000`  | 64 KiB      | **off-limits** (the tool refuses) |
| splash        | `0x010000`  | 4,128,768 B | `0x21100000` |
| assets        | `0x400000`  | 4,128,768 B | `0x21100000` |
| reserved (hi) | `0x7F0000`  | 48 KiB      | **off-limits** |
| settings      | `0x7FC000`  | 16 KiB      | **off-limits** |

The tool writes only inside the **host window** `[0x010000, 0x7F0000)`, and refuses
any offset that isn't 64 KiB-aligned. That window exists because this part's
Block-Erase is **non-uniform** — see the caveat below.

## Prerequisite: a u-boot with `sf` support

The committed NAND flasher (`../binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin`)
already includes the SPI-flash stack — `sf probe 0` detects the SST26 — so it is the
default `UBOOT_BIN` and no separate QSPI flasher is needed. To check a different
u-boot, stage it without programming and probe by hand:

```sh
cd firmware/marvin/openocd
UBOOT_BIN=<other-u-boot.bin> ./program-qspi.sh shell
screen /dev/cu.usbserial-XXXX2 115200      # then: sf probe 0   (exit: Ctrl-A k)
```

`SF: Detected sst26...` means it works. A u-boot without SF needs `CONFIG_CMD_SF`, the
SAM9X7 QSPI driver and `CONFIG_SPI_FLASH_SST`, linked at `0x23f00000` (build recipe:
[`program-nand.md`](program-nand.md)). `qspi_console.py` bails clearly if `sf probe`
fails, so a real run can't half-flash.

## Jumpers / physical prerequisites

- **JP4 (QSPI-CS) IN** — the part must be connected for `sf` to reach it. Our QSPI
  holds no boot image (splash/assets/settings, no boot header), so RomBOOT inspects
  it and skips it; JP4 IN does **not** cause a QSPI boot.
- **JP3 (NAND-CS) OUT** and **no bootable microSD** — so nothing boots marvin and
  the core is caught cold (DDR off) in the ROM monitor. `program-qspi.cfg` refuses
  (clear error) if `reset init` finds the core running in DDR, because staging into
  low DDR would race a running marvin's capture DMA.
- Kill any debug OpenOCD server holding the FT4232H, and close any serial monitor
  on the DBGU console.

> This jumper state (JP4 IN, JP3 OUT, no SD) should also suit `load-ram.sh` — with no
> bootable image anywhere RomBOOT falls through to the monitor — so the dev loop is
> **flash QSPI → load-ram marvin** without re-jumpering. Not yet confirmed on hardware
> that RomBOOT skips the header-less QSPI with JP4 IN.

## Program

```sh
cd firmware/marvin/openocd
./program-qspi.sh splash                      # ../data/ui/splash.raw -> QSPI 0x010000
./program-qspi.sh splash path/to/splash.raw   # explicit file
./program-qspi.sh assets icons.bin            # -> QSPI 0x400000
./program-qspi.sh assets fonts.bin 0x80000    # -> QSPI 0x400000 + 0x80000
./program-qspi.sh raw 0x123000 blob.bin       # escape hatch: arbitrary offset
# UBOOT_BIN=../binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin ./program-qspi.sh splash
```

It (a) pre-flight checks the DBGU console is free; (b) over one JTAG pass, brings up
DDR via the init-and-stop at91bootstrap, stages the blob → `0x21100000` and u-boot →
`0x23f00000`, then jumps to u-boot; (c) drives the u-boot console (`qspi_console.py`)
to `sf probe` / `sf erase` (64 KiB-rounded length) / `sf write` from DDR / verify by
reading back to scratch DDR (`0x21600000`) and `cmp.b`. Exit status is non-zero
unless the verify passes. The blob stages below u-boot's autoboot read window and the
console waits for a *stable* prompt, so autoboot can't clobber it or eat commands.

To program by hand (debugging): after staging, open the console
(`screen /dev/cu.usbserial-...2 115200`) and run (all numbers **hex**, offset and
erase length 64 KiB-aligned) `sf probe 0`, `sf erase 0x10000 0x3f0000`,
`sf write 0x21100000 0x10000 0x3e8000`, then `sf read 0x21600000 0x10000 0x3e8000`
+ `cmp.b 0x21100000 0x21600000 0x3e8000` (expect `Total of ... were the same`, no
`!=` line). `0x3e8000` = 4,096,000 = the splash size; `0x3f0000` is that rounded
up to 64 KiB, which is also the whole splash region.

## Caveats

- **The SST26's Block-Erase is non-uniform, and u-boot doesn't know it.** The array
  is four 8 KiB blocks + one 32 KiB block at the bottom, 126 × 64 KiB in the middle,
  then a mirrored 32 KiB + four 8 KiB at the top (datasheet DS20005119K §3.0). `D8h`
  erases 8, 32 or 64 KiB **depending on address**, but u-boot's spi-nor sets a flat
  64 KiB `erase_size`, so `sf erase 0x0 …` issues `D8h` at `0x0` (clearing only
  `0x0–0x1FFF`), then jumps to `0x10000` — leaving `0x2000–0xFFFF` un-erased and
  reporting `Erased: OK`; the following `sf write` then ANDs into stale bits (NOR
  only clears). **The layout keeps both non-uniform bands out of the host window**,
  and the script refuses any write that leaves it.
- **Regions above `0x7F0000` are firmware-only:** the reserved band and the settings
  ring are erased by firmware's uniform 4 KiB Sector-Erase (`20h`), which has no
  address dependence. (The firmware `qspi` smoke test's scratch lands in the
  settings region; the settings CRC fallback makes that non-fatal.)
- **A full-chip escape hatch exists** if the low blocks ever need clearing from
  u-boot: `sf erase 0 0x800000` matches the device size, which takes u-boot's
  whole-chip path (`C7h`) and is uniform. It wipes splash, assets *and* settings, so
  it's a recovery tool, not part of the normal flow.
- **Splash format** is raw RGBA8888 1280×800 = 4,096,000 bytes — exactly what
  `ui/screens/splash/screen_splash.c` reads in one shot from `QSPI_SPLASH_OFFSET`
  (it does no size check; a short blob leaves stale flash in the tail). Build it with
  [`../data/ui/make-splash.sh`](../data/ui/make-splash.sh). Nothing in the firmware
  reads the assets region yet.
- **u-boot parses command args as hex** — the script passes all lengths/offsets in
  hex. `sf write` writes the file's exact byte count; `sf erase` rounds up to u-boot's
  **64 KiB** erase block for this part (a 4 KiB-aligned length gives `ERROR -22`). The
  64 KiB-rounded erase stays within the region (splash/assets are MiB-sized and
  64 KiB-aligned). The verify covers the written bytes.
- 100 MHz QSCK reads are integrity-clean (firmware `qspi verify`); u-boot's `sf`
  runs the QSPI at its own (lower) default rate, so programming is reliable
  regardless of the firmware clock setting.
