# Program marvin into NAND over JTAG — no SD card, no SAM-BA (macOS)

Writes the standalone NAND boot image (at91bootstrap + marvin) to the cHybrid's
on-board NAND from macOS, using **u-boot as a PMECC-aware flasher loaded over
JTAG**. The SAM9X75 ROM then boots NAND on its own: RomBOOT → at91bootstrap →
`harmony.bin` in DDR (the same chain as the SD path, just resident in NAND).

This replaces the SAM-BA `nand_flash.bat` flow (`../binaries/nand_flash.bat`) on
hosts where SAM-BA isn't used. It reproduces exactly what SAM-BA does:

| SAM-BA (`nand_flash.bat`)            | here (u-boot console)                       |
|--------------------------------------|---------------------------------------------|
| `erase::0x100000`                    | `nand erase 0x0 0x100000`                    |
| `writeboot:sam9x7-nandflashboot…bin` | `nand write 0x22000000 0x0 0x4000`           |
| `write:harmony.bin:0x40000`          | `nand write 0x22100000 0x40000 0x80000`      |

## Why u-boot (and not OpenOCD's NAND driver)

The boot region must be written with the **PMECC** (BCH) layout RomBOOT expects.
OpenOCD's `at91sam9` NAND driver does old-style hardware ECC, not PMECC, so it
can't produce a bootable boot region. u-boot's `atmel_nand` driver uses the same
PMECC IP, configured from the board DT (hw ECC, 8-bit strength, 512-byte step,
on-flash BBT) — the scheme RomBOOT reads — so `nand write` is RomBOOT-correct by
construction.

## Build the u-boot flasher (one-time)

A prebuilt flasher is committed at
`../binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin` (what `program-nand.sh`
uses by default) — you only need this section to regenerate it.

u-boot's source is `~/Projects/microchip/u-boot-mchp` (linux4microchip-2026.04,
v2025.07 base). macOS quirks: use Homebrew **gmake** (the stock `/usr/bin/make`
3.81 can't parse u-boot's Makefile), and pass Homebrew OpenSSL to the host-tool
build via the **environment** (passing `HOST_EXTRACFLAGS` on the command line
clobbers the bundled-dtc include path).

```sh
cd ~/Projects/microchip/u-boot-mchp
export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"
export HOST_EXTRACFLAGS="-I/opt/homebrew/opt/openssl@3/include"
export HOSTLDFLAGS="-L/opt/homebrew/opt/openssl@3/lib"

gmake CROSS_COMPILE=arm-none-eabi- sam9x75_curiosity_pro_nandflash_defconfig
gmake CROSS_COMPILE=arm-none-eabi- -j8       # -> u-boot.bin (links/enters at 0x23f00000)
cp u-boot.bin <repo>/firmware/marvin/binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin
```

## Stage + program

1. **Prerequisites (physical):**
   - Remove any bootable microSD (SD outranks NAND in the ROM boot order).
   - QSPI must hold no valid image (QSPI outranks NAND) — keep **JP4 (QSPI-CS)
     OUT**, or erase QSPI.
   - **JP3 (NAND-CS)** may be either way for *programming* (we drive NAND from
     u-boot, not RomBOOT); it must be **IN** to boot from NAND afterwards.
   - Kill any debug OpenOCD server holding the FT4232H.
   - **The board must not already be booting marvin from NAND.** This flow stages
     firmware into low DDR, which a running marvin's capture DMA can corrupt — so
     `program-nand.sh` refuses (clear error) if it catches the core running in DDR.
     To reflash a board that already boots from NAND (e.g. in an enclosure, no
     jumper access), erase first: **`./erase-nand.sh && ./program-nand.sh`**.
     `erase-nand.sh` robustly handles a booting/wedged board; after it, NAND is
     non-bootable so `program-nand.sh` catches the clean ROM-monitor path.

2. **Stage images into DDR + start u-boot** (one JTAG pass, then JTAG is done):

   ```sh
   cd firmware/marvin/openocd
   ./program-nand.sh
   # FTDI_SERIAL=W16-2026-4300 ./program-nand.sh   # target a specific board
   ```

   This runs the init-and-stop at91bootstrap to bring up DDR, loads at91bootstrap
   → `0x21100000`, `harmony.bin` → `0x21200000`, u-boot → `0x23f00000`, and jumps
   to u-boot. u-boot leaves low DDR untouched, so the buffers survive.

   > **Stop autoboot.** u-boot's default `bootcmd` reads the (empty) kernel
   > partition into `0x22000000`/`0x21000000`; if the 3 s autoboot fires before
   > you program, it overwrites staged blobs with `0xff`. The staging addresses
   > dodge its read windows, but program promptly (the console script below sends
   > a key on connect to halt autoboot).

3. **Program NAND from the DBGU console** (channel C):

   ```sh
   screen /dev/cu.usbserial-W16_2026_4302 115200      # Ctrl-A k to quit
   ```

   At the `U-Boot>` prompt:

   ```
   nand info
   nand erase 0x0 0x100000
   nand write 0x21100000 0x0      0x4000      # at91bootstrap -> boot region
   nand write 0x21200000 0x40000  0x80000     # harmony.bin   -> app
   ```

   Optional read-back verify (into an unused DDR window):

   ```
   nand read  0x24000000 0x40000  0x80000
   cmp.b 0x21200000 0x24000000 0x7e4d0
   ```

4. **Boot from NAND:** power off, set **JP3 (NAND) IN**, **JP4 (QSPI) OUT**, no SD,
   power on. marvin boots from NAND — DBGU banner, no JTAG.

## Layout reference

| NAND offset | Contents                          | DDR stage  | Size written |
|-------------|-----------------------------------|------------|--------------|
| `0x00000`   | at91bootstrap (boot region)       | `0x21100000` | `0x4000`   |
| `0x40000`   | `harmony.bin` (marvin app)        | `0x21200000` | `0x80000`  |

The boot region must carry a **PMECC header** the ROM reads (page 0, no ECC): 52
copies of a 32-bit word encoding the NAND/PMECC params, with the bootstrap code
at offset `0xD0`. Prepend it with at91bootstrap's `scripts/addpmecchead.py` —
but generate the word from the **actual** chip params (`nand info`:
4096/256/8-bit/512 → word `0xc2605007`), *not* that script's SAM9X7 defaults
(2048/64/4), which don't match this board's NAND.

The at91bootstrap reads the app from raw offset `0x40000` (`CONFIG_IMG_ADDRESS`,
`CONFIG_IMG_SIZE=0x100000`) and jumps to `0x23f00000` — where marvin links. See
`../binaries/README.md`.

## Residual risk

PMECC layout is matched by analysis (same linux4sam board DT/scheme as SAM-BA's
`sam9x75-curiosity` applet), not yet confirmed on-hardware. If RomBOOT does not
load the bootstrap (no DBGU banner after a NAND-only power-on), the SAM-BA
`writeboot` likely does something extra in the boot region's spare area; capture
`nand dump 0x0` and compare against a SAM-BA-written reference.
