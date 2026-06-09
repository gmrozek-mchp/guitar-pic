#!/usr/bin/env bash
# Load marvin into DDR over JTAG and run it, with no SD card.
#
# Reuses at91bootstrap to initialize the DDR3L controller (see load-ram.cfg),
# then loads marvin's ELF into DDR and jumps to it. Must run outside the Claude
# command sandbox (it blocks USB).
#
# Usage:   ./load-ram.sh
# Env overrides:
#   BOOTSTRAP_ELF  at91bootstrap ELF (default: ../binaries/at91bootstrap.elf)
#   MARVIN_ELF     marvin ELF        (default: ../out/marvin/default.elf)
#   FTDI_SERIAL    target a specific board by its FT4232H serial
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

BOOTSTRAP_ELF="${BOOTSTRAP_ELF:-$root/binaries/at91bootstrap.elf}"
MARVIN_ELF="${MARVIN_ELF:-$root/out/marvin/default.elf}"
FTDI_SERIAL="${FTDI_SERIAL:-}"

# XC32 ships binutils (nm/readelf) that read these ARM ELFs.
xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
NM="${NM:-${xc32bin:+$xc32bin/}xc32-nm}"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"

[ -f "$BOOTSTRAP_ELF" ] || { echo "missing bootstrap ELF: $BOOTSTRAP_ELF" >&2; exit 1; }
[ -f "$MARVIN_ELF" ]    || { echo "missing marvin ELF: $MARVIN_ELF (build marvin first)" >&2; exit 1; }

hwinit="$("$NM" "$BOOTSTRAP_ELF" | awk '$3=="hw_init"{print "0x"$1}')"
boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"
marvin_entry="$("$READELF" -h "$MARVIN_ELF" | awk '/Entry point/{print $NF}')"
[ -n "$hwinit" ] || { echo "hw_init symbol not found in $BOOTSTRAP_ELF" >&2; exit 1; }

echo "bootstrap : $BOOTSTRAP_ELF (entry $boot_entry, hw_init $hwinit)"
echo "marvin    : $MARVIN_ELF (entry $marvin_entry)"

args=()
[ -n "$FTDI_SERIAL" ] && args+=(-c "set FTDI_SERIAL $FTDI_SERIAL")
args+=(-f "$here/sam9x75-chybrid.cfg"
       -f "$here/load-ram.cfg"
       -c "init"
       -c "marvin_load_ram $BOOTSTRAP_ELF $boot_entry $hwinit $MARVIN_ELF $marvin_entry"
       -c "shutdown")

exec openocd "${args[@]}"
