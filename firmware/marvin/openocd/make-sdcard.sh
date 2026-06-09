#!/usr/bin/env bash
# Prepare a bootable microSD for marvin on macOS (SAM9X75 ROM SD-card boot).
#
# With the board's boot jumpers set for SD, the SAM9X75 BootROM looks for
# "boot.bin" in the FAT root and loads it (at91bootstrap, the SD bootstrap), which
# in turn loads its configured second stage. Our SD bootstrap is built with
# CONFIG_IMAGE_NAME="harmony.bin" (the Harmony MPU convention), so it loads marvin
# from "harmony.bin". This formats the card FAT and writes both files. The result
# boots standalone — no JTAG.
#
# macOS only. THIS ERASES THE TARGET DISK.
#
# Usage:   ./make-sdcard.sh /dev/diskN        (find it with: diskutil list)
# Env overrides:
#   BOOT_BIN   SD bootstrap (default: ../binaries/sam9x7-sdcardboot-harmony-4.0.13.bin)
#   APP_BIN    marvin app image (default: ../out/harmony.bin)
#   APP_NAME   second-stage filename on the card (default: harmony.bin)
#   VOL_NAME   FAT volume label (default: MARVIN)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

BOOT_BIN="${BOOT_BIN:-$root/binaries/sam9x7-sdcardboot-harmony-4.0.13.bin}"
APP_BIN="${APP_BIN:-$root/out/harmony.bin}"
APP_NAME="${APP_NAME:-harmony.bin}"
VOL_NAME="${VOL_NAME:-MARVIN}"

disk="${1:-}"
[ -n "$disk" ] || { echo "usage: $0 /dev/diskN   (see 'diskutil list')" >&2; exit 1; }
[ -f "$BOOT_BIN" ] || { echo "missing SD bootstrap: $BOOT_BIN" >&2; exit 1; }
[ -f "$APP_BIN" ]  || { echo "missing app image: $APP_BIN (build marvin first)" >&2; exit 1; }

disk="/dev/$(basename "$disk")"   # normalize "disk4" -> "/dev/disk4"

info="$(diskutil info "$disk" 2>/dev/null)" || { echo "not a disk: $disk" >&2; exit 1; }

# Safety net: refuse a fixed internal disk (the system drive). A built-in SD
# slot reports Internal:Yes but Removable Media:Removable — that is allowed.
if grep -qE 'Internal:[[:space:]]+Yes' <<<"$info" \
   && ! grep -qE 'Removable Media:[[:space:]]+Removable' <<<"$info"; then
  echo "REFUSING: $disk looks like a fixed internal disk." >&2
  exit 1
fi

echo "Target: $disk"
grep -E 'Device / Media Name|Disk Size|Removable Media|Internal:' <<<"$info" || true
echo
echo "This will ERASE ALL DATA on $disk and write:"
echo "  boot.bin    <- $BOOT_BIN"
echo "  $APP_NAME   <- $APP_BIN"
echo
read -r -p "Type the disk identifier ($(basename "$disk")) to confirm: " ans
[ "$ans" = "$(basename "$disk")" ] || { echo "aborted." >&2; exit 1; }

echo ">>> formatting $disk as FAT (MBR), label $VOL_NAME ..."
diskutil eraseDisk MS-DOS "$VOL_NAME" MBRFormat "$disk"

mnt="/Volumes/$VOL_NAME"
[ -d "$mnt" ] || { echo "expected mount at $mnt not found" >&2; exit 1; }

echo ">>> copying boot.bin + $APP_NAME to $mnt ..."
cp "$BOOT_BIN" "$mnt/boot.bin"
cp "$APP_BIN"  "$mnt/$APP_NAME"
sync

echo ">>> ejecting ..."
diskutil eject "$disk"
echo "done. Set the board boot jumpers for SD, insert the card, and power-cycle."
