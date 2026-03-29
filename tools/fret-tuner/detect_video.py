"""
Video-based fret detection -- camera pointed at the game screen.

Two sensor points per button, both above the strike line:

  Sensor 1 (hold)  -- brightness detect (color OR white).
                      Sets pressed=True whenever anything bright appears.
  Sensor 2 (edge)  -- color-filtered leading-edge detect.
                      Increments press_count on each new note arrival,
                      which the actuator uses to schedule strums.

Use video_calibrate.py to find pixel coordinates for all 10 sensor
points.  Detection thresholds are applied to raw pixel values (dark
background is the implicit zero).

The "baseline" output is the stronger of the two signals (scaled to
0..4095) for chart overlay visualization.

When SHOW_PREVIEW=1 (default), the annotated camera feed is served at
http://localhost:8080/video as an MJPEG stream.

Requires: opencv-python
"""

import sys
import threading
import time
from typing import Optional

import cv2
import numpy as np

from stream import Sample, CHANNELS

_DIST_SCALE = 4095.0 / 255.0  # map max single-channel distance (255) to 0..4095

# Per-button color filter for the edge detector (sensor 2).
# (target_weights, reject_weights) in BGR order.
# Detection signal = target - reject.  Reject weights > 1.0 total
# so that camera-captured whites (which have color-temperature bias)
# are aggressively suppressed, not just mathematically-perfect whites.
_COLOR_FILTER: dict[str, tuple[tuple[float, ...], tuple[float, ...]]] = {
    "green":  ((0, 1, 0),     (0.7, 0, 0.7)),   # want G up, penalize B/R
    "red":    ((0, 0, 1),     (0.7, 0.7, 0)),    # want R up, penalize B/G
    "yellow": ((0, 0.5, 0.5), (1.4, 0, 0)),      # want R+G up, penalize B
    "blue":   ((1, 0.4, 0),   (0, 0, 1.4)),       # want B + some G, penalize R
    "orange": ((0, 0.3, 0.7), (1.4, 0, 0)),      # want R+someG up, penalize B
}

_MARKER_COLORS = {
    "green":  (0, 200, 0),
    "red":    (0, 0, 220),
    "yellow": (0, 220, 220),
    "blue":   (220, 0, 0),
    "orange": (0, 140, 255),
}
_IDLE_COLOR = (120, 120, 120)
_PRESSED_COLOR = (0, 255, 0)

_MARKER_RADIUS = 16
_MARKER_THICK = 3
_FONT_SCALE = 0.6
_FONT_THICK = 2


