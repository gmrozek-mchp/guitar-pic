"""
Server-side camera capture and shared video ring buffer.

Owns a single physical camera at a time, captures frames into a ring buffer
sized for >= 30 seconds of coverage, and exposes per-time frame access.
Frames are tagged with a sample-clock timestamp via a caller-supplied
callback so video and ADC streams share a timeline.

Detectors that need pixel data (e.g. detect_video) register a frame
listener; the server reads `latest()` for live MJPEG and `closest(t)`
for paused scrub.
"""

from __future__ import annotations

import glob
import json
import logging
import os
import subprocess
import sys
import threading
import time
from collections import deque
from typing import Callable, Optional

import cv2
import numpy as np


log = logging.getLogger("fret-tuner.camera")

BUFFER_SECONDS = 30.0
# Buffer depth is sized for the realistic worst-case capture rate.
# Real cameras top out at 30 Hz; raising this just inflates RAM (each frame
# is a raw uncompressed numpy array -- ~6 MB at 1080p, ~900 KB at 480p).
MAX_FPS = 30
BUFFER_CAP = int(BUFFER_SECONDS * MAX_FPS)

CAPTURE_WIDTH = 1920
CAPTURE_HEIGHT = 1080

# Frames are downscaled to this width before being shared with detectors and
# the view buffer. Detection target coordinates scale automatically
# (detect_video uses BASE_W=640), and the view canvas in the browser doesn't
# need more than this. Keeping the working resolution low is the single
# biggest knob for buffer RAM and per-frame CPU.
WORK_MAX_WIDTH = 960


class Frame:
    """A single captured frame with its sample-clock timestamp.

    `image` is the downscaled working/buffered frame and is always present.
    `raw_image` is the original full-resolution frame from the capture device.
    It is only populated for the duration of the capture-thread listener
    dispatch (so detectors can opt into full-res pixel data); it is cleared
    before the frame is consumed from the ring buffer, so buffered/scrubbed
    frames never carry the full-res payload.
    """

    __slots__ = ("t", "seq", "image", "raw_image")

    def __init__(
        self,
        t: float,
        seq: int,
        image: np.ndarray,
        raw_image: Optional[np.ndarray] = None,
    ) -> None:
        self.t = t
        self.seq = seq
        self.image = image
        self.raw_image = raw_image

    def detect_image(self) -> np.ndarray:
        """Best image for a detector: full-res raw if available, else working."""
        return self.raw_image if self.raw_image is not None else self.image


