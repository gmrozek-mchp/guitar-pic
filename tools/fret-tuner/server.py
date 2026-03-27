"""
FastAPI server for fret-tuner.

Streams ADC data from the serial port (or CSV replay), runs detection,
manages actuation, and pushes everything to the browser over WebSocket.
"""

import asyncio
import glob
import importlib
import json
import os
import threading
import time
from collections import deque
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse

from actuator import Actuator, DEFAULT_TIMING
from stream import CHANNELS, CsvStream, Sample, SerialStream

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


def _feed_loop():
    """Background thread: read samples, run detection, batch for WebSocket."""
    global _batch, _batch_states, _detector

    for sample in _stream.samples():
        with _lock:
            det = _detector
            act = _actuator

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


async def _broadcast_loop():
    """Async loop: flush batched data to all connected WebSocket clients."""
    global _batch, _batch_states

    while True:
        await asyncio.sleep(BATCH_INTERVAL)

        with _lock:
            if not _batch:
                continue
            payload = _batch
            states = _batch_states
            _batch = []
            _batch_states = []

        msg = json.dumps({"type": "data", "samples": payload, "state": states})

        dead = set()
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
