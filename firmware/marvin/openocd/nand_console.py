#!/usr/bin/env python3
"""Drive the marvin u-boot console over the DBGU serial port for NAND ops.

Shared by program-nand.sh and erase-nand.sh. Run via uv so pyserial is present:
    uv run --with pyserial python nand_console.py <port> <op> [args...]

Ops:
    probe                       open the port exclusively and exit (pre-flight)
    erase   <region|chip>       erase boot+app region (0x0..0x100000) or whole chip
    program <ba> <bo> <bs>      erase 0x0..0x100000, then nand-write the boot region
            <aa> <ao> <as>      from DDR <ba> and the app from DDR <aa>, and verify
                                both against the staged DDR copies (cmp.b)

All ops first wait for a STABLE U-Boot prompt (so commands aren't eaten by the
"Hit any key to stop autoboot" countdown). nand commands are retried once if their
expected keyword is missing. Exit status is 0 only on confirmed success.
"""
import sys, time

try:
    import serial
except ImportError:
    sys.exit("nand_console: pyserial missing — run via: uv run --with pyserial python ...")

BAUD = 115200
RB1 = "0x24000000"   # scratch DDR for boot-region read-back
RB2 = "0x24800000"   # scratch DDR for app read-back


def open_port(port):
    # macOS cu.* devices are exclusive: a second open fails with EBUSY, so this
    # cleanly detects a console already held by screen/minicom/etc.
    try:
        return serial.Serial(port, BAUD, timeout=0.3, exclusive=True)
    except Exception as e:
        sys.exit(f"nand_console: cannot open console {port}: {e}\n"
                 f"  In use elsewhere (screen/minicom)? Close it, or set CONSOLE=<node>.")


def wait_prompt(ser, timeout=12):
    end = time.time() + timeout
    while time.time() < end:
        ser.write(b"\r\n"); time.sleep(0.4)
        buf = ser.read(8192)
        if buf.rstrip().endswith(b"U-Boot>"):
            time.sleep(0.3)
            if not ser.read(8192):          # quiet → prompt settled (autoboot stopped)
                return True
    return False


def cmd(ser, c, t=120):
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


def run(ser, c, expect, t=120):
    out = cmd(ser, c, t)
    if expect not in out:                   # raced the prompt — resync and retry once
        print(f">>> '{c}' missing '{expect}'; resyncing prompt and retrying")
        wait_prompt(ser); out = cmd(ser, c, t)
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: nand_console.py <port> <probe|erase|program> [args...]")
    port, op = sys.argv[1], sys.argv[2]
    ser = open_port(port)

    if op == "probe":
        ser.close(); print(f"nand_console: {port} available"); return 0

    print(">>> connecting to u-boot console (booting + stopping autoboot)...",
          flush=True)
    if not wait_prompt(ser):
        sys.exit(f"nand_console: no stable U-Boot prompt on {port} (is u-boot running?)")

    if op == "erase":
        scope = sys.argv[3] if len(sys.argv) > 3 else "region"
        ecmd = "nand erase.chip" if scope == "chip" else "nand erase 0x0 0x100000"
        run(ser, ecmd, "NAND erase")
        cmd(ser, f"nand read {RB1} 0x0 0x1000")
        chk = cmd(ser, f"md.l {RB1} 4")
        ser.close()
        ok = "ffffffff ffffffff" in chk
        print("\n>>> NAND erased (boot region all-0xff)." if ok
              else "\n>>> WARNING: erase not confirmed — check output above.")
        return 0 if ok else 1

    if op == "program":
        ba, bo, bs, aa, ao, asz = sys.argv[3:9]
        # Chip-erase, not a fixed-size region erase. `nand write` needs erased
        # (0xff) pages, and the boot bootstrap reads a fixed CONFIG_IMG_SIZE window
        # (>= the app) from 0x40000 at boot — every page it writes AND every page
        # that window reads must be clean, or PMECC fails on a stale/half-written
        # page. A hardcoded region erase silently undershoots once the app grows
        # past it; chip-erase always covers both regardless of image or IMG_SIZE.
        run(ser, "nand erase.chip", "NAND erase")
        run(ser, f"nand write {ba} {bo} {bs}", "written")     # boot region
        run(ser, f"nand write {aa} {ao} {asz}", "written")    # app
        cmd(ser, f"nand read {RB1} {bo} {bs}")
        v1 = cmd(ser, f"cmp.b {ba} {RB1} {bs}")
        cmd(ser, f"nand read {RB2} {ao} {asz}")
        v2 = cmd(ser, f"cmp.b {aa} {RB2} {asz}")
        ser.close()
        ok = "were the same" in v1 and "were the same" in v2
        print("\n>>> NAND programmed and verified." if ok
              else "\n>>> WARNING: verify failed — check output above.")
        return 0 if ok else 1

    ser.close()
    sys.exit(f"nand_console: unknown op '{op}'")


if __name__ == "__main__":
    sys.exit(main())
