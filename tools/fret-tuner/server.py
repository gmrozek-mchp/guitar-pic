"""
FastAPI server for fret-tuner.

Wires together three subsystems:

* **Video reference** (`camera.py`) -- always-on, owns the camera and ring buffer.
* **Actuator** (`actuator.py`) -- always-on, owns its own GPIO serial port (or
  shares one with the data port when the same physical device is used for both).
* **Detector** (`detect_*.py`) -- swappable, consumes ADC samples and (optionally)
  video frames, emits per-channel state and overlay layers.

Streams ADC data from the serial port (or CSV replay), runs detection, feeds
detector state into the actuator, and pushes everything to the browser over
WebSocket.
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

import cv2
import serial
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse, Response
from starlette.responses import StreamingResponse

from actuator import Actuator, DEFAULT_TIMING
from camera import enumerate_cameras, get_camera
from stream import (
    CHANNELS,
    CsvStream,
    Sample,
    SerialStream,
    enumerate_serial_ports,
)

log = logging.getLogger("fret-tuner")

BATCH_INTERVAL = 1.0 / 30  # push to browser ~30x/sec
HISTORY_SECONDS = 30
HISTORY_CAP = HISTORY_SECONDS * 500
RECONNECT_INTERVAL = 2.0

app = FastAPI()

# --- global state (set by configure() before uvicorn starts) ---

_stream = None
_serial_port: Optional[str] = None  # data port (ADC source)
_detector_name: str = "detect_threshold"
_detector = None
_detector_params: dict = {}
_actuator: Optional[Actuator] = None
_ser_obj = None  # data-port pyserial Serial instance (when --port used)
_initial_camera_id: Optional[int] = None
_initial_actuator_port: Optional[str] = None

_lock = threading.Lock()
_raw_history: deque[Sample] = deque(maxlen=HISTORY_CAP)
_batch: list[dict] = []
_batch_states: list[dict] = []
_pending_status_msgs: list[str] = []
_ws_clients: set[WebSocket] = set()
_paused = False

_latest_sample_t: float = 0.0
_latest_state: dict = {}
_latest_actuator_mask: int = 0
_main_loop: Optional[asyncio.AbstractEventLoop] = None


def _discover_detectors() -> list[str]:
    here = Path(__file__).parent
    return [p.stem for p in sorted(here.glob("detect_*.py"))]


def _stop_detector(det):
    if det is not None and hasattr(det, "stop"):
        try:
            det.stop()
        except Exception as exc:
            log.warning("Error stopping detector: %s", exc)


def _load_detector(name: str, params: Optional[dict] = None):
    mod = importlib.import_module(name)
    importlib.reload(mod)
    cls = mod.Detector
    defaults = cls.default_params()
    if params:
        for k, v in params.items():
            if k in defaults:
                defaults[k] = v
    return cls(**defaults), defaults


def configure(
    stream,
    detector_name: str,
    detector_params: dict,
    serial_port: Optional[str] = None,
    ser_obj=None,
    camera_id: Optional[int] = None,
    actuator_port: Optional[str] = None,
):
    """Called by fret-tuner.py before starting uvicorn."""
    global _stream, _serial_port, _detector_name, _detector, _detector_params
    global _actuator, _ser_obj, _initial_camera_id, _initial_actuator_port

    _stream = stream
    _serial_port = serial_port
    _ser_obj = ser_obj
    _detector_name = detector_name
    _detector, _detector_params = _load_detector(detector_name, detector_params)
    _actuator = Actuator()
    _actuator.add_status_listener(_on_actuator_status)
    _initial_camera_id = camera_id
    _initial_actuator_port = actuator_port


# --- status / message helpers ----------------------------------------------


def _push_status(msg_dict: dict) -> None:
    """Queue a status message for broadcast to all WebSocket clients."""
    payload = json.dumps(msg_dict)
    with _lock:
        _pending_status_msgs.append(payload)


def _push_serial_status(connected: bool) -> None:
    _push_status({"type": "serial_status", "connected": connected})


def _on_actuator_status(_status: dict) -> None:
    """Listener registered with Actuator; pushes a snapshot to clients."""
    _push_status(_actuator_status_msg())


def _actuator_status_msg() -> dict:
    a = _actuator
    s = a.status() if a is not None else {"active": False, "port": None,
                                          "connected": False, "enabled": False}
    return {
        "type": "actuator_status",
        "available": enumerate_serial_ports(),
        **s,
    }


def _sources_msg() -> dict:
    cam = get_camera()
    return {
        "type": "sources",
        "available": enumerate_cameras(),
        "active": cam.status(),
    }


# --- data-port reconnect ---------------------------------------------------


def _try_reopen_serial() -> tuple:
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
    global _batch, _batch_states
    global _latest_sample_t, _latest_state, _latest_actuator_mask

    state = det.update(sample)

    now_ms = sample.timestamp * 1000.0
    if act is not None:
        mask = act.update(now_ms, state)
    else:
        mask = 0

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
        state_row[ch] = dict(ch_s)
        state_row[ch].setdefault("pressed", False)
        state_row[ch].setdefault("baseline", 0)

    with _lock:
        _raw_history.append(sample)
        _latest_sample_t = float(sample.timestamp)
        _latest_state = state_row
        _latest_actuator_mask = mask
        if not _paused:
            _batch.append(row)
            _batch_states.append(state_row)


def _feed_loop():
    """Background thread: read samples, run detection, batch for WebSocket.

    For serial sources, automatically reconnects when the device is lost.
    When the actuator shares the data port, it is re-attached to the new
    serial handle on each reconnect.
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

            # If the actuator was sharing this handle, drop it so writes don't fault.
            if _actuator is not None and _actuator.is_using_port(_serial_port):
                _actuator.detach()

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

                # If the user wants the actuator on this same port, re-share.
                if _initial_actuator_port == _serial_port and _actuator is not None:
                    _actuator.attach_shared(_serial_port, new_ser)

                log.info("Serial reconnected on %s", _serial_port)
                _push_serial_status(True)
                break

        except StopIteration:
            return


