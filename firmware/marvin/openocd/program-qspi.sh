#!/usr/bin/env bash
# Program a QSPI NOR region (splash / assets) from the host over JTAG — no SD card.
# Stages an init-and-stop at91bootstrap (brings up DDR) + a u-boot SF flasher + the
# blob into DDR over JTAG, starts u-boot, then erases + writes + verifies the QSPI
# from the DBGU console. macOS/OpenOCD only. See program-qspi.md. Mirrors
# program-nand.sh.
#
# Offsets come from default/src/flash/qspi_layout.h:
#   splash -> 0x010000   assets -> 0x400000   (both 0x3F0000 = 4,128,768 B)
# Writes are confined to the 64 KiB-uniform host window [0x010000, 0x7F0000): the
# bottom and top 64 KiB of this part are non-uniform Block-Erase bands (8/32 KiB
# blocks), where u-boot's 64 KiB `sf erase` under-erases without reporting it.
#
# PREREQUISITES (see program-qspi.md):
#   * JP4 (QSPI-CS) IN  — the part must be connected for `sf` to reach it. QSPI holds
#     no boot image, so RomBOOT skips it (won't boot).
#   * JP3 (NAND-CS) OUT and no bootable microSD — so nothing boots marvin and the
#     core is caught cold (DDR off). program-qspi.cfg refuses if the core is in DDR.
#   * Nothing else holding the FT4232H or the DBGU console node.
#
# Usage:
#   ./program-qspi.sh shell                    # stage u-boot only -> console (test `sf probe 0`)
#   ./program-qspi.sh splash [file]            # -> QSPI 0x010000  (default $root/data/ui/splash.raw)
#   ./program-qspi.sh assets <file> [offset]   # -> QSPI 0x400000 + offset
#   ./program-qspi.sh raw <qspi_off> <file>    # -> QSPI <qspi_off> (escape hatch)
#
# Env overrides:
#   BOOTSTRAP_ELF  init-and-stop at91bootstrap ELF (default: ../binaries/sam9x7-boot-none-4.0.13.elf)
#   UBOOT_BIN      u-boot raw binary with SF support, links/enters at 0x23f00000.
#                  Default is the NAND flasher — its build includes `sf` (confirmed
#                  `sf probe` detects the SST26), so it doubles as the QSPI flasher;
#                  no separate qspiflash build is needed.
#   FTDI_SERIAL    target a specific board by its FT4232H serial
#   CONSOLE        DBGU console node (FT4232H channel C); auto-detected if unset
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

# QSPI region bases (keep in sync with default/src/flash/qspi_layout.h).
QSPI_SPLASH_OFFSET=0x010000
QSPI_ASSETS_OFFSET=0x400000
QSPI_HOST_WINDOW_OFFSET=0x010000  # first 64 KiB-uniform block
QSPI_HOST_WINDOW_LIMIT=0x7F0000   # exclusive end; above here is firmware-only
QSPI_BLOCK_SIZE=0x10000           # u-boot's erase granularity for this part

[ $# -ge 1 ] || { sed -n '2,30p' "$0"; exit 1; }
region="$1"; shift

case "$region" in
    shell)  ;;   # stage u-boot only, then drop to the interactive console (sf probe etc.)
    splash) QOFF=$QSPI_SPLASH_OFFSET; BLOB="${1:-$root/data/ui/splash.raw}" ;;
    assets) [ $# -ge 1 ] || { echo "usage: ./program-qspi.sh assets <file> [offset]" >&2; exit 1; }
            BLOB="$1"; QOFF=$(printf '0x%x' $(( QSPI_ASSETS_OFFSET + ${2:-0} ))) ;;
    raw)    [ $# -ge 2 ] || { echo "usage: ./program-qspi.sh raw <qspi_off> <file>" >&2; exit 1; }
            QOFF="$1"; BLOB="$2" ;;
    *)      echo "unknown region '$region' (splash|assets|raw)" >&2; exit 1 ;;
esac

BOOTSTRAP_ELF="${BOOTSTRAP_ELF:-$root/binaries/sam9x7-boot-none-4.0.13.elf}"
UBOOT_BIN="${UBOOT_BIN:-$root/binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin}"
FTDI_SERIAL="${FTDI_SERIAL:-}"
CONSOLE="${CONSOLE:-$(ls /dev/cu.usbserial-* 2>/dev/null | sort | sed -n '3p')}"

STAGE_ADDR=0x21100000             # blob staging in low DDR (matches qspi_console RB headroom)

