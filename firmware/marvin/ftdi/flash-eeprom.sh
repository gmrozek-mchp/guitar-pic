#!/usr/bin/env bash
# Flash the FT4232H EEPROM on the connected SAM9X75 cHybrid with a per-board serial.
# Run with the target board (and only that board, to be safe) connected via USB.
#
#   ./flash-eeprom.sh W16-2026-360
#
# Uses ft4232h-chybrid.conf as the base image and overrides only the serial.
# Must run outside the Claude command sandbox (it blocks USB).
set -euo pipefail

serial="${1:?usage: flash-eeprom.sh <SERIAL>}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
base="$here/ft4232h-chybrid.conf"

tmp="$(mktemp "${TMPDIR:-/tmp}/ft4232h.XXXXXX")"
conf="$tmp.conf"
bin="$tmp.bin"
trap 'rm -f "$tmp" "$conf" "$bin"' EXIT

sed -e "s/^serial=.*/serial=\"$serial\"/" \
    -e "s|^filename=.*|filename=\"$bin\"|" "$base" > "$conf"

ftdi_eeprom --device i:0x0403:0x6011 --flash-eeprom "$conf"
echo "Flashed serial: $serial — unplug/replug the board to re-enumerate."
