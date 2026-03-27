"""
FastAPI server for fret-tuner.

Streams ADC data from the serial port (or CSV replay), runs detection,
manages actuation, and pushes everything to the browser over WebSocket.
"""

import asyncio
import importlib
import json
import logging
import threading
import time
from collections import deque
from pathlib import Path
from typing import Optional

import serial
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse

from actuator import Actuator, DEFAULT_TIMING
from stream import CHANNELS, CsvStream, Sample, SerialStream

log = logging.getLogger("fret-tuner")

BATCH_INTERVAL = 1.0 / 30  # push to browser ~30x/sec
HISTORY_SECONDS = 30
HISTORY_CAP = HISTORY_SECONDS * 500

app = FastAPI()

# --- global state (set by configure() before uvicorn starts) ---

_stream = None
_serial_port: Optional[str] = None
_detector_name: str = "detect_threshold"
_detector = None
_detector_params: dict = {}
_actuator: Optional[Actuator] = None
_ser_obj = None  # pyserial Serial instance, shared for read + write

_lock = threading.Lock()
_raw_history: deque[Sample] = deque(maxlen=HISTORY_CAP)
_batch: list[dict] = []
_batch_states: list[dict] = []
_pending_status_msgs: list[str] = []
_ws_clients: set[WebSocket] = set()
_paused = False


def _discover_detectors() -> list[str]:
    """Find all detect_*.py modules in the fret-tuner directory."""
    here = Path(__file__).parent
    names = []
    for p in sorted(here.glob("detect_*.py")):
        names.append(p.stem)
    return names


def _load_detector(name: str, params: Optional[dict] = None):
    """Import and instantiate a detector by module name."""
    mod = importlib.import_module(name)
    importlib.reload(mod)  # pick up edits without restarting server
    cls = mod.Detector
    defaults = cls.default_params()
    if params:
        defaults.update(params)
    return cls(**defaults), defaults


def configure(
    stream,
    detector_name: str,
    detector_params: dict,
    serial_port: Optional[str] = None,
    ser_obj=None,
):
    """Called by fret-tuner.py before starting uvicorn."""
    global _stream, _serial_port, _detector_name, _detector, _detector_params
    global _actuator, _ser_obj

    _stream = stream
    _serial_port = serial_port
    _ser_obj = ser_obj
    _detector_name = detector_name
    _detector, _detector_params = _load_detector(detector_name, detector_params)
    _actuator = Actuator(ser=ser_obj)


RECONNECT_INTERVAL = 2.0  # seconds between reconnect attempts


def _push_serial_status(connected: bool):
    """Queue a serial_status message for broadcast to all WebSocket clients."""
    msg = json.dumps({"type": "serial_status", "connected": connected})
    with _lock:
        _pending_status_msgs.append(msg)


def _try_reopen_serial() -> tuple:
    """Attempt to reopen the serial port. Returns (ser_obj, stream) or raises."""
    ser = serial.Serial(
        port=_serial_port,
        baudrate=115200,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
    )
    ser.reset_input_buffer()
    return ser, SerialStream(ser=ser)


def _process_sample(sample: Sample, det, act):
    """Run detection + actuation for one sample and buffer results."""
    global _batch, _batch_states

    state = det.update(sample)

    now_ms = sample.timestamp * 1000.0
    if act is not None:
        act.update(now_ms, state)

    row = {
        "t": round(sample.timestamp, 6),
        "green": sample.green,
        "red": sample.red,
        "yellow": sample.yellow,
        "blue": sample.blue,
        "orange": sample.orange,
    }
    state_row = {}
    for ch in CHANNELS:
        ch_s = state.get(ch, {})
        state_row[ch] = {
            "pressed": ch_s.get("pressed", False),
            "baseline": ch_s.get("baseline", 0),
        }

    with _lock:
        _raw_history.append(sample)
        if not _paused:
            _batch.append(row)
            _batch_states.append(state_row)


