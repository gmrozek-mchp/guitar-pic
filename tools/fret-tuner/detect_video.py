"""
Video-based fret detection -- camera pointed at the game screen.

Consumes frames from the shared reference video buffer (camera.py); does
not own the camera. Sensor markers and per-button signal levels are
exposed as an overlay layer through the `overlays()` hook so the server
can bake them into the live MJPEG and paused scrub frames.

Two sensor points per button, both above the strike line:

  Sensor 1 (hold)  -- brightness detect (color OR white).
                      Sets pressed=True whenever anything bright appears.
  Sensor 2 (edge)  -- color-filtered leading-edge detect.
                      Increments press_count on each new note arrival,
                      which the actuator uses to schedule strums.

Use video_calibrate.py to find pixel coordinates for all 10 sensor
points. Detection thresholds are applied to raw pixel values (dark
background is the implicit zero).

The "baseline" output is the stronger of the two signals (scaled to
0..4095) for chart overlay visualization.

Requires: opencv-python
"""

import threading
from typing import Optional

import cv2
import numpy as np

from camera import Frame, get_camera
from stream import CH_COLORS, CHANNELS, Sample, make_slot_schema

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

# Calibration coords are anchored to a 1920x1080 reference frame.
_CAL_W = 1920
_CAL_H = 1080


