#!/usr/bin/env python3
"""Drive the marvin u-boot console over the DBGU serial port for QSPI (SF) ops.

Used by program-qspi.sh. Run via uv so pyserial is present:
    uv run --with pyserial python qspi_console.py <port> <op> [args...]

Ops:
    probe                          open the port exclusively and exit (pre-flight)
    program <sa> <off> <len> <esz> sf-probe, erase <off>..<esz> (4 KiB-rounded),
                                   write <len> bytes from DDR <sa> to QSPI <off>,
                                   then verify by reading back to scratch DDR and
                                   cmp.b against the staged copy

Mirrors nand_console.py: every op first waits for a STABLE U-Boot prompt (so the
"Hit any key to stop autoboot" countdown can't eat commands), and each sf command
is retried once if its expected keyword is missing. Exit status is 0 only on
confirmed success.
"""
import sys, time

try:
    import serial
except ImportError:
    sys.exit("qspi_console: pyserial missing — run via: uv run --with pyserial python ...")

BAUD = 115200
# Read-back scratch in low DDR, clear of the staged blob (0x21100000 + <=4 MiB)
# and below u-boot's autoboot window (0x22000000+).
RB = "0x21600000"


def open_port(port):
    try:
        return serial.Serial(port, BAUD, timeout=0.3, exclusive=True)
    except Exception as e:
        sys.exit(f"qspi_console: cannot open console {port}: {e}\n"
                 f"  In use elsewhere (screen/minicom)? Close it, or set CONSOLE=<node>.")


def wait_prompt(ser, timeout=12):
    end = time.time() + timeout
    while time.time() < end:
        ser.write(b"\r\n"); time.sleep(0.4)
        buf = ser.read(8192)
        if buf.rstrip().endswith(b"U-Boot>"):
            time.sleep(0.3)
            if not ser.read(8192):          # quiet -> prompt settled (autoboot stopped)
                return True
    return False


def cmd(ser, c, t=300):
    # u-boot's sf erase/write emit nothing until they finish (seconds to tens of
    # seconds for a multi-MB write), so print a heartbeat dot every ~2 s of silence
    # — otherwise a long op looks hung.
    ser.reset_input_buffer(); ser.write(c.encode() + b"\r\n")
    print(f"\n=== $ {c} ===", flush=True)
    end = time.time() + t; buf = b""; beat = time.time(); dotted = False
    while time.time() < end:
        d = ser.read(4096)
        if d:
            buf += d
            if buf.rstrip().endswith(b"U-Boot>"): break
        elif time.time() - beat >= 2.0:
            sys.stdout.write("."); sys.stdout.flush(); beat = time.time(); dotted = True
    txt = buf.decode("utf-8", "replace")
    print(("\n" if dotted else "") + txt)
    return txt


def run(ser, c, expect, t=300):
    out = cmd(ser, c, t)
    if expect not in out:                   # raced the prompt — resync and retry once
        print(f">>> '{c}' missing '{expect}'; resyncing prompt and retrying")
        wait_prompt(ser); out = cmd(ser, c, t)
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: qspi_console.py <port> <probe|program> [args...]")
    port, op = sys.argv[1], sys.argv[2]
    ser = open_port(port)

    if op == "probe":
        ser.close(); print(f"qspi_console: {port} available"); return 0

    print(">>> connecting to u-boot console (booting + stopping autoboot)...", flush=True)
    if not wait_prompt(ser):
        sys.exit(f"qspi_console: no stable U-Boot prompt on {port} (is u-boot running?)")

    if op == "program":
        # Lengths/offsets are passed in hex — u-boot parses command args as hex.
        sa, off, length, esz = sys.argv[3:7]

        # `sf probe` must succeed or nothing else will; bail clearly if the SF
        # stack isn't in this u-boot build (then build the qspiflash defconfig).
        if "Detected" not in run(ser, "sf probe 0", "Detected"):
            ser.close()
            sys.exit("qspi_console: `sf probe` failed — no SF support / no flash detected.")

        # Match the ': OK' suffix — `Erased`/`Written` also appear in the error
        # lines ('Erased: ERROR -22'), so the bare verb is not enough.
        if "Erased: OK" not in run(ser, f"sf erase {off} {esz}", "Erased: OK"):
            ser.close()
            sys.exit(f"qspi_console: sf erase {off} {esz} FAILED "
                     f"(erase length must be 64 KiB-aligned for this part). See output.")
        if "Written: OK" not in run(ser, f"sf write {sa} {off} {length}", "Written: OK"):
            ser.close()
            sys.exit(f"qspi_console: sf write {off} {length} FAILED. See output.")

        cmd(ser, f"sf read {RB} {off} {length}")
        v = cmd(ser, f"cmp.b {sa} {RB} {length}")
        ser.close()
        # cmp.b prints 'Total of 0 byte(s) were the same' AND a 'byte at X != Y'
        # line on mismatch, so require the match phrase and the absence of '!='.
        ok = ("were the same" in v) and ("!=" not in v)
        print("\n>>> QSPI programmed and verified." if ok
              else "\n>>> WARNING: verify FAILED — check output above.")
        return 0 if ok else 1

    ser.close()
    sys.exit(f"qspi_console: unknown op '{op}'")


if __name__ == "__main__":
    sys.exit(main())