async def _broadcast_loop():
    global _batch, _batch_states, _pending_status_msgs

    while True:
        await asyncio.sleep(BATCH_INTERVAL)

        with _lock:
            status_msgs = _pending_status_msgs
            _pending_status_msgs = []

            has_data = bool(_batch)
            payload = _batch
            states = _batch_states
            act_mask = _latest_actuator_mask
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
            msg = json.dumps({"type": "data", "samples": payload, "state": states,
                              "actuator_mask": act_mask})
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


# --- Overlays --------------------------------------------------------------

_BIT_STRUM_DOWN = 1 << 5
_BIT_STRUM_UP = 1 << 6

_OVERLAY_CH_COLORS = {
    "green":  (60, 220, 60),
    "red":    (60, 60, 230),
    "yellow": (40, 220, 220),
    "blue":   (230, 130, 50),
    "orange": (50, 150, 240),
}


def _draw_default_overlays(image, sample_t: float, state: dict, mask: int) -> None:
    if image is None:
        return
    h, w = image.shape[:2]

    n = len(CHANNELS)
    pad = 12
    radius = max(10, min(28, w // 60))
    spacing = (w - pad * 2) // (n + 1)
    y = h - radius - 16
    for i, ch in enumerate(CHANNELS):
        x = pad + spacing * (i + 1)
        ch_state = (state or {}).get(ch, {}) or {}
        pressed = bool(ch_state.get("pressed"))
        color = _OVERLAY_CH_COLORS[ch]
        if pressed:
            cv2.circle(image, (x, y), radius, color, -1)
            cv2.circle(image, (x, y), radius, (255, 255, 255), 2)
        else:
            cv2.circle(image, (x, y), radius, color, 2)
        label = ch[0].upper()
        ts = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 2)[0]
        cv2.putText(
            image, label,
            (x - ts[0] // 2, y + ts[1] // 2),
            cv2.FONT_HERSHEY_SIMPLEX, 0.6,
            (255, 255, 255) if pressed else color, 2,
        )

    sd_on = bool(mask & _BIT_STRUM_DOWN)
    su_on = bool(mask & _BIT_STRUM_UP)
    box_w = 70
    box_h = 26
    bx = w - box_w - 12
    by_down = y - box_h - 8
    by_up = by_down - (box_h + 6)
    for label, on, by in (("STRUM v", sd_on, by_down), ("STRUM ^", su_on, by_up)):
        bg = (40, 40, 200) if on else (60, 60, 60)
        cv2.rectangle(image, (bx, by), (bx + box_w, by + box_h), bg, -1)
        cv2.rectangle(image, (bx, by), (bx + box_w, by + box_h), (220, 220, 220), 1)
        cv2.putText(
            image, label, (bx + 6, by + box_h - 8),
            cv2.FONT_HERSHEY_SIMPLEX, 0.5,
            (255, 255, 255) if on else (200, 200, 200), 1,
        )

    text = f"t = {sample_t:7.3f}s"
    cv2.rectangle(image, (8, 8), (8 + 168, 8 + 28), (0, 0, 0), -1)
    cv2.putText(
        image, text, (14, 30),
        cv2.FONT_HERSHEY_SIMPLEX, 0.65, (233, 69, 96), 2,
    )


def _maybe_apply_overlays(image, det) -> None:
    if det is None or image is None or not hasattr(det, "overlays"):
        return
    try:
        layers = det.overlays() or []
    except Exception as exc:
        log.warning("detector overlays() raised: %s", exc)
        return
    for layer in layers:
        try:
            layer.get("draw")(image)
        except Exception as exc:
            log.warning("overlay layer raised: %s", exc)


def _compose_frame_jpeg(frame, det, scrub_t: Optional[float] = None) -> Optional[bytes]:
    if frame is None:
        return None
    img = frame.image.copy()
    with _lock:
        state = dict(_latest_state)
        mask = _latest_actuator_mask
        live_t = _latest_sample_t
    t = scrub_t if scrub_t is not None else (frame.t if frame.t else live_t)
    _draw_default_overlays(img, t, state, mask)
    _maybe_apply_overlays(img, det)
    ok, buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 80])
    if not ok:
        return None
    return buf.tobytes()


