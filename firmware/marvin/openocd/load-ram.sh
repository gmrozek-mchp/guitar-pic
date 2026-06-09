#!/usr/bin/env bash
# Load marvin into DDR over JTAG and run it, with no SD card.
#
# Reuses at91bootstrap to initialize clocks + DDR (see load-ram.cfg), then loads
# marvin's ELF into DDR and jumps to it. macOS/OpenOCD only.
#
# PREREQUISITES:
#   * Both memory CS jumpers OUT (JP3 NAND, JP4 QSPI) -> board sits in the ROM
#     SAM-BA monitor (clean state, DDR uninitialized).
#   * POWER-CYCLE the board first: DDR3L init only completes cleanly on a fresh
#     MPDDRC. Also kill any other process holding the FT4232H (e.g. a debug
#     OpenOCD server) so this invocation can claim it.
#   * Run outside the Claude command sandbox (it blocks USB).
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

# XC32 ships binutils (nm/objdump/readelf) that read these ARM ELFs.
xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
NM="${NM:-${xc32bin:+$xc32bin/}xc32-nm}"
OBJDUMP="${OBJDUMP:-${xc32bin:+$xc32bin/}xc32-objdump}"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"

[ -f "$BOOTSTRAP_ELF" ] || { echo "missing bootstrap ELF: $BOOTSTRAP_ELF" >&2; exit 1; }
[ -f "$MARVIN_ELF" ]    || { echo "missing marvin ELF: $MARVIN_ELF (build marvin first)" >&2; exit 1; }

boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"
marvin_entry="$("$READELF" -h "$MARVIN_ELF" | awk '/Entry point/{print $NF}')"

# hw_init() return address = the instruction after `bl <hw_init>` in main().
# Stopping there means clocks + DDR are up but the MMU/banner/media-load haven't run.
bl_addr="$("$OBJDUMP" -d "$BOOTSTRAP_ELF" | awk '/<main>:/{m=1} m && /\<bl\>/ && /<hw_init>/{gsub(":","",$1); print $1; exit}')"
[ -n "$bl_addr" ] || { echo "could not find 'bl hw_init' in main() of $BOOTSTRAP_ELF" >&2; exit 1; }
hwinit_ret="$(printf '0x%x' $(( 0x$bl_addr + 4 )))"

echo "bootstrap : $BOOTSTRAP_ELF (entry $boot_entry, hw_init returns to $hwinit_ret)"
echo "marvin    : $MARVIN_ELF (entry $marvin_entry)"

args=()
[ -n "$FTDI_SERIAL" ] && args+=(-c "set FTDI_SERIAL $FTDI_SERIAL")
args+=(-f "$here/sam9x75-chybrid.cfg"
       -f "$here/load-ram.cfg"
       -c "init"
       -c "marvin_load_ram $BOOTSTRAP_ELF $boot_entry $hwinit_ret $MARVIN_ELF $marvin_entry"
       -c "shutdown")

exec openocd "${args[@]}"
