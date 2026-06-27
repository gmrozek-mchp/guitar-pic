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
| `sam9x7-nandflashboot-uboot-4.0.13-pmecchead.bin` | **NAND** boot via the macOS u-boot/JTAG path (`../openocd/program-nand.sh`) | as above + PMECC header prepended (`make-pmecchead.sh`) |
| `sam9x75-uboot-nandflash-flasher-2025.07.bin` | **NAND flasher**, RAM-loaded over JTAG by `../openocd/program-nand.sh` (drives `nand erase`/`nand write`; not itself flashed to the board) | u-boot `sam9x75_curiosity_pro_nandflash` (recipe below) |
| `sam9x7-dataflashboot-uboot-4.0.13.bin` | **QSPI** boot (SAM-BA `writeboot`) | `sam9x75_curiosity_prodf_qspi_uboot` |
| `sam9x7-sdcardboot-harmony-4.0.13.bin` | **microSD** boot (copied to FAT as `boot.bin`) | `sam9x75_curiosity_prosd_uboot` + `IMAGE_NAME=harmony.bin` |
| `make-pmecchead.sh` | Prepend the SAM9X7 ROM PMECC header to a NAND bootstrap (so a plain `nand write` is RomBOOT-bootable; SAM-BA's `writeboot` does this itself) | — |
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

## Marvin customization: blue LED channel → PC18, output low

at91bootstrap's only LED action is a one-shot `at91_leds_init()` in `hw_init()`
(`driver/led.c`): it drives up to three Kconfig-defined pins (R/G/B) to fixed
levels and never touches them again. The blue channel carries no status meaning,
so we **repurpose it to hold PC18 low at boot** — a generic "init this pin output
low" with no source edits. (`pio_set_gpio_output(pin, 0)` = output enable, pull-up
off, drive low.) Stock blue is PC20; our binaries move it to **PC18, value 0**,
leaving red (PC14) and green (PC21) as-is.

This is applied at build time so the at91bootstrap clone stays vanilla. After
`make <defconfig>` and **before** the final `make`, run:

```sh
sed -i '' 's|^CONFIG_LED_B_PIN=.*|CONFIG_LED_B_PIN=18|' .config      # macOS sed
sed -i '' 's|^CONFIG_LED_B_VALUE=.*|CONFIG_LED_B_VALUE=0|' .config
make CROSS_COMPILE=arm-none-eabi- oldconfig </dev/null               # see note
```

> **The `oldconfig` step is mandatory.** `CONFIG_LED_B_PIN` reaches the code via
> the generated C header `config/at91bootstrap-config/autoconf.h`, and a plain
> `make` does **not** regenerate that header after a hand-edit of `.config` — only
> a kconfig target (`oldconfig`) does. Skip it and the build silently keeps PC20.
> (Contrast `CONFIG_IMAGE_NAME` below, which the Makefile reads from `.config`
> directly as a `-D` flag, so it needs no `oldconfig`.) Verify with:
> `arm-none-eabi-objdump -d <elf> | grep -A8 at91_leds_init` — the blue call
> should load `r0, #82` (0x52 = PIOC·32+18) with `r1, #0`.

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
# blue LED channel -> PC18, output low (see "Marvin customization" above)
sed -i '' 's|^CONFIG_LED_B_PIN=.*|CONFIG_LED_B_PIN=18|' .config
sed -i '' 's|^CONFIG_LED_B_VALUE=.*|CONFIG_LED_B_VALUE=0|' .config
make CROSS_COMPILE=arm-none-eabi- oldconfig </dev/null
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
CONFIG_LED_*    # red PC14, green PC21, blue PC20 (curiosity_pro);
                # we override blue -> PC18 value 0 at build time (see above)
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

# Each build inserts the blue-LED -> PC18 override (see "Marvin customization"
# above) between `make <defconfig>` and the final `make`:
led_pc18() {   # run from the at91bootstrap dir, after `make <defconfig>`
  sed -i '' 's|^CONFIG_LED_B_PIN=.*|CONFIG_LED_B_PIN=18|' .config
  sed -i '' 's|^CONFIG_LED_B_VALUE=.*|CONFIG_LED_B_VALUE=0|' .config
  make CROSS_COMPILE=arm-none-eabi- oldconfig </dev/null
}