class Detector:
    """Camera-based fret press detector."""

    @classmethod
    def default_params(cls) -> dict:
        return {
            "DEVICE_ID": 0,
            "HOLD_THRESH": 50,
            "HOLD_RELEASE_FRAC": 60,
            "EDGE_GREEN": 50,
            "EDGE_RED": 50,
            "EDGE_YELLOW": 50,
            "EDGE_BLUE": 50,
            "EDGE_ORANGE": 50,
            "PATCH_RADIUS": 2,
            "SHOW_PREVIEW": 1,
            "GREEN_X": 748, "GREEN_Y": 700,
            "RED_X": 850, "RED_Y": 700,
            "YELLOW_X": 947, "YELLOW_Y": 700,
            "BLUE_X": 1045, "BLUE_Y": 700,
            "ORANGE_X": 1147, "ORANGE_Y": 700,
            "GREEN_EX": 780, "GREEN_EY": 700,
            "RED_EX": 882, "RED_EY": 700,
            "YELLOW_EX": 979, "YELLOW_EY": 700,
            "BLUE_EX": 1013, "BLUE_EY": 700,
            "ORANGE_EX": 1115, "ORANGE_EY": 700,
        }

    def __init__(self, **params):
        defaults = self.default_params()
        defaults.update(params)

        self._hold_thresh = int(defaults["HOLD_THRESH"])
        self._hold_release = self._hold_thresh * int(defaults["HOLD_RELEASE_FRAC"]) // 100
        self._edge_thresh: dict[str, int] = {
            ch: int(defaults[f"EDGE_{ch.upper()}"]) for ch in CHANNELS
        }
        self._patch_r = max(0, int(defaults["PATCH_RADIUS"]))
        self._show_preview = bool(int(defaults["SHOW_PREVIEW"]))

        self._cal_hold_pixels: dict[str, tuple[int, int]] = {}
        self._cal_edge_pixels: dict[str, tuple[int, int]] = {}
        for ch in CHANNELS:
            p = ch.upper()
            self._cal_hold_pixels[ch] = (int(defaults[f"{p}_X"]), int(defaults[f"{p}_Y"]))
            self._cal_edge_pixels[ch] = (int(defaults[f"{p}_EX"]), int(defaults[f"{p}_EY"]))

        self._hold_pixels: dict[str, tuple[int, int]] = dict(self._cal_hold_pixels)
        self._edge_pixels: dict[str, tuple[int, int]] = dict(self._cal_edge_pixels)

        self._hold_dist: dict[str, float] = {ch: 0.0 for ch in CHANNELS}
        self._edge_dist: dict[str, float] = {ch: 0.0 for ch in CHANNELS}
        self._pressed: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._press_count: dict[str, int] = {ch: 0 for ch in CHANNELS}
        self._edge_active: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._cached_result: Optional[dict] = None

        self._lock = threading.Lock()
        self._frame: Optional[np.ndarray] = None
        self._preview_jpeg: Optional[bytes] = None
        self._frame_seq = 0
        self._preview_seq = -1
        self._frame_event = threading.Event()
        self._cam_ok = False
        self._running = True

        dev = int(defaults["DEVICE_ID"])
        self._thread = threading.Thread(
            target=self._capture_loop, args=(dev,), daemon=True
        )
        self._thread.start()

    def stop(self):
        """Stop the camera capture thread and release resources."""
        self._running = False
        if self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._log("stopped")

    def __del__(self):
        self._running = False

    def _log(self, msg: str):
        print(f"[detect_video] {msg}", flush=True)

    _EDGE_RADIUS = 10
    _EDGE_THICK = 2

    def _encode_preview(self, frame: np.ndarray):
        """Annotate frame with hold and edge detection markers."""
        display = frame.copy()

        for ch in CHANNELS:
            hx, hy = self._hold_pixels[ch]
            ex, ey = self._edge_pixels[ch]
            r = self._patch_r

            with self._lock:
                pressed = self._pressed[ch]
                hold_d = self._hold_dist[ch]
                edge_d = self._edge_dist[ch]
                edge_on = self._edge_active[ch]

            color = _MARKER_COLORS[ch]
            hold_ring = _PRESSED_COLOR if pressed else _IDLE_COLOR

            cv2.rectangle(display, (hx - r, hy - r), (hx + r, hy + r), color, 1)
            cv2.circle(display, (hx, hy), _MARKER_RADIUS, hold_ring, _MARKER_THICK)

            cv2.line(display, (hx, hy), (ex, ey), color, 1)
            edge_ring = _PRESSED_COLOR if edge_on else _IDLE_COLOR
            cv2.rectangle(display, (ex - r, ey - r), (ex + r, ey + r), color, 1)
            cv2.circle(display, (ex, ey), self._EDGE_RADIUS, edge_ring, self._EDGE_THICK)

            label = f"{ch[0].upper()} h{hold_d:.0f} e{edge_d:.0f}"
            cv2.putText(display, label, (hx + _MARKER_RADIUS + 4, hy + 5),
                        cv2.FONT_HERSHEY_SIMPLEX, _FONT_SCALE, color, _FONT_THICK)

        h, w = display.shape[:2]
        cv2.putText(display, f"{w}x{h}", (10, h - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)

        ok, buf = cv2.imencode(".jpg", display, [cv2.IMWRITE_JPEG_QUALITY, 80])
        if ok:
            with self._lock:
                self._preview_jpeg = buf.tobytes()

    def get_preview_jpeg(self) -> Optional[bytes]:
        """Return the latest annotated frame as JPEG bytes, encoding lazily."""
        if not self._show_preview:
            return None
        with self._lock:
            frame = self._frame
            seq = self._frame_seq
        if frame is None:
            return None
        if seq != self._preview_seq:
            self._encode_preview(frame)
            self._preview_seq = seq
        with self._lock:
            return self._preview_jpeg

    def wait_for_frame(self, timeout: float = 0.1) -> bool:
        """Block until a new preview frame is ready. Returns True if a frame arrived."""
        got = self._frame_event.wait(timeout)
        if got:
            self._frame_event.clear()
        return got

    def _scale_pixels(self, frame_w: int, frame_h: int):
        """Scale calibrated pixel coords to match actual camera resolution."""
        all_cal = list(self._cal_hold_pixels.values()) + list(self._cal_edge_pixels.values())
        cal_xs = [x for x, _ in all_cal]
        cal_ys = [y for _, y in all_cal]
        cal_w = max(cal_xs) + max(cal_xs) // 4
        cal_h = max(cal_ys) + max(cal_ys) // 4

        if frame_w < cal_w or frame_h < cal_h:
            sx = frame_w / 1920.0
            sy = frame_h / 1080.0
            self._log(f"scaling coords by {sx:.3f}x{sy:.3f}")
            for ch in CHANNELS:
                hx, hy = self._cal_hold_pixels[ch]
                self._hold_pixels[ch] = (int(hx * sx), int(hy * sy))
                ex, ey = self._cal_edge_pixels[ch]
                self._edge_pixels[ch] = (int(ex * sx), int(ey * sy))
        else:
            self._hold_pixels = dict(self._cal_hold_pixels)
            self._edge_pixels = dict(self._cal_edge_pixels)

    def _capture_loop(self, device_id: int):
        cap = cv2.VideoCapture(device_id)
        if not cap.isOpened():
            self._log("ERROR: cannot open camera")
            return
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
        with self._lock:
            self._cam_ok = True
        started = False
        try:
            while self._running:
                ret, frame = cap.read()
                if ret:
                    if not started:
                        h, w = frame.shape[:2]
                        self._log(f"camera opened: {w}x{h}")
                        self._scale_pixels(w, h)
                        for ch in CHANNELS:
                            hx, hy = self._hold_pixels[ch]
                            ex, ey = self._edge_pixels[ch]
                            self._log(f"  {ch}: hold({hx},{hy}) edge({ex},{ey})")
                        sys.stdout.flush()
                        started = True
                    self._detect(frame)
                    with self._lock:
                        self._frame = frame
                        self._frame_seq += 1
                    self._frame_event.set()
                else:
                    time.sleep(0.01)
        finally:
            cap.release()

    def _sample_at(self, frame: np.ndarray, px: int, py: int) -> np.ndarray:
        """Average a small patch around (px, py)."""
        h, w = frame.shape[:2]
        px = max(0, min(px, w - 1))
        py = max(0, min(py, h - 1))
        r = self._patch_r
        y0, y1 = max(0, py - r), min(h, py + r + 1)
        x0, x1 = max(0, px - r), min(w, px + r + 1)
        patch = frame[y0:y1, x0:x1]
        if patch.size == 0:
            return np.zeros(3, dtype=np.float64)
        return patch.astype(np.float64).mean(axis=(0, 1))

    @staticmethod
    def _brightness(color: np.ndarray) -> float:
        """Max channel value -- triggers on any color or white."""
        d = float(max(0.0, color[0], color[1], color[2]))
        return 0.0 if np.isnan(d) else d

    def _color_signal(self, color: np.ndarray, ch: str) -> float:
        """Color-filtered signal -- only the button's own color triggers.

        Scales by saturation ratio (max-min)/max so white/gray pixels
        produce near-zero signal regardless of camera white balance.
        """
        cmax = float(max(color[0], color[1], color[2]))
        if cmax < 1.0:
            return 0.0
        cmin = float(min(color[0], color[1], color[2]))
        sat_ratio = (cmax - cmin) / cmax

        tw, rw = _COLOR_FILTER[ch]
        target = tw[0]*color[0] + tw[1]*color[1] + tw[2]*color[2]
        reject = max(0.0, rw[0]*color[0] + rw[1]*color[1] + rw[2]*color[2])
        d = float((target - reject) * sat_ratio)
        return 0.0 if np.isnan(d) else d

    def _detect(self, frame: np.ndarray):
        """Run detection on a new camera frame. Called from capture thread."""
        result: dict[str, dict] = {}
        for ch in CHANNELS:
            hx, hy = self._hold_pixels[ch]
            ex, ey = self._edge_pixels[ch]
            hold_color = self._sample_at(frame, hx, hy)
            edge_color = self._sample_at(frame, ex, ey)

            hold_dist = self._brightness(hold_color)
            edge_dist = self._color_signal(edge_color, ch)
            self._hold_dist[ch] = hold_dist
            self._edge_dist[ch] = edge_dist

            if not self._pressed[ch]:
                if hold_dist > self._hold_thresh:
                    self._pressed[ch] = True
            else:
                if hold_dist < self._hold_release:
                    self._pressed[ch] = False

            edge_on = edge_dist > self._edge_thresh[ch]
            if edge_on and not self._edge_active[ch]:
                self._press_count[ch] += 1
            self._edge_active[ch] = edge_on

            result[ch] = {
                "pressed": self._pressed[ch],
                "baseline": min(4095, int(hold_dist * _DIST_SCALE)),
                "edge_line": min(4095, int(edge_dist * _DIST_SCALE)),
                "press_count": self._press_count[ch],
            }

        with self._lock:
            self._cached_result = result

    def update(self, sample: Sample) -> dict:
        """Return the latest detection result (computed in capture thread)."""
        with self._lock:
            cached = self._cached_result
            cam_ok = self._cam_ok

        if cached is not None:
            return cached

        return {ch: {
            "pressed": False, "baseline": 0, "edge_line": 0,
            "press_count": 0, "camera": "waiting" if cam_ok else "no_device",
        } for ch in CHANNELS}
