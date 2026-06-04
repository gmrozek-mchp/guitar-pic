#!/usr/bin/env python3
"""Measure the fretboard's TC0 callback rate from its data-stream over serial.

Each 17-byte frame carries `sample_seq`, incremented once per 240 Hz TC0
callback (before the TX-buffer check, so a dropped send still advances it). So:

  * device tick rate  = how fast sample_seq advances  -> the loop's real rate.
    If this is below 240 Hz, the model loop is overrunning the 4.17 ms tick.
  * received fps       = frames actually arriving at the PC.
  * drops              = seq advanced but frame not received -> UART can't keep
    up (unlikely at 500k baud) rather than the loop being slow.

Frame layout (data_stream.c, packed little-endian):
  uint8 start(0x03), uint16 g,r,y,b,o, uint32 sample_seq, uint8 applied_mask, uint8 end(0xFC)

Run from tools/marvin-perf (its venv has pyserial):
    uv run python fretboard_rate.py --port /dev/cu.usbmodemXXXX
"""

from __future__ import annotations

import argparse
import glob
import struct
import sys
import time

import serial  # pyserial (a marvin-perf dependency)

START = 0x03
END = 0xFC
FRAME_LEN = 21  # MODEL_DRIVEN frame
_FMT = "<B5HIIBB"  # start, 5x adc, sample_seq, infer_count, applied_mask, end = 21 bytes
assert struct.calcsize(_FMT) == FRAME_LEN


def find_port(cli: str | None) -> str:
    if cli:
        return cli
    cands = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/tty.usbmodem*")
                   + glob.glob("/dev/ttyACM*"))
    if not cands:
        sys.exit("no serial port found; pass --port")
    return cands[0]


def frames(ser: serial.Serial):
    """Yield (seq, infer_count, applied_mask, adc5) per valid frame; resync on bad framing."""
    buf = bytearray()
    while True:
        chunk = ser.read(512)
        if chunk:
            buf.extend(chunk)
        while len(buf) >= FRAME_LEN:
            if buf[0] != START:
                del buf[0]
                continue
            if buf[FRAME_LEN - 1] != END:
                del buf[0]            # a 0x03 inside the payload; slide one byte
                continue
            vals = struct.unpack(_FMT, bytes(buf[:FRAME_LEN]))
            del buf[:FRAME_LEN]
            yield vals[6], vals[7], vals[8], vals[1:6]
        if not chunk:
            time.sleep(0.001)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first cu.usbmodem/ttyACM)")
    ap.add_argument("--baud", type=int, default=500000)
    ap.add_argument("--interval", type=float, default=1.0, help="report interval (s)")
    args = ap.parse_args()

    port = find_port(args.port)
    ser = serial.Serial(port, args.baud, timeout=0.05)
    print(f"reading {port} @ {args.baud} baud — expect device tick ~240 Hz; Ctrl-C to stop\n")

    STRUM_BIT = 1 << 5
    last_seq = None
    prev_strum = 0
    tot_recv = tot_drop = tot_span = tot_strums = 0
    w_first_seq = w_last_seq = None
    w_first_infer = w_last_infer = None
    w_recv = w_active = w_strums = 0
    w_start = time.monotonic()
    run_start = w_start
    try:
        for seq, infer, mask, _adc in frames(ser):
            now = time.monotonic()
            if last_seq is not None:
                step = (seq - last_seq) & 0xFFFFFFFF
                if 1 <= step < 1000:          # ignore wrap/garbage
                    tot_span += step
                    if step > 1:
                        tot_drop += step - 1
            last_seq = seq
            tot_recv += 1

            strum = mask & STRUM_BIT
            if strum and not prev_strum:       # strum rising edge = one strum event
                w_strums += 1
                tot_strums += 1
            prev_strum = strum

            if w_first_seq is None:
                w_first_seq = seq
                w_first_infer = infer
            w_last_seq = seq
            w_last_infer = infer
            w_recv += 1
            if mask & 0x3F:                    # any fret or strum asserted
                w_active += 1

            if now - w_start >= args.interval:
                dt = now - w_start
                span = (w_last_seq - w_first_seq) & 0xFFFFFFFF
                tick_hz = span / dt
                infer_hz = ((w_last_infer - w_first_infer) & 0xFFFFFFFF) / dt
                drop = span - (w_recv - 1)
                flag = "" if tick_hz >= 238 else " tick<240"
                # streaming locks infer to the sample rate; only flag a real shortfall
                # (inference falling behind sampling -> command lag growing)
                iflag = "" if infer_hz >= tick_hz - 5 else "  <-- INFER LAGGING SAMPLES"
                print(f"tick={tick_hz:6.1f} Hz | infer={infer_hz:6.1f} Hz | "
                      f"drop={drop:4d} | active={100*w_active/max(1,w_recv):3.0f}% | "
                      f"strums={w_strums/dt:4.1f}/s{flag}{iflag}")
                w_start, w_first_seq, w_first_infer, w_recv, w_active, w_strums = \
                    now, seq, infer, 0, 0, 0
    except KeyboardInterrupt:
        dt = time.monotonic() - run_start
        print(f"\nsummary: {tot_recv} frames in {dt:.1f}s | "
              f"device tick ~{tot_span/dt:.1f} Hz | "
              f"drops {tot_drop} ({100*tot_drop/max(1,tot_span):.1f}%) | "
              f"strums {tot_strums} (~{tot_strums/dt:.1f}/s)")


if __name__ == "__main__":
    main()
