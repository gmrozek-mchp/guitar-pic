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

Source: the linux4microchip u-boot fork,
<https://github.com/linux4microchip/u-boot-mchp> (branch `linux4microchip-2026.04`,
v2025.07 base), config `sam9x75_curiosity_pro_nandflash_defconfig`. Any
`arm-none-eabi-` cross toolchain works. macOS quirks: use Homebrew **gmake** (the
stock `/usr/bin/make` 3.81 can't parse u-boot's Makefile), and pass OpenSSL to the
host-tool build via the **environment** (passing `HOST_EXTRACFLAGS` on the command
line clobbers the bundled-dtc include path); adjust the OpenSSL prefix for your host.

```sh
git clone -b linux4microchip-2026.04 https://github.com/linux4microchip/u-boot-mchp.git
cd u-boot-mchp
# ensure an arm-none-eabi- cross-gcc is on PATH (e.g. the Arm GNU Toolchain)
export HOST_EXTRACFLAGS="-I$(brew --prefix openssl@3)/include"   # macOS host-tool deps
export HOSTLDFLAGS="-L$(brew --prefix openssl@3)/lib"
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

2. **Program** — one command does everything:

   ```sh
   cd firmware/marvin/openocd
   ./program-nand.sh
   # FTDI_SERIAL=W16-2026-4300 ./program-nand.sh   # target a specific board
   # CONSOLE=/dev/cu.usbserial-XXXX2 ./program-nand.sh   # override DBGU node
   ```

   It (a) **pre-flight checks the DBGU console** is free and bails with a clear
   error if it's held elsewhere (screen/minicom), *before* touching the board;
   (b) over one JTAG pass, brings up DDR via the init-and-stop at91bootstrap and
   stages at91bootstrap → `0x21100000`, `harmony.bin` → `0x21200000`, u-boot →
   `0x23f00000`, then jumps to u-boot; (c) drives the u-boot console
   (`nand_console.py`) to erase `0x0..0x100000`, `nand write` both blobs, and
   verify each region against its staged DDR copy (`cmp.b`). Exit status is
   non-zero unless both verifies pass.

   Staging addresses sit outside the windows u-boot's default `bootcmd` reads
   into (`0x21000000-0x21080000`, `0x22000000-0x22600000`), and the console driver
   waits for a *stable* prompt, so a 3 s autoboot can't clobber the staged blobs
   or eat the commands.

3. **Boot from NAND:** power off, set **JP3 (NAND) IN**, **JP4 (QSPI) OUT**, no SD,
   power on. marvin boots from NAND — DBGU banner, no JTAG.

To program by hand instead (e.g. debugging), open the console
(`screen /dev/cu.usbserial-...2 115200`) after staging and run: `nand erase 0x0
0x100000`, `nand write 0x21100000 0x0 0x4000`, `nand write 0x21200000 0x40000
0x80000`.

## Layout reference

| NAND offset | Contents                          | DDR stage    | Size written            |
|-------------|-----------------------------------|--------------|-------------------------|
| `0x00000`   | at91bootstrap (boot region)       | `0x21100000` | file size, 4 KiB-rounded (`0x4000`) |
| `0x40000`   | `harmony.bin` (marvin app)        | `0x21200000` | file size, 4 KiB-rounded (`0x7f000`) |

(`program-nand.sh` computes the write size from each file, rounded up to the 4 KiB
page; the values shown are for the current binaries.)

The boot region must carry a **PMECC header** the ROM reads (page 0, no ECC): 52
copies of a 32-bit word encoding the NAND/PMECC params, with the bootstrap code
at offset `0xD0`. Prepend it with at91bootstrap's `scripts/addpmecchead.py` —
but generate the word from the **actual** chip params (`nand info`:
4096/256/8-bit/512 → word `0xc2605007`), *not* that script's SAM9X7 defaults
(2048/64/4), which don't match this board's NAND.

The at91bootstrap reads the app from raw offset `0x40000` (`CONFIG_IMG_ADDRESS`,
`CONFIG_IMG_SIZE=0x100000`) and jumps to `0x23f00000` — where marvin links. See
`../binaries/README.md`.

## Validation & caveats

**Validated on hardware:** marvin boots standalone from NAND (RomBOOT →
at91bootstrap → harmony), and both regions read back bit-identical to the staged
images. The PMECC layout matches RomBOOT (the "something extra" SAM-BA `writeboot`
does is exactly the page-0 NAND header, which `make-pmecchead.sh` prepends).

The one parameter that is **NAND-part-specific** is the header word: it encodes
this board's `4096/256/8-bit/512` geometry (→ `0xc2605007`, eccOffset 152). If the
populated NAND ever changes:

1. read the real geometry from u-boot `nand info`,
2. regenerate the header: `make-pmecchead.sh <in> <out> <page> <oob> <ecc> <sector>`,
3. confirm the u-boot DT ECC (`nand-ecc-strength`/`-step-size`) still matches, so
   u-boot writes the ECC at the offset the header declares.

If a future board ever fails to boot from a freshly programmed NAND, capture
`nand dump 0x0` and check the page-0 header word against the regenerated value.