# --- lifecycle -------------------------------------------------------------


def _attach_initial_actuator() -> None:
    if _actuator is None or _initial_actuator_port is None:
        return
    if _serial_port and _initial_actuator_port == _serial_port and _ser_obj is not None:
        _actuator.attach_shared(_serial_port, _ser_obj)
    else:
        _actuator.attach(_initial_actuator_port)


@app.on_event("startup")
async def startup():
    global _main_loop, _view_encoder
    _main_loop = asyncio.get_running_loop()

    cam = get_camera()
    cam.set_sample_clock(lambda: _latest_sample_t)

    sources = enumerate_cameras()
    target_id: Optional[int] = None
    target_label: Optional[str] = None
    if _initial_camera_id is not None:
        target_id = int(_initial_camera_id)
        for s in sources:
            if s["id"] == target_id:
                target_label = s["label"]
                break
    elif sources:
        target_id = int(sources[0]["id"])
        target_label = sources[0]["label"]

    if target_id is not None:
        try:
            cam.open(target_id, target_label)
        except Exception as exc:
            log.warning("startup: failed to open camera %s: %s", target_id, exc)

    _attach_initial_actuator()

    _view_encoder = _ViewEncoder()
    _view_encoder.start()

    t = threading.Thread(target=_feed_loop, daemon=True)
    t.start()
    asyncio.create_task(_broadcast_loop())


@app.on_event("shutdown")
async def shutdown():
    if _view_encoder is not None:
        try:
            _view_encoder.stop()
        except Exception:
            pass
    try:
        get_camera().close()
    except Exception:
        pass
    if _actuator is not None:
        try:
            _actuator.detach()
        except Exception:
            pass


# --- HTTP routes -----------------------------------------------------------


@app.get("/")
async def index():
    return FileResponse(Path(__file__).parent / "index.html")


@app.get("/api/detectors")
async def list_detectors():
    return JSONResponse(_discover_detectors())


@app.get("/api/sources")
async def list_sources():
    return JSONResponse(_sources_msg())


@app.get("/api/serial-ports")
async def list_serial_ports():
    return JSONResponse(enumerate_serial_ports())


# --- View pipeline (low priority) -----------------------------------------
#
# The browser-facing view is intentionally decoupled from capture and detection:
# a single dedicated encoder thread snaps the latest buffered frame at a
# throttled rate (VIEW_FPS), runs overlays + JPEG encode, and caches the
# result. All MJPEG clients share the cached JPEG so multiple browsers don't
# multiply CPU. /video/snap (scrub) is a one-shot encode dispatched to a
# worker thread so it never blocks the asyncio event loop either.

VIEW_FPS = 15  # cap for the live MJPEG stream; capture+detection still run full-rate


