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

case "${1:-region}" in
    chip)   ERASE="nand erase.chip" ;;
    region) ERASE="nand erase 0x0 0x100000" ;;
    *)      echo "usage: $0 [region|chip]" >&2; exit 2 ;;
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
echo "erase                : $ERASE"

# 1) Bring u-boot up in DDR over JTAG (JTAG released afterwards).
common=(-f "$here/sam9x75-chybrid.cfg" -f "$here/erase-nand.cfg")
[ -n "$FTDI_SERIAL" ] && common=(-c "set FTDI_SERIAL $FTDI_SERIAL" "${common[@]}")
openocd "${common[@]}" -c "init" \
    -c "load_uboot $BOOTSTRAP_ELF $boot_entry $UBOOT_BIN" \
    -c "shutdown"

# 2) Drive the erase over the console (stop autoboot first, then verify erased).
uv run --with pyserial python - "$CONSOLE" "$ERASE" <<'PY'
import sys, time, serial
port, erase = sys.argv[1], sys.argv[2]
ser = serial.Serial(port, 115200, timeout=0.3)

def wait_prompt(timeout=12):
    # u-boot needs ~1-2 s to boot; tap Enter until we get a STABLE "U-Boot>"
    # (so autoboot is fully stopped and command keystrokes aren't eaten by the
    # "Hit any key to stop autoboot" prompt).
    end = time.time() + timeout
    while time.time() < end:
        ser.write(b"\r\n"); time.sleep(0.4)
        buf = ser.read(8192)
        if buf.rstrip().endswith(b"U-Boot>"):
            time.sleep(0.3)
            if not ser.read(8192):            # quiet → prompt is settled
                return True
    return False

def cmd(c, t=120):
    ser.reset_input_buffer(); ser.write(c.encode() + b"\r\n")
    end = time.time() + t; buf = b""
    while time.time() < end:
        d = ser.read(4096)
        if d:
            buf += d
            if buf.rstrip().endswith(b"U-Boot>"): break
    txt = buf.decode("utf-8", "replace")
    print(f"\n=== $ {c} ===\n" + txt)
    return txt

if not wait_prompt():
    print("\n>>> ERROR: never reached a stable U-Boot prompt."); ser.close(); sys.exit(1)

out = cmd(erase)
if "NAND erase" not in out:                      # raced the prompt — retry once
    print(">>> erase did not execute; retrying after re-syncing prompt")
    wait_prompt(); out = cmd(erase)

cmd("nand read 0x24000000 0x0 0x1000")
chk = cmd("md.l 0x24000000 4")                   # expect ffffffff x4 if erased
ser.close()
ok = "NAND erase" in out and "ffffffff ffffffff" in chk
print("\n>>> boot region erased." if ok
      else "\n>>> WARNING: erase not confirmed — check output above.")
sys.exit(0 if ok else 1)
PY

echo
echo ">>> NAND erased. The ROM will now fall through to the SAM-BA monitor on boot,"
echo ">>> so ./load-ram.sh works again (no jumper change needed)."