# NAND  (raw offset; filename unused)
make mrproper && make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_pronf_uboot_defconfig
led_pc18 && make CROSS_COMPILE=arm-none-eabi-
# QSPI  (raw offset; filename unused)
make mrproper && make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_prodf_qspi_uboot_defconfig
led_pc18 && make CROSS_COMPILE=arm-none-eabi-
# microSD — override the FAT second-stage filename to harmony.bin
make mrproper && make CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_prosd_uboot_defconfig
sed -i '' 's|^CONFIG_IMAGE_NAME=.*|CONFIG_IMAGE_NAME="harmony.bin"|' .config   # macOS sed
led_pc18 && make CROSS_COMPILE=arm-none-eabi-
# artifacts: build/binaries/sam9x7-{nandflashboot,dataflashboot}-uboot-4.0.13.bin
#            build/binaries/sam9x7-sdcardboot-harmony-4.0.13.bin
# (mrproper wipes build/, so copy each .bin out before the next build.)
```

### Flashing

- **NAND (macOS, no SAM-BA)** — `../openocd/program-nand.sh` RAM-loads the u-boot
  flasher over JTAG and `nand write`s the boot region + marvin. The boot region
  uses the PMECC-headed bootstrap (`*-pmecchead.bin`); see
  `../openocd/program-nand.md`. **Validated** booting marvin standalone from NAND.
- **NAND / QSPI (Linux/Windows)** — SAM-BA. See `nand_flash.bat` / `qspi_flash.bat`:
  bootstrap via `writeboot`, marvin via `write harmony.bin:0x40000`. (SAM-BA's
  `nandflash` applet writes the PMECC/boot header the ROM expects — the same header
  `make-pmecchead.sh` prepends for the macOS path.)
- **microSD** — `../openocd/make-sdcard.sh` on macOS. It writes the SD bootstrap as
  `boot.bin` (what the ROM looks for) and marvin as **`harmony.bin`** (the SD
  bootstrap's `CONFIG_IMAGE_NAME`).

> **QSPI over JTAG from macOS** is still open R&D — OpenOCD has no SAM9X7 QSPI
> driver and the u-boot QSPI-boot flasher path isn't wired up here (u-boot's `sf`
> could drive it; check whether QSPI boot needs an analogous header). BOSSA is
> **not** applicable (it programs Cortex-M on-chip flash over the SAM-BA protocol,
> not SAM9 external NVM, and not over JTAG).

## Build the NAND flasher u-boot (`sam9x75-uboot-nandflash-flasher-*.bin`)

This is **not** a boot stage — it's u-boot RAM-loaded over JTAG to drive NAND (see
`../openocd/program-nand.md`). Source: the linux4microchip u-boot fork,
<https://github.com/linux4microchip/u-boot-mchp> (branch `linux4microchip-2026.04`,
v2025.07 base), built with `sam9x75_curiosity_pro_nandflash_defconfig`. Any
`arm-none-eabi-` cross toolchain works. macOS specifics: use Homebrew `gmake`
(stock make 3.81 can't parse u-boot's Makefile) and pass OpenSSL via the
**environment** (command-line `HOST_EXTRACFLAGS` clobbers the bundled-dtc include
path); adjust the OpenSSL prefix for your host.

```sh
git clone -b linux4microchip-2026.04 https://github.com/linux4microchip/u-boot-mchp.git
cd u-boot-mchp
# ensure an arm-none-eabi- cross-gcc is on PATH (e.g. the Arm GNU Toolchain)
export HOST_EXTRACFLAGS="-I$(brew --prefix openssl@3)/include"   # macOS host-tool deps
export HOSTLDFLAGS="-L$(brew --prefix openssl@3)/lib"
gmake CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_pro_nandflash_defconfig
gmake CROSS_COMPILE=arm-none-eabi- -j8
cp u-boot.bin <repo>/firmware/marvin/binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin
```