class _ViewEncoder:
    """Throttled, single-threaded JPEG encoder for the live view."""

    def __init__(self, target_fps: float = VIEW_FPS) -> None:
        self._interval = 1.0 / float(max(1, target_fps))
        self._cv = threading.Condition()
        self._latest_jpeg: Optional[bytes] = None
        self._latest_seq: int = -1
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run, name="view-encoder", daemon=True,
        )

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def _run(self) -> None:
        cam = get_camera()
        last_emit = 0.0
        last_seq = -1
        while not self._stop.is_set():
            cam.wait_for_frame(self._interval)
            now = time.monotonic()
            wait = self._interval - (now - last_emit)
            if wait > 0:
                if self._stop.wait(wait):
                    return
            frame = cam.latest()
            if frame is None or frame.seq == last_seq:
                continue
            with _lock:
                det = _detector
            try:
                jpeg = _compose_frame_jpeg(frame, det)
            except Exception as exc:
                log.warning("view-encoder: compose failed: %s", exc)
                jpeg = None
            if jpeg is None:
                continue
            with self._cv:
                self._latest_jpeg = jpeg
                self._latest_seq = frame.seq
                self._cv.notify_all()
            last_seq = frame.seq
            last_emit = time.monotonic()

    def get_after(self, last_seq: int, timeout: float = 0.5) -> tuple[Optional[bytes], int]:
        """Block until a JPEG with seq > last_seq is available, or timeout."""
        with self._cv:
            if self._latest_seq <= last_seq:
                self._cv.wait(timeout)
            return self._latest_jpeg, self._latest_seq


_view_encoder: Optional[_ViewEncoder] = None


async def _mjpeg_generator():
    enc = _view_encoder
    last_seq = -1
    while True:
        if enc is None:
            await asyncio.sleep(0.1)
            continue
        jpeg, seq = await asyncio.to_thread(enc.get_after, last_seq, 0.5)
        if jpeg is None or seq == last_seq:
            continue
        last_seq = seq
        yield (b"--frame\r\n"
               b"Content-Type: image/jpeg\r\n\r\n" + jpeg + b"\r\n")


@app.get("/video")
async def video_feed():
    return StreamingResponse(
        _mjpeg_generator(),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )


def _snap_jpeg(t: Optional[float]) -> Optional[bytes]:
    cam = get_camera()
    frame = cam.latest() if t is None else cam.closest(float(t))
    if frame is None:
        return None
    with _lock:
        det = _detector
    return _compose_frame_jpeg(frame, det, scrub_t=t)


@app.get("/video/snap")
async def video_snap(t: Optional[float] = None):
    jpeg = await asyncio.to_thread(_snap_jpeg, t)
    if jpeg is None:
        return Response(content=b"no frame available", status_code=503)
    return Response(content=jpeg, media_type="image/jpeg",
                    headers={"Cache-Control": "no-store"})


# --- WebSocket -------------------------------------------------------------


@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    global _detector, _detector_name, _detector_params, _paused

    await ws.accept()
    _ws_clients.add(ws)

    try:
        await ws.send_text(json.dumps(_detector_info()))
        await ws.send_text(json.dumps(_sources_msg()))
        await ws.send_text(json.dumps(_actuator_status_msg()))

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
                    old_det = _detector
                    _detector_params.update(
                        {k: int(v) for k, v in new_params.items()}
                    )
                    _detector, _detector_params = _load_detector(
                        _detector_name, _detector_params
                    )
                _stop_detector(old_det)
                time.sleep(0.5)
                await ws.send_text(json.dumps(_detector_info()))

            elif msg_type == "set_detector":
                name = msg.get("name", _detector_name)
                with _lock:
                    if name == _detector_name and _detector is not None:
                        await ws.send_text(json.dumps(_detector_info()))
                        continue
                    old_det = _detector
                    _detector_name = name
                    _detector, _detector_params = _load_detector(name)
                _stop_detector(old_det)
                time.sleep(0.5)
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

            elif msg_type == "set_source":
                name = msg.get("name")
                cam = get_camera()
                if name in (None, "", "none"):
                    cam.close()
                else:
                    try:
                        dev_id = int(name)
                    except (TypeError, ValueError):
                        dev_id = None
                    if dev_id is not None:
                        label = None
                        for s in enumerate_cameras():
                            if s["id"] == dev_id:
                                label = s["label"]
                                break
                        cam.open(dev_id, label)
                msg_out = json.dumps(_sources_msg())
                for client in list(_ws_clients):
                    try:
                        await client.send_text(msg_out)
                    except Exception:
                        pass

            elif msg_type == "set_actuator_port":
                name = msg.get("name")
                if _actuator is not None:
                    if name in (None, "", "none"):
                        _actuator.detach()
                    else:
                        if (_serial_port and name == _serial_port
                                and _ser_obj is not None and _ser_obj.is_open):
                            _actuator.attach_shared(_serial_port, _ser_obj)
                        else:
                            _actuator.attach(str(name))
                # The status listener will broadcast the updated state.

            elif msg_type == "manual_output":
                mask = int(msg.get("mask", 0)) & 0x7F
                if _actuator is not None and not _actuator.enabled:
                    _actuator.write_manual(mask)

    except WebSocketDisconnect:
        pass
    finally:
        _ws_clients.discard(ws)