def _feed_loop():
    """Background thread: read samples, run detection, batch for WebSocket.

    For serial sources, automatically reconnects when the device is lost.
    """
    global _stream, _ser_obj

    while True:
        try:
            for sample in _stream.samples():
                with _lock:
                    det = _detector
                    act = _actuator
                _process_sample(sample, det, act)

        except (serial.SerialException, OSError) as exc:
            if _serial_port is None:
                log.error("Stream error (no serial port to reconnect): %s", exc)
                return

            log.warning("Serial connection lost: %s", exc)
            _push_serial_status(False)

            if _ser_obj is not None:
                try:
                    _ser_obj.close()
                except Exception:
                    pass
                _ser_obj = None

            while True:
                time.sleep(RECONNECT_INTERVAL)
                try:
                    new_ser, new_stream = _try_reopen_serial()
                except (serial.SerialException, OSError) as retry_exc:
                    log.debug("Reconnect attempt failed: %s", retry_exc)
                    continue

                _ser_obj = new_ser
                _stream = new_stream
                with _lock:
                    if _actuator is not None:
                        _actuator.set_serial(new_ser)

                log.info("Serial reconnected on %s", _serial_port)
                _push_serial_status(True)
                break

        except StopIteration:
            return


async def _broadcast_loop():
    """Async loop: flush batched data and status messages to WebSocket clients."""
    global _batch, _batch_states, _pending_status_msgs

    while True:
        await asyncio.sleep(BATCH_INTERVAL)

        with _lock:
            status_msgs = _pending_status_msgs
            _pending_status_msgs = []

            has_data = bool(_batch)
            payload = _batch
            states = _batch_states
            _batch = []
            _batch_states = []

        dead = set()

        for smsg in status_msgs:
            for ws in list(_ws_clients):
                try:
                    await ws.send_text(smsg)
                except Exception:
                    dead.add(ws)

        if has_data:
            msg = json.dumps({"type": "data", "samples": payload, "state": states})
            for ws in list(_ws_clients):
                try:
                    await ws.send_text(msg)
                except Exception:
                    dead.add(ws)

        _ws_clients.difference_update(dead)


def _detector_info() -> dict:
    return {
        "type": "detector",
        "name": _detector_name,
        "params": _detector_params,
        "available": _discover_detectors(),
        "timing": {
            "STRUM_DELAY_MS": _actuator.strum_delay_ms if _actuator else DEFAULT_TIMING["STRUM_DELAY_MS"],
            "FRET_EARLY_MS": _actuator.fret_early_ms if _actuator else DEFAULT_TIMING["FRET_EARLY_MS"],
            "STRUM_PULSE_MS": _actuator.strum_pulse_ms if _actuator else DEFAULT_TIMING["STRUM_PULSE_MS"],
            "CHORD_WINDOW_MS": _actuator.chord_window_ms if _actuator else DEFAULT_TIMING["CHORD_WINDOW_MS"],
        },
        "actuate_enabled": _actuator.enabled if _actuator else False,
    }


@app.on_event("startup")
async def startup():
    t = threading.Thread(target=_feed_loop, daemon=True)
    t.start()
    asyncio.create_task(_broadcast_loop())


@app.get("/")
async def index():
    return FileResponse(Path(__file__).parent / "index.html")


@app.get("/api/detectors")
async def list_detectors():
    return JSONResponse(_discover_detectors())


@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    global _detector, _detector_name, _detector_params, _paused

    await ws.accept()
    _ws_clients.add(ws)

    try:
        await ws.send_text(json.dumps(_detector_info()))

        while True:
            text = await ws.receive_text()
            msg = json.loads(text)
            msg_type = msg.get("type")

            if msg_type == "pause":
                _paused = True

            elif msg_type == "resume":
                _paused = False

            elif msg_type == "set_params":
                new_params = msg.get("params", {})
                with _lock:
                    _detector_params.update(
                        {k: int(v) for k, v in new_params.items()}
                    )
                    _detector, _detector_params = _load_detector(
                        _detector_name, _detector_params
                    )
                await ws.send_text(json.dumps(_detector_info()))

            elif msg_type == "set_detector":
                name = msg.get("name", _detector_name)
                with _lock:
                    _detector_name = name
                    _detector, _detector_params = _load_detector(name)
                await ws.send_text(json.dumps(_detector_info()))

            elif msg_type == "set_timing":
                timing = msg.get("params", {})
                if _actuator:
                    _actuator.set_timing(**timing)
                await ws.send_text(json.dumps(_detector_info()))

            elif msg_type == "set_actuate":
                if _actuator:
                    _actuator.set_enabled(msg.get("enabled", False))
                await ws.send_text(json.dumps(_detector_info()))

    except WebSocketDisconnect:
        pass
    finally:
        _ws_clients.discard(ws)