class Detector:
    """Camera-based fret press detector consuming the shared video buffer."""

    @classmethod
    def default_params(cls) -> dict:
        return {
            "HOLD_THRESH": 50,
            "HOLD_RELEASE_FRAC": 60,
            "EDGE_GREEN": 50,
            "EDGE_RED": 50,
            "EDGE_YELLOW": 50,
            "EDGE_BLUE": 50,
            "EDGE_ORANGE": 50,
            "PATCH_RADIUS": 2,
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

        self._cal_hold_pixels: dict[str, tuple[int, int]] = {}
        self._cal_edge_pixels: dict[str, tuple[int, int]] = {}
        for ch in CHANNELS:
            p = ch.upper()
            self._cal_hold_pixels[ch] = (int(defaults[f"{p}_X"]), int(defaults[f"{p}_Y"]))
            self._cal_edge_pixels[ch] = (int(defaults[f"{p}_EX"]), int(defaults[f"{p}_EY"]))

        # Per-frame-size cache of scaled pixel coords. The capture path
        # passes full-res frames to detection while the view path applies
        # overlays onto the downscaled working frame, so we typically need
        # at least two entries (one for detection, one for view). Callers
        # treat the returned dicts as immutable.
        self._scaled_cache: dict[
            tuple[int, int],
            tuple[dict[str, tuple[int, int]], dict[str, tuple[int, int]]],
        ] = {}
        self._scale_lock = threading.Lock()

        self._hold_dist: dict[str, float] = {ch: 0.0 for ch in CHANNELS}
        self._edge_dist: dict[str, float] = {ch: 0.0 for ch in CHANNELS}
        self._pressed: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._press_count: dict[str, int] = {ch: 0 for ch in CHANNELS}
        self._edge_active: dict[str, bool] = {ch: False for ch in CHANNELS}
        self._cached_result: Optional[dict] = None

        self._lock = threading.Lock()

        self._cam = get_camera()
        self._cam.add_frame_listener(self._on_frame)

    def stop(self):
        """Detach the camera frame listener."""
        try:
            self._cam.remove_frame_listener(self._on_frame)
        except Exception:
            pass
        self._log("stopped")

    def __del__(self):
        try:
            self._cam.remove_frame_listener(self._on_frame)
        except Exception:
            pass

    def _log(self, msg: str):
        print(f"[detect_video] {msg}", flush=True)

    _EDGE_RADIUS = 10
    _EDGE_THICK = 2

    def _scale_pixels(
        self, frame_w: int, frame_h: int,
    ) -> tuple[dict[str, tuple[int, int]], dict[str, tuple[int, int]]]:
        """Return (hold_pixels, edge_pixels) scaled for the given frame size.

        Cached per (w, h) so the detection (full-res) and overlay (working-res)
        paths don't keep invalidating each other. Returned dicts are shared
        across calls and should be treated as immutable by callers.
        """
        key = (frame_w, frame_h)
        with self._scale_lock:
            cached = self._scaled_cache.get(key)
            if cached is not None:
                return cached

            all_cal = list(self._cal_hold_pixels.values()) + list(self._cal_edge_pixels.values())
            cal_xs = [x for x, _ in all_cal]
            cal_ys = [y for _, y in all_cal]
            cal_w = max(cal_xs) + max(cal_xs) // 4
            cal_h = max(cal_ys) + max(cal_ys) // 4

            if frame_w < cal_w or frame_h < cal_h:
                sx = frame_w / float(_CAL_W)
                sy = frame_h / float(_CAL_H)
                self._log(f"scaling coords for {frame_w}x{frame_h} by {sx:.3f}x{sy:.3f}")
                hold = {
                    ch: (int(self._cal_hold_pixels[ch][0] * sx),
                         int(self._cal_hold_pixels[ch][1] * sy))
                    for ch in CHANNELS
                }
                edge = {
                    ch: (int(self._cal_edge_pixels[ch][0] * sx),
                         int(self._cal_edge_pixels[ch][1] * sy))
                    for ch in CHANNELS
                }
            else:
                hold = dict(self._cal_hold_pixels)
                edge = dict(self._cal_edge_pixels)
            cached = (hold, edge)
            self._scaled_cache[key] = cached
            return cached

    def _on_frame(self, frame: Frame) -> None:
        """Camera frame listener -- runs in the capture thread.

        Prefers the full-resolution raw frame from the capture device when
        available, so detection is unaffected by the working/buffer downscale
        used for the live view.

        After detection, attaches a per-frame snapshot to `frame.meta` so the
        view path can render scrub-accurate overlays from buffered frames.
        """
        img = frame.detect_image()
        if img is None:
            return
        h, w = img.shape[:2]
        hold_px, edge_px = self._scale_pixels(w, h)
        self._detect(img, hold_px, edge_px)

        with self._lock:
            snap_state = dict(self._cached_result) if self._cached_result else {}
            snap_markers = {
                ch: {
                    "pressed": self._pressed[ch],
                    "hold_dist": self._hold_dist[ch],
                    "edge_dist": self._edge_dist[ch],
                    "edge_active": self._edge_active[ch],
                } for ch in CHANNELS
            }
            patch_r = self._patch_r
        if frame.meta is None:
            frame.meta = {}
        frame.meta["detect_video"] = {
            "state": snap_state,
            "markers": snap_markers,
            "patch_r": patch_r,
        }

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

    def _detect(
        self,
        frame: np.ndarray,
        hold_pixels: dict[str, tuple[int, int]],
        edge_pixels: dict[str, tuple[int, int]],
    ):
        """Run detection on a new camera frame. Called from capture thread."""
        result: dict[str, dict] = {}
        for ch in CHANNELS:
            hx, hy = hold_pixels[ch]
            ex, ey = edge_pixels[ch]
            hold_color = self._sample_at(frame, hx, hy)
            edge_color = self._sample_at(frame, ex, ey)

            hold_dist = self._brightness(hold_color)
            edge_dist = self._color_signal(edge_color, ch)

            with self._lock:
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

        if cached is not None:
            return cached

        cam_status = self._cam.status()
        msg = "no_device" if not cam_status.get("active") else "waiting"
        return {ch: {
            "pressed": False, "baseline": 0, "edge_line": 0,
            "press_count": 0, "camera": msg,
        } for ch in CHANNELS}

    # --- Chart schema -------------------------------------------------------

    def chart_schema(self) -> list[dict]:
        """Per-fret hold + edge signals (no raw ADC)."""
        return [
            make_slot_schema(ch, [
                {"key": "baseline", "label": "Hold",
                 "color": CH_COLORS[ch], "width": 1.5},
                {"key": "edge_line", "label": "Edge",
                 "color": "#e94560", "width": 1.5, "dash": [2, 2]},
            ])
            for ch in CHANNELS
        ]

    # --- Overlay extension --------------------------------------------------

    def overlays(self) -> list[dict]:
        """Expose sensor markers as an overlay layer for the video viewport."""
        return [{"name": "detect_video.markers", "draw": self._draw_markers}]

    def _draw_markers(self, image, frame: Optional[Frame] = None) -> None:
        h, w = image.shape[:2]
        hold_px, edge_px = self._scale_pixels(w, h)

        # Prefer the per-frame snapshot when available (scrub fidelity); fall
        # back to live state for live frames or when the frame predates the
        # current detector instance.
        snap_meta = None
        if frame is not None and frame.meta:
            snap_meta = frame.meta.get("detect_video")
        if snap_meta is not None:
            markers = snap_meta.get("markers", {})
            patch_r = int(snap_meta.get("patch_r", self._patch_r))
            snap = {
                ch: (
                    hold_px[ch],
                    edge_px[ch],
                    bool(markers.get(ch, {}).get("pressed", False)),
                    float(markers.get(ch, {}).get("hold_dist", 0.0)),
                    float(markers.get(ch, {}).get("edge_dist", 0.0)),
                    bool(markers.get(ch, {}).get("edge_active", False)),
                ) for ch in CHANNELS
            }
        else:
            with self._lock:
                snap = {
                    ch: (
                        hold_px[ch],
                        edge_px[ch],
                        self._pressed[ch],
                        self._hold_dist[ch],
                        self._edge_dist[ch],
                        self._edge_active[ch],
                    ) for ch in CHANNELS
                }
                patch_r = self._patch_r

        for ch, ((hx, hy), (ex, ey), pressed, hold_d, edge_d, edge_on) in snap.items():
            color = _MARKER_COLORS[ch]
            hold_ring = _PRESSED_COLOR if pressed else _IDLE_COLOR

            cv2.rectangle(image, (hx - patch_r, hy - patch_r),
                          (hx + patch_r, hy + patch_r), color, 1)
            cv2.circle(image, (hx, hy), _MARKER_RADIUS, hold_ring, _MARKER_THICK)

            cv2.line(image, (hx, hy), (ex, ey), color, 1)
            edge_ring = _PRESSED_COLOR if edge_on else _IDLE_COLOR
            cv2.rectangle(image, (ex - patch_r, ey - patch_r),
                          (ex + patch_r, ey + patch_r), color, 1)
            cv2.circle(image, (ex, ey), self._EDGE_RADIUS, edge_ring, self._EDGE_THICK)

            label = f"{ch[0].upper()} h{hold_d:.0f} e{edge_d:.0f}"
            cv2.putText(image, label, (hx + _MARKER_RADIUS + 4, hy + 5),
                        cv2.FONT_HERSHEY_SIMPLEX, _FONT_SCALE, color, _FONT_THICK)
