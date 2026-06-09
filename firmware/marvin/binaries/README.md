# marvin boot binaries — what they are and how to (re)build them

at91bootstrap is marvin's first-stage boot: RomBOOT loads it, it brings up the
266 MHz clocks + the in-package DDR3L (Winbond `W632GU6NB12I`), then either hands
off to the application or — for the JTAG dev loop — stops and waits.

All bootstraps here are built from at91bootstrap **v4.0.13** source (recipes
below), so their provenance is known.

| File | Medium / use | Built from defconfig |
|------|--------------|----------------------|
| `sam9x7-boot-none-4.0.13.elf` | **JTAG load-to-RAM** (init-and-stop). Used by `../openocd/load-ram.sh`. | `sam9x75_curiosity_pro_bkptnone` |
| `sam9x7-nandflashboot-uboot-4.0.13.bin` | **NAND** boot (SAM-BA `writeboot`) | `sam9x75_curiosity_pronf_uboot` |
| `sam9x7-dataflashboot-uboot-4.0.13.bin` | **QSPI** boot (SAM-BA `writeboot`) | `sam9x75_curiosity_prodf_qspi_uboot` |
| `sam9x7-sdcardboot-harmony-4.0.13.bin` | **microSD** boot (copied to FAT as `boot.bin`) | `sam9x75_curiosity_prosd_uboot` + `IMAGE_NAME=harmony.bin` |
| `nand_flash.bat`, `qspi_flash.bat` | SAM-BA flashing scripts (Linux/Windows host) | — |

The media bootstraps all jump to **`0x23F00000`** (`CONFIG_JUMP_ADDR`) — exactly
where marvin links and runs — so marvin (`out/harmony.bin`) is the second stage.

## Boot order (SAM9X75 ROM)

The ROM tries NVM in a fixed sequential fall-through (first valid image wins);
order is customizable only via the OTP Boot Configuration Packet:

> **SD/eMMC (SDMMC0) → SD/eMMC (SDMMC1) → QSPI → NAND (SMC) → SPI (FLEXCOM5)**

So **SD outranks NAND, and QSPI sits between them.** Useful consequences:
- NAND = resident firmware; **SD = override/recovery** (a valid card boots first,
  no jumper change; remove it to fall back to NAND).
- For NAND to boot, **QSPI must hold no valid image** (erase it or keep JP4/QSPI-CS
  out) — otherwise QSPI boots before NAND.
- For the JTAG dev loop we pull JP3 (NAND) + JP4 (QSPI) and use no card, so the
  ROM falls all the way through to the SAM-BA monitor.

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

## Build the production (standalone-boot) bootstraps

These configs are named `*_uboot_*` by convention but are **not** u-boot-specific
— they load the next-stage binary from flash into DRAM (`0x23F00000`) and jump.
No actual u-boot / Linux is involved; marvin (`harmony.bin`) is the second stage.

- **NAND / QSPI** load the app from a **raw flash offset** (`CONFIG_IMG_ADDRESS`,
  `0x40000`) — the filename is irrelevant on-device, so these keep their stock
  `-uboot-` build-name suffix.
- **SD** loads the app from **FAT by filename** (`CONFIG_IMAGE_NAME`). The stock
  default is `u-boot.bin`; we override it to **`harmony.bin`** (the Harmony MPU
  convention, and consistent with the NAND/QSPI host files). at91bootstrap then
  auto-names the output `...-sdcardboot-harmony-...`.

```sh
cd ~/Projects/microchip/at91bootstrap
export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"

# NAND  (raw offset; filename unused)
make mrproper && make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_pronf_uboot_defconfig     && make CROSS_COMPILE=arm-none-eabi-
# QSPI  (raw offset; filename unused)
make mrproper && make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_prodf_qspi_uboot_defconfig && make CROSS_COMPILE=arm-none-eabi-
# microSD — override the FAT second-stage filename to harmony.bin
make mrproper && make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_prosd_uboot_defconfig
sed -i '' 's|^CONFIG_IMAGE_NAME=.*|CONFIG_IMAGE_NAME="harmony.bin"|' .config   # macOS sed
make CROSS_COMPILE=arm-none-eabi-
# artifacts: build/binaries/sam9x7-{nandflashboot,dataflashboot}-uboot-4.0.13.bin
#            build/binaries/sam9x7-sdcardboot-harmony-4.0.13.bin
# (mrproper wipes build/, so copy each .bin out before the next build.)
```

### Flashing

- **NAND / QSPI** — SAM-BA on a **Linux/Windows** host (macOS has no SAM-BA). See
  `nand_flash.bat` / `qspi_flash.bat`: bootstrap via `writeboot`, marvin via
  `write harmony.bin:0x40000`. (SAM-BA's `nandflash` applet writes the PMECC/boot
  header the ROM expects.)
- **microSD** — `../openocd/make-sdcard.sh` on macOS. It writes the SD bootstrap as
  `boot.bin` (what the ROM looks for) and marvin as **`harmony.bin`** (the SD
  bootstrap's `CONFIG_IMAGE_NAME`).

> macOS-native NAND/QSPI flashing over JTAG (no Linux/Windows) is open R&D — e.g.
> OpenOCD's `at91sam9` NAND driver, or loading u-boot into RAM via `load-ram` and
> flashing from its console. BOSSA is **not** applicable (it programs Cortex-M
> on-chip flash over the SAM-BA protocol, not SAM9 external NVM, and not over JTAG).
