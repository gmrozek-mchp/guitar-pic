#!/usr/bin/env bash
# Load marvin into DDR over JTAG and run it, with no SD card. macOS/OpenOCD only.
# The dev iteration loop: repeatable with no power-cycle (edit, rebuild, re-run).
# See load-ram.cfg for the mechanism.
#
# PREREQUISITES:
#   * No bootable medium present: JP3/JP4 (NAND/QSPI CS) OUT and no bootable microSD
#     (else RomBOOT boots it before reset init catches the core).
#   * Nothing else holding the FT4232H (e.g. kill a running debug OpenOCD server).
#
# Usage:   ./load-ram.sh
# Env overrides:
#   BOOTSTRAP_ELF  at91bootstrap init-and-stop ELF
#                  (default: ../binaries/sam9x7-boot-none-4.0.13.elf)
#   MARVIN_ELF     marvin ELF (default: ../out/marvin/default.elf)
#   FTDI_SERIAL    target a specific board by its FT4232H serial
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

BOOTSTRAP_ELF="${BOOTSTRAP_ELF:-$root/binaries/sam9x7-boot-none-4.0.13.elf}"
MARVIN_ELF="${MARVIN_ELF:-$root/out/marvin/default.elf}"
FTDI_SERIAL="${FTDI_SERIAL:-}"

# XC32 ships binutils that read these ARM ELFs (just need the entry points).
xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"

[ -f "$BOOTSTRAP_ELF" ] || { echo "missing bootstrap ELF: $BOOTSTRAP_ELF" >&2; exit 1; }
[ -f "$MARVIN_ELF" ]    || { echo "missing marvin ELF: $MARVIN_ELF (build marvin first)" >&2; exit 1; }

boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"
marvin_entry="$("$READELF" -h "$MARVIN_ELF" | awk '/Entry point/{print $NF}')"

echo "bootstrap : $BOOTSTRAP_ELF (entry $boot_entry)"
echo "marvin    : $MARVIN_ELF (entry $marvin_entry)"

# Common args (never empty). A specific board serial, if given, must precede the
# config file that reads FTDI_SERIAL.
common=(-f "$here/sam9x75-chybrid.cfg" -f "$here/load-ram.cfg")
[ -n "$FTDI_SERIAL" ] && common=(-c "set FTDI_SERIAL $FTDI_SERIAL" "${common[@]}")

# marvin_load_ram (load-ram.cfg): reset init -> run bootstrap to completion -> load
# + run marvin.
exec openocd "${common[@]}" -c "init" \
        -c "marvin_load_ram $BOOTSTRAP_ELF $boot_entry $MARVIN_ELF $marvin_entry" \
        -c "shutdown"
