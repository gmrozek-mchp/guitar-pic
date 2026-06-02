#!/usr/bin/env python3
"""
Fret-tuner: development tool for note/strum detection algorithms.

Launches a local web server and opens the browser UI for interactive
visualization of fretboard ADC data with detection overlays, parameter
tuning, and optional hardware actuation.

Usage examples:

    # Live from hardware
    python fret-tuner.py --port /dev/cu.usbmodem21202

    # Replay a CSV capture
    python fret-tuner.py --csv ../../firmware/fretboard/fretboard-sample-raw.csv

    # Start with a specific detector and parameters
    python fret-tuner.py --port /dev/cu.usbmodem21202 --detector detect_trough \
        --param MIN_DROP=150 --param RISE_CONFIRM=30
"""

import argparse
import sys
import webbrowser
import threading

import serial
import uvicorn

from stream import CsvStream, SerialStream
import server


def _parse_param(s: str) -> tuple[str, str]:
    if "=" not in s:
        raise argparse.ArgumentTypeError(f"expected KEY=VALUE, got {s!r}")
    key, _, value = s.partition("=")
    return key.strip(), value.strip()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Fret-tuner: web-based fretboard ADC visualizer with detection tuning",
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Serial port for live data (e.g. /dev/cu.usbmodem21202)")
    source.add_argument("--csv", metavar="FILE", help="CSV file to replay")

    parser.add_argument(
        "--detector", default="detect_threshold", metavar="MODULE",
        help="Initial detection algorithm module (default: detect_threshold)",
    )
    parser.add_argument(
        "--param", action="append", type=_parse_param, default=[], metavar="KEY=VALUE",
        help="Override a detector parameter (repeatable)",
    )
    parser.add_argument(
        "--fast", action="store_true",
        help="CSV replay as fast as possible (no real-time pacing)",
    )
    parser.add_argument(
        "--host", default="127.0.0.1",
        help="Server bind address (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--web-port", type=int, default=8080, metavar="PORT",
        help="Web server port (default: 8080)",
    )
    parser.add_argument(
        "--camera", type=int, default=None, metavar="ID",
        help="Initial camera device ID for the video reference (default: first available)",
    )
    parser.add_argument(
        "--actuator-port", default=None, metavar="PORT",
        help="Serial port for the actuator (GPIO output). May equal --port (handle "
             "is shared) or be a different device. Default: idle, picker in the UI.",
    )

    args = parser.parse_args()

    params = dict(args.param)

    ser_obj = None
    if args.csv:
        stream = CsvStream(args.csv, realtime=not args.fast)
    else:
        ser_obj = serial.Serial(
            port=args.port,
            baudrate=500000,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
        )
        ser_obj.reset_input_buffer()
        stream = SerialStream(ser=ser_obj)

    server.configure(
        stream=stream,
        detector_name=args.detector,
        detector_params={k: int(v) for k, v in params.items()},
        serial_port=args.port,
        ser_obj=ser_obj,
        camera_id=args.camera,
        actuator_port=args.actuator_port,
    )

    url = f"http://{args.host}:{args.web_port}"
    print(f"Starting fret-tuner at {url}")
    threading.Timer(1.0, lambda: webbrowser.open(url)).start()

    uvicorn.run(server.app, host=args.host, port=args.web_port, log_level="warning")


if __name__ == "__main__":
    main()
