#!/usr/bin/env python3
"""
Calibration helper for detect_video.

Opens a camera feed and lets you click on each fret button to record
its pixel coordinates.  Prints the coordinates as fret-tuner CLI params.

Usage:
    python video_calibrate.py [--device 0]

Controls:
    Left-click  -- mark current button
    Space       -- pause/resume the live feed
    u           -- undo last mark
    q           -- finish and print results

Requires: opencv-python
"""

import argparse
import cv2

BUTTONS = ("GREEN", "RED", "YELLOW", "BLUE", "ORANGE")

_BGR = {
    "GREEN":  (0, 200, 0),
    "RED":    (0, 0, 220),
    "YELLOW": (0, 220, 220),
    "BLUE":   (220, 0, 0),
    "ORANGE": (0, 140, 255),
}


def main():
    parser = argparse.ArgumentParser(
        description="Calibrate pixel locations for video fret detection"
    )
    parser.add_argument("--device", type=int, default=0, help="Camera device index")
    parser.add_argument("--width", type=int, default=1920, help="Capture width")
    parser.add_argument("--height", type=int, default=1080, help="Capture height")
    args = parser.parse_args()

    cap = cv2.VideoCapture(args.device)
    if not cap.isOpened():
        print(f"Error: cannot open camera device {args.device}")
        return
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)

    coords: dict[str, tuple[int, int]] = {}
    idx = 0
    click_raw = None
    paused = False
    frozen_frame = None
    scale_x = 1.0
    scale_y = 1.0

    def on_mouse(event, x, y, _flags, _param):
        nonlocal click_raw
        if event == cv2.EVENT_LBUTTONDOWN:
            click_raw = (x, y)

    win = "Video Calibration"
    cv2.namedWindow(win, cv2.WINDOW_AUTOSIZE)
    cv2.setMouseCallback(win, on_mouse)

    print("Click on each fret button in the camera view.")
    print(f"  Next: {BUTTONS[0]}")
    print("  Press Space to pause, 'u' to undo, 'q' to quit.\n")

    try:
        while True:
            if not paused:
                ret, frame = cap.read()
                if not ret:
                    continue
                frozen_frame = frame.copy()
            else:
                frame = frozen_frame.copy()

            img_h, img_w = frame.shape[:2]

            for name, (cx, cy) in coords.items():
                c = _BGR[name]
                cv2.circle(frame, (cx, cy), 8, c, 2)
                cv2.putText(frame, name[0], (cx - 5, cy + 5),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.4, c, 1)

            if idx < len(BUTTONS):
                label = f"Click: {BUTTONS[idx]}"
            else:
                label = "Done! Press 'q' to finish"
            if paused:
                label = "PAUSED  " + label
            cv2.putText(frame, label, (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)

            cv2.imshow(win, frame)
            key = cv2.waitKey(30) & 0xFF

            # Compute HiDPI scale factor from window vs image size
            try:
                _, _, win_w, win_h = cv2.getWindowImageRect(win)
                if win_w > 0 and win_h > 0:
                    scale_x = img_w / win_w
                    scale_y = img_h / win_h
            except Exception:
                pass

            if key == ord(" "):
                paused = not paused
                print("  " + ("Paused" if paused else "Resumed"))

            if click_raw is not None and idx < len(BUTTONS):
                name = BUTTONS[idx]
                img_x = int(click_raw[0] * scale_x)
                img_y = int(click_raw[1] * scale_y)
                coords[name] = (img_x, img_y)
                print(f"  {name}: ({img_x}, {img_y})  [scale {scale_x:.1f}x{scale_y:.1f}]")
                click_raw = None
                idx += 1
                if idx < len(BUTTONS):
                    print(f"  Next: {BUTTONS[idx]}")
                else:
                    print("\nAll buttons marked. Press 'q' to print results.\n")

            if key == ord("u") and idx > 0:
                idx -= 1
                removed = BUTTONS[idx]
                coords.pop(removed, None)
                click = None
                print(f"  Undo: {removed}")
                print(f"  Next: {BUTTONS[idx]}")

            if key == ord("q"):
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()

    if len(coords) < len(BUTTONS):
        print(f"Only {len(coords)}/{len(BUTTONS)} buttons marked.")
        return

    print("=" * 50)
    print("CLI params for fret-tuner:\n")
    parts = []
    for name in BUTTONS:
        x, y = coords[name]
        parts.append(f"--param {name}_X={x}")
        parts.append(f"--param {name}_Y={y}")
    print("  " + " \\\n  ".join(parts))

    print("\nOr paste into default_params():\n")
    for name in BUTTONS:
        x, y = coords[name]
        print(f'    "{name}_X": {x}, "{name}_Y": {y},')


if __name__ == "__main__":
    main()
