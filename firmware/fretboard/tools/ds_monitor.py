#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""
Fretboard data-stream monitor.

Reads the 17-byte frame emitted by data_stream.c:
    start (0x03) | green | red | yellow | blue | orange (5×uint16 LE)
    | sample_seq (uint32 LE) | applied_mask (uint8) | end (0xFC)
Reports actual frame rate, latest ADC values, sample_seq, and applied_mask;
flags framing errors and seq gaps.

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
# start(1) + 5×uint16(10) + uint32 sample_seq(4) + uint8 applied_mask(1) + end(1) = 17
FRAME_FMT = "<BHHHHHIBB"
FRAME_LEN = struct.calcsize(FRAME_FMT)
assert FRAME_LEN == 17

CHANNELS = ("green", "red", "yellow", "blue", "orange")


def find_frame(buf: bytearray) -> tuple[tuple[int, ...] | None, int]:
    """Return (values, dropped_bytes). values is None if no full frame yet.
       values = (green, red, yellow, blue, orange, sample_seq, applied_mask).
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
        # unpacked: (start, g, r, y, b, o, sample_seq, applied_mask, end)
        return unpacked[1:8], dropped
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
    seq_gaps = 0
    last_values: tuple[int, ...] | None = None
    last_seq: int | None = None
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
                    seq = values[5]
                    if last_seq is not None and seq != (last_seq + 1) & 0xFFFFFFFF:
                        seq_gaps += 1
                    last_seq = seq

            now = time.monotonic()
            if now - t_report >= args.report_every:
                dt = now - t_report
                hz = frames / dt
                t_report = now
                if last_values:
                    adcs = " ".join(f"{c[0].upper()}{v:>4}"
                                    for c, v in zip(CHANNELS, last_values[:5]))
                    vals = (f"{adcs}  seq={last_values[5]}  "
                            f"mask=0x{last_values[6]:02x}")
                else:
                    vals = "(no frames)"
                drift = (hz - args.expect_hz) / args.expect_hz * 100 if args.expect_hz else 0
                print(f"{hz:6.1f} Hz ({drift:+5.1f}%)  bytes/s={bytes_in/dt:7.0f}  "
                      f"drops={sync_drops:3d}  gaps={seq_gaps:3d}  buf={len(buf):3d}  {vals}")
                frames = 0
                bytes_in = 0
                sync_drops = 0
                seq_gaps = 0
    except KeyboardInterrupt:
        elapsed = time.monotonic() - t_start
        print(f"\nStopped after {elapsed:.1f}s.")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