# Shared by all modes: bootstrap + u-boot present, console known, bootstrap entry.
for f in "$BOOTSTRAP_ELF" "$UBOOT_BIN"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
[ -n "$CONSOLE" ] || { echo "no DBGU console found; set CONSOLE=/dev/cu.usbserial-... (channel C)" >&2; exit 1; }

xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"
boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"

common=(-f "$here/sam9x75-chybrid.cfg" -f "$here/program-qspi.cfg")
[ -n "$FTDI_SERIAL" ] && common=(-c "set FTDI_SERIAL $FTDI_SERIAL" "${common[@]}")

# `shell`: stage u-boot only (no blob, no programming), then hand over the console
# — e.g. to check this u-boot has SF support with `sf probe 0` before committing.
if [ "$region" = "shell" ]; then
    echo "u-boot (SF flasher)  : $UBOOT_BIN -> 0x23f00000"
    echo "console              : $CONSOLE"
    openocd "${common[@]}" -c "init" \
        -c "stage_uboot_only $BOOTSTRAP_ELF $boot_entry $UBOOT_BIN" \
        -c "shutdown"
    echo
    echo ">>> u-boot is running (JTAG released). Open the console and try 'sf probe 0':"
    echo ">>>   screen $CONSOLE 115200        (exit screen: Ctrl-A then k)"
    echo ">>> 'SF: Detected sst26...' => this u-boot can program QSPI; use ./program-qspi.sh splash|assets."
    echo ">>> no such command / error   => build the qspiflash u-boot (see program-qspi.md)."
    exit 0
fi

[ -f "$BLOB" ] || { echo "missing: $BLOB" >&2; exit 1; }
LEN=$(stat -f%z "$BLOB")
LEN_HEX=$(printf '0x%x' "$LEN")   # u-boot parses command args as HEX — pass lengths in hex
# u-boot's SF driver erases this SST26 in 64 KiB blocks, so the erase length must
# be 64 KiB-aligned (4 KiB-aligned gives `sf erase ... ERROR -22`).
roundup64k() { printf '0x%x' $(( ( ($1 + 0xffff) / 0x10000 ) * 0x10000 )); }
ESZ=$(roundup64k "$LEN")

# Host-window + alignment guards (all arithmetic in (( )), which parses 0x...).
# The window bounds are what keep every erase on a uniform 64 KiB block; stepping
# outside means `sf erase` silently clears less than it claims and the write lands
# on un-erased NOR (bits only ever clear, so the result is old AND new). The
# offset check matters most for `raw`, which can name any address.
qoff_d=$(( QOFF )); end_d=$(( qoff_d + ESZ ))
if (( qoff_d % QSPI_BLOCK_SIZE != 0 )); then
    echo "refusing: offset $(printf '0x%x' $qoff_d) is not 64 KiB-aligned (u-boot's sf erase granularity)." >&2; exit 1
fi
if (( qoff_d < QSPI_HOST_WINDOW_OFFSET || end_d > QSPI_HOST_WINDOW_LIMIT )); then
    echo "refusing: write [$(printf '0x%x' $qoff_d)..$(printf '0x%x' $end_d)) leaves the host window [$QSPI_HOST_WINDOW_OFFSET..$QSPI_HOST_WINDOW_LIMIT)." >&2
    echo "  Outside it the SST26's blocks are 8/32 KiB, not 64 KiB — u-boot would under-erase. Those bands (and the settings ring) are firmware-only." >&2
    exit 1
fi

echo "bootstrap (DDR init) : $BOOTSTRAP_ELF (entry $boot_entry)"
echo "u-boot (SF flasher)  : $UBOOT_BIN -> 0x23f00000"
echo "blob                 : $BLOB ($LEN bytes) -> DDR $STAGE_ADDR -> QSPI $QOFF (erase $ESZ)"
echo "console              : $CONSOLE"

# Pre-flight: confirm the DBGU console is free before touching the board.
uv run --with pyserial python "$here/qspi_console.py" "$CONSOLE" probe

# 1) Stage blob + u-boot into DDR over JTAG and start u-boot (JTAG released).
openocd "${common[@]}" -c "init" \
    -c "stage_for_qspi $BOOTSTRAP_ELF $boot_entry $UBOOT_BIN $BLOB $STAGE_ADDR" \
    -c "shutdown"

# 2) Drive sf erase + write + verify from the console (lengths in hex for u-boot).
uv run --with pyserial python "$here/qspi_console.py" "$CONSOLE" program \
    "$STAGE_ADDR" "$QOFF" "$LEN_HEX" "$ESZ"

echo
echo ">>> Done. QSPI $region region written + verified."
