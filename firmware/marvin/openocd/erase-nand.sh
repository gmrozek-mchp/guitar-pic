#!/usr/bin/env bash
# Erase on-board NAND from macOS over JTAG, to make the board non-bootable and
# return to RAM development (load-ram.sh) WITHOUT touching the JP3/JP4 jumpers —
# e.g. when the board is sealed in an enclosure. macOS/OpenOCD only.
#
# Why this exists: load-ram.sh needs no valid boot medium so `reset init` can
# catch the core. Once marvin is resident in NAND (JP3 in), erasing NAND is the
# no-jumper way back to that state. It RAM-loads u-boot over JTAG (its atmel_nand
# driver does the erase), then drives the erase over the DBGU console.
#
# Usage:
#   ./erase-nand.sh            # erase the boot+app region (0x0..0x100000) — enough
#                              #   to make NAND non-bootable
#   ./erase-nand.sh chip       # full-chip erase
#
# Env overrides:
#   BOOTSTRAP_ELF  init-and-stop at91bootstrap ELF (brings up DDR over JTAG)
#   UBOOT_BIN      u-boot flasher (RAM-loaded; links/enters at 0x23f00000)
#   FTDI_SERIAL    target a specific board by its FT4232H serial
#   CONSOLE        DBGU console node (FT4232H channel C). Auto-detected as the 3rd
#                  /dev/cu.usbserial-* if unset; set explicitly with >1 board.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

BOOTSTRAP_ELF="${BOOTSTRAP_ELF:-$root/binaries/sam9x7-boot-none-4.0.13.elf}"
UBOOT_BIN="${UBOOT_BIN:-$root/binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin}"
FTDI_SERIAL="${FTDI_SERIAL:-}"
CONSOLE="${CONSOLE:-$(ls /dev/cu.usbserial-* 2>/dev/null | sort | sed -n '3p')}"

SCOPE="${1:-region}"
case "$SCOPE" in
    region|chip) ;;
    *) echo "usage: $0 [region|chip]" >&2; exit 2 ;;
esac

xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"

for f in "$BOOTSTRAP_ELF" "$UBOOT_BIN"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
[ -n "$CONSOLE" ] || { echo "no DBGU console found; set CONSOLE=/dev/cu.usbserial-... (channel C)" >&2; exit 1; }

boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"

echo "bootstrap (DDR init) : $BOOTSTRAP_ELF (entry $boot_entry)"
echo "u-boot (flasher)     : $UBOOT_BIN -> 0x23f00000"
echo "console              : $CONSOLE"
echo "erase                : $SCOPE"

# Pre-flight: confirm the DBGU console is free BEFORE touching the board.
uv run --with pyserial python "$here/nand_console.py" "$CONSOLE" probe

# 1) Bring u-boot up in DDR over JTAG (JTAG released afterwards).
common=(-f "$here/sam9x75-chybrid.cfg" -f "$here/erase-nand.cfg")
[ -n "$FTDI_SERIAL" ] && common=(-c "set FTDI_SERIAL $FTDI_SERIAL" "${common[@]}")
openocd "${common[@]}" -c "init" \
    -c "load_uboot $BOOTSTRAP_ELF $boot_entry $UBOOT_BIN" \
    -c "shutdown"

# 2) Drive the erase + verify over the console.
uv run --with pyserial python "$here/nand_console.py" "$CONSOLE" erase "$SCOPE"

echo
echo ">>> NAND erased. The ROM will now fall through to the SAM-BA monitor on boot,"
echo ">>> so ./load-ram.sh works again (no jumper change needed)."
