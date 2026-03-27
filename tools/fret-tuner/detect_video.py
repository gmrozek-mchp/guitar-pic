"""
Video-based fret press detection -- camera pointed at the guitar controller.

Point a camera at the fret buttons, configure pixel coordinates for each
button (use video_calibrate.py to find them), and the detector auto-captures
reference colors from the first few frames.  When the button's own color
channel increases from its reference by more than THRESHOLD, that button
is pressed (e.g. red channel for the red button, green for green, etc.).

This detector ignores ADC sample values -- it uses the sample timestamp
only.  The "baseline" output is the color distance (scaled to ADC range)
for chart overlay visualization, so you can see video-detected presses
overlaid on the raw ADC waveforms.

When SHOW_PREVIEW=1 (default), the annotated camera feed is served at
http://localhost:8080/video as an MJPEG stream.  Close the browser camera
panel first so OpenCV gets full access to the camera.

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

# Per-button color filter: (target_weights, reject_weights) in BGR order.
# target = weighted sum of increase in the button's own color channels.
# reject = weighted sum of increase in off-color channels.
# Detection signal = target - reject.  White light increases all channels
# equally, so target - reject ≈ 0 and won't trigger.
_COLOR_FILTER: dict[str, tuple[tuple[float, ...], tuple[float, ...]]] = {
    "green":  ((0, 1, 0),     (0.5, 0, 0.5)),   # want G up, penalize B/R
    "red":    ((0, 0, 1),     (0.5, 0.5, 0)),    # want R up, penalize B/G
    "yellow": ((0, 0.5, 0.5), (1, 0, 0)),        # want R+G up, penalize B
    "blue":   ((1, 0, 0),     (0, 0.5, 0.5)),    # want B up, penalize G/R
    "orange": ((0, 0.3, 0.7), (1, 0, 0)),        # want R+someG up, penalize B
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
            "THRESHOLD": 50,
            "RELEASE_FRAC": 60,
            "SETTLE_FRAMES": 30,
            "PATCH_RADIUS": 2,
            "SHOW_PREVIEW": 1,
            "GREEN_X": 737, "GREEN_Y": 708,
            "RED_X": 841, "RED_Y": 712,
            "YELLOW_X": 948, "YELLOW_Y": 706,
            "BLUE_X": 1054, "BLUE_Y": 704,
            "ORANGE_X": 1164, "ORANGE_Y": 712,
        }

    def __init__(self, **params):
        defaults = self.default_params()
        defaults.update(params)

        self._threshold = int(defaults["THRESHOLD"])
        self._release_thresh = self._threshold * int(defaults["RELEASE_FRAC"]) // 100
        self._settle_total = int(defaults["SETTLE_FRAMES"])
        self._patch_r = max(0, int(defaults["PATCH_RADIUS"]))
        self._show_preview = bool(int(defaults["SHOW_PREVIEW"]))

        self._cal_pixels: dict[str, tuple[int, int]] = {}
        for ch in CHANNELS:
            p = ch.upper()
            self._cal_pixels[ch] = (int(defaults[f"{p}_X"]), int(defaults[f"{p}_Y"]))

        self._pixels: dict[str, tuple[int, int]] = dict(self._cal_pixels)

        self._ref: dict[str, Optional[np.ndarray]] = {ch: None for ch in CHANNELS}
        self._accum: dict[str, np.ndarray] = {
            ch: np.zeros(3, dtype=np.float64) for ch in CHANNELS
        }
        self._settle_n = 0
        self._settled = False

        self._dist: dict[str, float] = {ch: 0.0 for ch in CHANNELS}
        self._pressed: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._press_count: dict[str, int] = {ch: 0 for ch in CHANNELS}

        self._lock = threading.Lock()
        self._frame: Optional[np.ndarray] = None
        self._preview_jpeg: Optional[bytes] = None
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

    def _encode_preview(self, frame: np.ndarray):
        """Annotate frame with detection markers and encode as JPEG."""
        display = frame.copy()

        for ch in CHANNELS:
            x, y = self._pixels[ch]
            r = self._patch_r

            with self._lock:
                pressed = self._pressed[ch]
                dist = self._dist[ch]
                settled = self._settled

            color = _MARKER_COLORS[ch]
            ring = _PRESSED_COLOR if pressed else _IDLE_COLOR

            cv2.rectangle(display, (x - r, y - r), (x + r, y + r), color, 1)
            cv2.circle(display, (x, y), _MARKER_RADIUS, ring, _MARKER_THICK)

            label = ch[0].upper()
            if settled:
                label += f" {dist:.0f}"
            cv2.putText(display, label, (x + _MARKER_RADIUS + 4, y + 5),
                        cv2.FONT_HERSHEY_SIMPLEX, _FONT_SCALE, color, _FONT_THICK)

        h, w = display.shape[:2]
        cv2.putText(display, f"{w}x{h}", (10, h - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)

        if not settled:
            cv2.putText(display, "Settling...", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 200, 255), 2)

        ok, buf = cv2.imencode(".jpg", display, [cv2.IMWRITE_JPEG_QUALITY, 80])
        if ok:
            with self._lock:
                self._preview_jpeg = buf.tobytes()

    def get_preview_jpeg(self) -> Optional[bytes]:
        """Return the latest annotated frame as JPEG bytes, or None."""
        with self._lock:
            return self._preview_jpeg

    def _scale_pixels(self, frame_w: int, frame_h: int):
        """Scale calibrated pixel coords to match actual camera resolution."""
        cal_xs = [x for x, _ in self._cal_pixels.values()]
        cal_ys = [y for _, y in self._cal_pixels.values()]
        cal_w = max(cal_xs) + max(cal_xs) // 4
        cal_h = max(cal_ys) + max(cal_ys) // 4

        if frame_w < cal_w or frame_h < cal_h:
            sx = frame_w / 1920.0
            sy = frame_h / 1080.0
            self._log(f"scaling coords by {sx:.3f}x{sy:.3f}")
            for ch in CHANNELS:
                ox, oy = self._cal_pixels[ch]
                self._pixels[ch] = (int(ox * sx), int(oy * sy))
        else:
            self._pixels = dict(self._cal_pixels)

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
                            x, y = self._pixels[ch]
                            self._log(f"  {ch}: ({x}, {y})")
                        sys.stdout.flush()
                        started = True
                    with self._lock:
                        self._frame = frame
                    if self._show_preview:
                        self._encode_preview(frame)
                else:
                    time.sleep(0.01)
        finally:
            cap.release()

    def _sample(self, frame: np.ndarray, ch: str) -> np.ndarray:
        """Average a small patch around the configured pixel location."""
        x, y = self._pixels[ch]
        h, w = frame.shape[:2]
        x = max(0, min(x, w - 1))
        y = max(0, min(y, h - 1))
        r = self._patch_r
        y0, y1 = max(0, y - r), min(h, y + r + 1)
        x0, x1 = max(0, x - r), min(w, x + r + 1)
        patch = frame[y0:y1, x0:x1]
        if patch.size == 0:
            return np.zeros(3, dtype=np.float64)
        return patch.astype(np.float64).mean(axis=(0, 1))

    def update(self, sample: Sample) -> dict:
        with self._lock:
            frame = self._frame
            cam_ok = self._cam_ok

        result: dict[str, dict] = {}

        if frame is None:
            for ch in CHANNELS:
                result[ch] = {
                    "pressed": False,
                    "baseline": 0,
                    "distance": 0.0,
                    "press_count": self._press_count[ch],
                    "camera": "waiting" if cam_ok else "no_device",
                }
            return result

        for ch in CHANNELS:
            color = self._sample(frame, ch)

            if not self._settled:
                self._accum[ch] += color

            if self._settled:
                diff = color - self._ref[ch]
                tw, rw = _COLOR_FILTER[ch]
                target = tw[0]*diff[0] + tw[1]*diff[1] + tw[2]*diff[2]
                reject = rw[0]*diff[0] + rw[1]*diff[1] + rw[2]*diff[2]
                reject = max(0.0, reject)
                dist = float(target - reject)
                if np.isnan(dist):
                    dist = 0.0
                self._dist[ch] = dist

                if not self._pressed[ch] and dist > self._threshold:
                    self._pressed[ch] = True
                    self._press_count[ch] += 1
                elif self._pressed[ch] and dist < self._release_thresh:
                    self._pressed[ch] = False

            result[ch] = {
                "pressed": self._pressed[ch],
                "baseline": min(4095, int(self._dist[ch] * _DIST_SCALE)),
                "distance": round(self._dist[ch], 1),
                "press_count": self._press_count[ch],
                "camera": "ok",
            }

        if not self._settled:
            self._settle_n += 1
            if self._settle_n >= self._settle_total:
                for ch in CHANNELS:
                    self._ref[ch] = self._accum[ch] / self._settle_total
                self._settled = True

        return result
