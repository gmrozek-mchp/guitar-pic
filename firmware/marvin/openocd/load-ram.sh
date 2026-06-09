#!/usr/bin/env bash
# Load marvin into DDR over JTAG and run it, with no SD card.
#
# Resets the SoC (`reset init`), reuses at91bootstrap to initialize clocks + DDR
# (see load-ram.cfg), then loads marvin's ELF into DDR and jumps to it.
# macOS/OpenOCD only.
#
# This is the dev iteration loop: it is REPEATABLE with no physical power-cycle.
# `reset init` resets the SoC from any state (incl. a running marvin), so
# at91bootstrap re-inits DDR3L fresh on every run — edit, rebuild, re-run.
#
# PREREQUISITES:
#   * Both memory CS jumpers OUT (JP3 NAND, JP4 QSPI) -> board sits in the ROM
#     SAM-BA monitor when no marvin is running (no boot media).
#   * Kill any other process holding the FT4232H (e.g. a debug OpenOCD server) so
#     this invocation can claim it.
#   * Run outside the Claude command sandbox (it blocks USB).
#
# Usage:   ./load-ram.sh
# Env overrides:
#   BOOTSTRAP_ELF  at91bootstrap "init-and-stop" ELF
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

# `marvin_load_ram` starts with `reset init`, which resets the SoC from ANY state
# (incl. a running marvin) via the nSRST pin and disables the watchdog — see
# sam9x75-chybrid.cfg. That makes the load repeatable with no physical power-cycle:
# bring DDR up fresh via the init-and-stop at91bootstrap each run, then load + run.
exec openocd "${common[@]}" -c "init" \
        -c "marvin_load_ram $BOOTSTRAP_ELF $boot_entry $MARVIN_ELF $marvin_entry" \
        -c "shutdown"