class Camera:
    """Owns the active capture device and the shared ring buffer."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._buffer: deque[Frame] = deque(maxlen=BUFFER_CAP)
        self._cap: Optional[cv2.VideoCapture] = None
        self._device_id: Optional[int] = None
        self._device_label: Optional[str] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False
        self._sample_clock_fn: Callable[[], float] = lambda: 0.0
        self._frame_event = threading.Event()
        self._seq = 0
        self._jpeg_cache: Optional[bytes] = None
        self._jpeg_cache_seq: int = -1
        self._frame_listeners: list[Callable[[Frame], None]] = []

    def set_sample_clock(self, fn: Callable[[], float]) -> None:
        """Register a callback returning the current sample-clock time."""
        self._sample_clock_fn = fn

    def add_frame_listener(self, fn: Callable[[Frame], None]) -> None:
        """Register a callback invoked for every captured frame."""
        with self._lock:
            if fn not in self._frame_listeners:
                self._frame_listeners.append(fn)

    def remove_frame_listener(self, fn: Callable[[Frame], None]) -> None:
        with self._lock:
            try:
                self._frame_listeners.remove(fn)
            except ValueError:
                pass

    def open(self, device_id: int, label: Optional[str] = None) -> bool:
        """Switch to a new device. Closes any current capture first."""
        self.close()
        cap = cv2.VideoCapture(device_id)
        if not cap.isOpened():
            log.warning("camera: cannot open device %s", device_id)
            cap.release()
            return False
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAPTURE_WIDTH)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAPTURE_HEIGHT)

        with self._lock:
            self._cap = cap
            self._device_id = device_id
            self._device_label = label or f"Camera {device_id}"
            self._buffer.clear()
            self._jpeg_cache = None
            self._jpeg_cache_seq = -1
            self._seq = 0

        self._running = True
        self._thread = threading.Thread(
            target=self._capture_loop, name=f"camera-{device_id}", daemon=True,
        )
        self._thread.start()
        log.info("camera: opened device %s (%s)", device_id, self._device_label)
        return True

    def close(self) -> None:
        """Stop capture and release the device."""
        self._running = False
        thread = self._thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=2.0)
        with self._lock:
            if self._cap is not None:
                try:
                    self._cap.release()
                except Exception:
                    pass
                self._cap = None
            self._device_id = None
            self._device_label = None
            self._buffer.clear()
            self._jpeg_cache = None
            self._jpeg_cache_seq = -1
        self._thread = None

    def latest(self) -> Optional[Frame]:
        with self._lock:
            return self._buffer[-1] if self._buffer else None

    def closest(self, t: float) -> Optional[Frame]:
        """Return the buffered frame whose timestamp is nearest to `t`."""
        with self._lock:
            buf = list(self._buffer)
        if not buf:
            return None
        return min(buf, key=lambda f: abs(f.t - t))

    def encode_jpeg(self, frame: Frame, quality: int = 80) -> Optional[bytes]:
        """Encode `frame.image` as JPEG bytes. Cached by frame sequence."""
        with self._lock:
            if frame.seq == self._jpeg_cache_seq and self._jpeg_cache is not None:
                return self._jpeg_cache
        ok, buf = cv2.imencode(".jpg", frame.image, [cv2.IMWRITE_JPEG_QUALITY, quality])
        if not ok:
            return None
        jpeg = buf.tobytes()
        with self._lock:
            self._jpeg_cache = jpeg
            self._jpeg_cache_seq = frame.seq
        return jpeg

    def wait_for_frame(self, timeout: float = 0.1) -> bool:
        got = self._frame_event.wait(timeout)
        if got:
            self._frame_event.clear()
        return got

    def status(self) -> dict:
        with self._lock:
            return {
                "active": self._device_id is not None,
                "device_id": self._device_id,
                "label": self._device_label,
                "buffered": len(self._buffer),
            }

    def _capture_loop(self) -> None:
        logged_size = False
        while self._running:
            with self._lock:
                cap = self._cap
            if cap is None:
                break

            try:
                ret, frame = cap.read()
            except Exception as exc:
                log.warning("camera: read error: %s", exc)
                time.sleep(0.05)
                continue

            if not ret or frame is None:
                time.sleep(0.01)
                continue

            raw_h, raw_w = frame.shape[:2]
            raw_image = frame
            if raw_w > WORK_MAX_WIDTH:
                scale = WORK_MAX_WIDTH / float(raw_w)
                work_image = cv2.resize(
                    frame,
                    (WORK_MAX_WIDTH, int(round(raw_h * scale))),
                    interpolation=cv2.INTER_AREA,
                )
            else:
                work_image = frame
                raw_image = None  # nothing extra to expose

            if not logged_size:
                h, w = work_image.shape[:2]
                bytes_per_frame = w * h * (work_image.shape[2] if work_image.ndim == 3 else 1)
                buf_mb = (BUFFER_CAP * bytes_per_frame) / (1024 * 1024)
                detect_res = f"{raw_w}x{raw_h}" if raw_image is not None else f"{w}x{h}"
                msg = (
                    f"camera: stream started {raw_w}x{raw_h} "
                    f"-> working {w}x{h} -- "
                    f"buffer max {BUFFER_CAP} frames ~ {buf_mb:.0f} MB "
                    f"(detection runs at {detect_res})"
                )
                log.info(msg)
                print(msg, flush=True)
                logged_size = True

            t = float(self._sample_clock_fn())
            with self._lock:
                self._seq += 1
                f = Frame(
                    t=t, seq=self._seq, image=work_image, raw_image=raw_image,
                )
                self._buffer.append(f)
                listeners = list(self._frame_listeners)

            self._frame_event.set()

            try:
                for listener in listeners:
                    try:
                        listener(f)
                    except Exception as exc:
                        log.warning("camera: frame listener raised: %s", exc)
            finally:
                # Drop the full-res reference so buffered/scrubbed frames stay
                # at the working resolution and don't pin large arrays in RAM.
                f.raw_image = None


_camera: Optional[Camera] = None


def get_camera() -> Camera:
    """Module-level singleton accessor."""
    global _camera
    if _camera is None:
        _camera = Camera()
    return _camera


# --- Source enumeration -----------------------------------------------------


def enumerate_cameras() -> list[dict]:
    """Return [{"id": int, "label": str}, ...] for available cameras."""
    if sys.platform == "darwin":
        result = _enumerate_macos()
    elif sys.platform.startswith("linux"):
        result = _enumerate_linux()
    else:
        result = []
    if not result:
        result = _enumerate_probe()
    return result


def _enumerate_macos() -> list[dict]:
    try:
        out = subprocess.check_output(
            ["system_profiler", "SPCameraDataType", "-json"],
            timeout=5,
            stderr=subprocess.DEVNULL,
        )
    except (subprocess.SubprocessError, FileNotFoundError, OSError) as exc:
        log.debug("camera: system_profiler failed: %s", exc)
        return []
    try:
        data = json.loads(out)
    except json.JSONDecodeError:
        return []
    cams = data.get("SPCameraDataType", []) or []
    result: list[dict] = []
    for i, c in enumerate(cams):
        label = c.get("_name") or c.get("spcamera_unique-id") or f"Camera {i}"
        result.append({"id": i, "label": str(label)})
    return result


def _enumerate_linux() -> list[dict]:
    result: list[dict] = []
    for path in sorted(glob.glob("/sys/class/video4linux/video*")):
        name_path = os.path.join(path, "name")
        try:
            with open(name_path) as f:
                label = f.read().strip()
        except OSError:
            continue
        try:
            idx = int(os.path.basename(path).removeprefix("video"))
        except ValueError:
            continue
        result.append({"id": idx, "label": label or f"Camera {idx}"})
    return result


def _enumerate_probe(max_index: int = 8) -> list[dict]:
    """Fallback: open device indices and report which respond."""
    result: list[dict] = []
    for i in range(max_index):
        cap = cv2.VideoCapture(i)
        opened = cap.isOpened()
        cap.release()
        if opened:
            result.append({"id": i, "label": f"Camera {i}"})
    return result
