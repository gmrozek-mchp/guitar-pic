#!/usr/bin/env bash
# Stage the NAND boot images into DDR over JTAG and start u-boot, which then
# programs on-board NAND from those buffers (driven from the DBGU console).
# macOS/OpenOCD only. No SD card, no SAM-BA. See program-nand.md for the full
# runbook, including the u-boot console commands to run after this exits.
#
# PREREQUISITES:
#   * No higher-priority boot medium catching the core first: remove any bootable
#     microSD, and QSPI must hold no valid image (JP4/QSPI-CS OUT). JP3 (NAND) may
#     be IN or OUT for *programming* (we drive NAND from u-boot, not RomBOOT) but
#     must be IN to boot from NAND afterwards.
#   * Nothing else holding the FT4232H (kill any debug OpenOCD server first).
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
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

BOOTSTRAP_ELF="${BOOTSTRAP_ELF:-$root/binaries/sam9x7-boot-none-4.0.13.elf}"
UBOOT_BIN="${UBOOT_BIN:-$root/binaries/sam9x75-uboot-nandflash-flasher-2025.07.bin}"
NANDBOOT_BIN="${NANDBOOT_BIN:-$root/binaries/sam9x7-nandflashboot-uboot-4.0.13-pmecchead.bin}"
HARMONY_BIN="${HARMONY_BIN:-$root/out/harmony.bin}"
FTDI_SERIAL="${FTDI_SERIAL:-}"

# XC32 ships binutils that read the bootstrap ARM ELF for its entry point.
xc32bin="$(ls -d /Applications/microchip/xc32/*/bin 2>/dev/null | sort -V | tail -1 || true)"
READELF="${READELF:-${xc32bin:+$xc32bin/}xc32-readelf}"

for f in "$BOOTSTRAP_ELF" "$UBOOT_BIN" "$NANDBOOT_BIN" "$HARMONY_BIN"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

boot_entry="$("$READELF" -h "$BOOTSTRAP_ELF" | awk '/Entry point/{print $NF}')"

echo "bootstrap (DDR init) : $BOOTSTRAP_ELF (entry $boot_entry)"
echo "u-boot (flasher)     : $UBOOT_BIN -> 0x23f00000"
echo "NAND boot region     : $NANDBOOT_BIN -> NAND 0x0"
echo "marvin app           : $HARMONY_BIN -> NAND 0x40000"

common=(-f "$here/sam9x75-chybrid.cfg" -f "$here/program-nand.cfg")
[ -n "$FTDI_SERIAL" ] && common=(-c "set FTDI_SERIAL $FTDI_SERIAL" "${common[@]}")

openocd "${common[@]}" -c "init" \
    -c "stage_for_nand $BOOTSTRAP_ELF $boot_entry $UBOOT_BIN $NANDBOOT_BIN $HARMONY_BIN" \
    -c "shutdown"

cat <<'EOF'

>>> u-boot is now running. Open the DBGU console and program NAND:

    screen /dev/cu.usbserial-W16_2026_4302 115200      # channel C; Ctrl-A k to quit

  At the U-Boot> prompt (images are pre-staged in DDR):

    nand info
    nand erase 0x0 0x100000
    nand write 0x21100000 0x0      0x4000      # at91bootstrap -> boot region
    nand write 0x21200000 0x40000  0x80000     # harmony.bin   -> app

  NOTE: hit a key within 3 s of u-boot starting to stop autoboot, or its default
  bootcmd reads the empty kernel partition into DDR and overwrites the staged
  blobs (the staging addresses above are chosen to dodge that, but don't dawdle).

  Then power off, set JP3 (NAND) IN / JP4 (QSPI) OUT, remove any SD, power on.
  marvin should boot from NAND (DBGU banner, no JTAG).
EOF
