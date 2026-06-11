#!/usr/bin/env bash
# Program marvin into on-board NAND from macOS over JTAG — no SD card, no SAM-BA.
# Stages at91bootstrap + harmony + u-boot into DDR over JTAG, starts u-boot, then
# erases + writes + verifies NAND from the DBGU console (all automated). macOS/
# OpenOCD only. See program-nand.md.
#
# PREREQUISITES:
#   * No higher-priority boot medium catching the core first: remove any bootable
#     microSD, and QSPI must hold no valid image (JP4/QSPI-CS OUT). JP3 (NAND) may
#     be IN or OUT for *programming* (we drive NAND from u-boot, not RomBOOT) but
#     must be IN to boot from NAND afterwards.
#   * The board must NOT already be booting marvin from NAND (staging into low DDR
#     would race a running marvin's capture DMA). program-nand.cfg refuses with a
#     clear error in that case — erase first: ./erase-nand.sh && ./program-nand.sh.
#   * Nothing else holding the FT4232H (kill any debug OpenOCD server first) or the
#     DBGU console serial node.
#
# Usage:   ./program-nand.sh
# Env overrides:
#   BOOTSTRAP_ELF  init-and-stop at91bootstrap ELF (brings up DDR over JTAG)
#                  (default: ../binaries/sam9x7-boot-none-4.0.13.elf)
#   UBOOT_BIN      u-boot raw binary, the flasher (links/enters at 0x23f00000)
#                  (default: ../binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin)
#   NANDBOOT_BIN   at91bootstrap written to NAND offset 0x0 (the boot region) —
#                  MUST carry the SAM9X7 ROM PMECC header (the *-pmecchead.bin;
#                  regenerate with ../binaries/make-pmecchead.sh). The plain
#                  bootstrap has no header and RomBOOT will not load it.
#                  (default: ../binaries/sam9x7-nandflashboot-uboot-4.0.13-pmecchead.bin)
#   HARMONY_BIN    marvin app written to NAND offset 0x40000
#                  (default: ../out/harmony.bin)
#   FTDI_SERIAL    target a specific board by its FT4232H serial
#   CONSOLE        DBGU console node (FT4232H channel C). Auto-detected as the 3rd
#                  /dev/cu.usbserial-* if unset; set explicitly with >1 board.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

BOOTSTRAP_ELF="${BOOTSTRAP_ELF:-$root/binaries/sam9x7-boot-none-4.0.13.elf}"
UBOOT_BIN="${UBOOT_BIN:-$root/binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin}"
NANDBOOT_BIN="${NANDBOOT_BIN:-$root/binaries/sam9x7-nandflashboot-uboot-4.0.13-pmecchead.bin}"
HARMONY_BIN="${HARMONY_BIN:-$root/out/harmony.bin}"
FTDI_SERIAL="${FTDI_SERIAL:-}"
CONSOLE="${CONSOLE:-$(ls /dev/cu.usbserial-* 2>/dev/null | sort | sed -n '3p')}"

# DDR staging addresses (must match program-nand.cfg) + NAND offsets.
BOOT_ADDR=0x21100000; BOOT_OFF=0x0
APP_ADDR=0x21200000;  APP_OFF=0x40000

xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"

for f in "$BOOTSTRAP_ELF" "$UBOOT_BIN" "$NANDBOOT_BIN" "$HARMONY_BIN"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
[ -n "$CONSOLE" ] || { echo "no DBGU console found; set CONSOLE=/dev/cu.usbserial-... (channel C)" >&2; exit 1; }

# nand-write sizes: file size rounded up to the 4 KiB page.
roundup() { printf '0x%x' $(( ( ($1 + 0xfff) / 0x1000 ) * 0x1000 )); }
BOOT_SZ=$(roundup "$(stat -f%z "$NANDBOOT_BIN")")
APP_SZ=$(roundup "$(stat -f%z "$HARMONY_BIN")")

boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"

echo "bootstrap (DDR init) : $BOOTSTRAP_ELF (entry $boot_entry)"
echo "u-boot (flasher)     : $UBOOT_BIN -> 0x23f00000"
echo "NAND boot region     : $NANDBOOT_BIN -> NAND $BOOT_OFF ($BOOT_SZ)"
echo "marvin app           : $HARMONY_BIN -> NAND $APP_OFF ($APP_SZ)"
echo "console              : $CONSOLE"

# Pre-flight: confirm the DBGU console is free BEFORE touching the board, so we
# don't stage everything over JTAG only to find the port busy.
uv run --with pyserial python "$here/nand_console.py" "$CONSOLE" probe

# 1) Stage the three blobs into DDR over JTAG and start u-boot (JTAG released).
common=(-f "$here/sam9x75-chybrid.cfg" -f "$here/program-nand.cfg")
[ -n "$FTDI_SERIAL" ] && common=(-c "set FTDI_SERIAL $FTDI_SERIAL" "${common[@]}")
openocd "${common[@]}" -c "init" \
    -c "stage_for_nand $BOOTSTRAP_ELF $boot_entry $UBOOT_BIN $NANDBOOT_BIN $HARMONY_BIN" \
    -c "shutdown"

# 2) Drive the erase + write + verify from the console.
uv run --with pyserial python "$here/nand_console.py" "$CONSOLE" program \
    "$BOOT_ADDR" "$BOOT_OFF" "$BOOT_SZ" "$APP_ADDR" "$APP_OFF" "$APP_SZ"

echo
echo ">>> Done. Power off, set JP3 (NAND) IN / JP4 (QSPI) OUT, remove any SD, power on."
echo ">>> marvin boots from NAND (DBGU banner, no JTAG)."
