# marvin boot binaries — what they are and how to (re)build them

at91bootstrap is marvin's first-stage boot: RomBOOT loads it, it brings up the
266 MHz clocks + the in-package DDR3L (Winbond `W632GU6NB12I`), then either hands
off to the application or — for the JTAG dev loop — stops and waits.

| File | Purpose | Provenance |
|------|---------|------------|
| `sam9x7-boot-none-4.0.13.elf` | **JTAG load-to-RAM** bootstrap (init-and-stop). Used by `../openocd/load-ram.sh`. | Built from source (recipe below) |
| `boot.bin` | SD / QSPI first-stage bootstrap (used for microSD boot and `qspi_flash.bat`) | Vendor-supplied, *unverified* |
| `at91bootstrap.bin` | NAND first-stage bootstrap (used by `nand_flash.bat`) | Vendor-supplied, *unverified* |
| `nand_flash.bat`, `qspi_flash.bat` | SAM-BA flashing scripts (Linux/Windows host) | Vendor-supplied |

> Only `sam9x7-boot-none-4.0.13.elf` is built from known source today. Rebuilding
> the production media bootstraps (`boot.bin` / `at91bootstrap.bin`) from the same
> source is the Phase 2 task — see the recipe below and the journal.

## Board ↔ defconfig

The **SAM9X75 Curiosity Hybrid (cHybrid)** maps to the at91bootstrap
**`sam9x75_curiosity_pro_*`** defconfigs (confirmed by the on-board red LED on
PIOC 14 — `curiosity_pro` uses PC14, plain `curiosity` uses PC19; DDR is identical
across both). All variants here use the in-package `W632GU6NB12I` DDR3L.

## Prerequisites

- **at91bootstrap source** (this was built from `v4.0.13`):
  ```sh
  git clone https://github.com/linux4sam/at91bootstrap
  # in-use clone for this project: ~/Projects/microchip/at91bootstrap
  ```
- **ARM bare-metal toolchain** — Arm GNU Toolchain (this build used 15.2.Rel1).
  It is installed but not on `PATH`; add its `bin` dir:
  ```sh
  export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"
  ```
  (Any `arm-none-eabi-` cross-gcc works; XC32 is *not* used for at91bootstrap.)

## Build the JTAG load-to-RAM bootstrap (`sam9x7-boot-none-*.elf`)

This is the `bkptnone` defconfig — `CONFIG_INIT_AND_STOP=y`: it inits clocks + DDR
and then **loops**, leaving MMU/caches off, so a debugger can load a binary into
the freshly-initialized DDR. (It does *not* issue a `bkpt`, so it is harmless with
no debugger attached.)

```sh
cd ~/Projects/microchip/at91bootstrap
export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"

make mrproper
make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_pro_bkptnone_defconfig
make CROSS_COMPILE=arm-none-eabi-
```

Output lands in `build/binaries/`:

```
build/binaries/sam9x7-boot-none-4.0.13.elf   <- copy this here
build/binaries/sam9x7-boot-none-4.0.13.bin   (raw image; not needed for JTAG load)
build/binaries/sam9x7-boot-none-4.0.13.map
```

Copy the ELF into this folder (the `.elf` is what `load_image` consumes; it
carries the entry point, `0x300000`):

```sh
cp build/binaries/sam9x7-boot-none-4.0.13.elf \
   <repo>/firmware/marvin/binaries/
```

If the version string in the filename changes (newer at91bootstrap), update the
`BOOTSTRAP_ELF` default in `../openocd/load-ram.sh` to match.

The `..._bkptnone_defconfig` contents (for reference):

```
CONFIG_INIT_AND_STOP=y
CONFIG_SAM9X7=y
CONFIG_DDR_SET_BY_DEVICE=y
CONFIG_DDR_W632GU6NB12I=y
CONFIG_BOARD_QUIRK_SAM9X75_CURIOSITY=y
CONFIG_LED_*    # red on PC14 (curiosity_pro)
```

## Build a production bootstrap (Phase 2 — standalone boot)

For a board that boots on its own (no JTAG), build the bootstrap for the target
medium and flash marvin (`out/harmony.bin`) as the second stage. These configs
are named `*_uboot_*` by convention but are **not** u-boot-specific — they just
load the next-stage binary from flash into DRAM (`0x23f00000`, where marvin runs)
and jump. No actual u-boot / Linux is involved.

```sh
# NAND
make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_pronf_uboot_defconfig && make
# QSPI
make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_prodf_qspi_uboot_defconfig && make
# microSD
make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_prosd_uboot_defconfig && make
```

Then flash with SAM-BA on a Linux/Windows host (macOS has no SAM-BA), substituting
the freshly built bootstrap and marvin's `harmony.bin` — see `nand_flash.bat` /
`qspi_flash.bat` (bootstrap via `writeboot`, app via `write harmony.bin:0x40000`).
For microSD, `../openocd/make-sdcard.sh` writes `boot.bin` + `harmony.bin` to a
FAT card on macOS.
