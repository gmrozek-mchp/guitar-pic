#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""
Fretboard data-stream monitor.

Reads the 12-byte frame emitted by data_stream.c:
    start (0x03) | green | red | yellow | blue | orange | end (0xFC)
where each colour is a little-endian uint16. Reports actual frame rate
and the latest values; flags framing errors.

Usage:
    ./ds_monitor.py <port> [--baud 500000] [--expect-hz 240]
    uv run ds_monitor.py <port> ...
"""
import argparse
import struct
import sys
import time

import serial  # pyserial

START = 0x03
END = 0xFC
FRAME_FMT = "<BHHHHHB"
FRAME_LEN = struct.calcsize(FRAME_FMT)
assert FRAME_LEN == 12

CHANNELS = ("green", "red", "yellow", "blue", "orange")


def find_frame(buf: bytearray) -> tuple[tuple[int, ...] | None, int]:
    """Return (values, dropped_bytes). values is None if no full frame yet.
       Skips bytes until a valid start+end pair is found; dropped_bytes
       counts misaligned bytes discarded during the search."""
    dropped = 0
    while len(buf) >= FRAME_LEN:
        if buf[0] != START or buf[FRAME_LEN - 1] != END:
            del buf[0]
            dropped += 1
            continue
        unpacked = struct.unpack(FRAME_FMT, bytes(buf[:FRAME_LEN]))
        del buf[:FRAME_LEN]
        return unpacked[1:6], dropped
    return None, dropped


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="Serial port (e.g. /dev/tty.usbmodem...)")
    ap.add_argument("--baud", type=int, default=500_000)
    ap.add_argument("--expect-hz", type=float, default=240.0)
    ap.add_argument("--report-every", type=float, default=1.0,
                    help="Seconds between status lines")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    print(f"Listening on {args.port} @ {args.baud} baud, expecting {args.expect_hz} Hz")

    buf = bytearray()
    frames = 0
    bytes_in = 0
    sync_drops = 0
    last_values: tuple[int, ...] | None = None
    t_start = time.monotonic()
    t_report = t_start

    try:
        while True:
            chunk = ser.read(256)
            if chunk:
                bytes_in += len(chunk)
                buf.extend(chunk)
                while True:
                    values, dropped = find_frame(buf)
                    sync_drops += dropped
                    if values is None:
                        break
                    frames += 1
                    last_values = values

            now = time.monotonic()
            if now - t_report >= args.report_every:
                dt = now - t_report
                hz = frames / dt
                t_report = now
                vals = (
                    " ".join(f"{c[0].upper()}{v:>4}" for c, v in zip(CHANNELS, last_values))
                    if last_values else "(no frames)"
                )
                drift = (hz - args.expect_hz) / args.expect_hz * 100 if args.expect_hz else 0
                print(f"{hz:6.1f} Hz ({drift:+5.1f}%)  bytes/s={bytes_in/dt:7.0f}  "
                      f"drops={sync_drops:3d}  buf={len(buf):3d}  {vals}")
                frames = 0
                bytes_in = 0
                sync_drops = 0
    except KeyboardInterrupt:
        elapsed = time.monotonic() - t_start
        print(f"\nStopped after {elapsed:.1f}s.")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
