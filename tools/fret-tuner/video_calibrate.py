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

HOLD_BUTTONS = ("GREEN", "RED", "YELLOW", "BLUE", "ORANGE")
EDGE_BUTTONS = ("GREEN_E", "RED_E", "YELLOW_E", "BLUE_E", "ORANGE_E")

_BGR = {
    "GREEN":  (0, 200, 0),
    "RED":    (0, 0, 220),
    "YELLOW": (0, 220, 220),
    "BLUE":   (220, 0, 0),
    "ORANGE": (0, 140, 255),
    "GREEN_E":  (0, 200, 0),
    "RED_E":    (0, 0, 220),
    "YELLOW_E": (0, 220, 220),
    "BLUE_E":   (220, 0, 0),
    "ORANGE_E": (0, 140, 255),
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

    all_targets = list(HOLD_BUTTONS) + list(EDGE_BUTTONS)
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

    def _display_name(name):
        if name.endswith("_E"):
            return f"{name[:-2]} edge"
        return name + " hold"

    print("Click on each fret button in the camera view.")
    print(f"  Next: {_display_name(all_targets[0])}")
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
                is_edge = name.endswith("_E")
                radius = 5 if is_edge else 8
                thickness = 1 if is_edge else 2
                cv2.circle(frame, (cx, cy), radius, c, thickness)
                lbl = name[0].lower() + "e" if is_edge else name[0]
                cv2.putText(frame, lbl, (cx - 5, cy + 5),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.4, c, 1)
                base_name = name[:-2] if is_edge else name
                edge_name = base_name + "_E"
                if not is_edge and edge_name in coords:
                    cv2.line(frame, (cx, cy), coords[edge_name], c, 1)

            if idx < len(all_targets):
                dn = _display_name(all_targets[idx])
                label = f"Click: {dn}"
                if idx == len(HOLD_BUTTONS):
                    label = "EDGE PASS -- " + label
            else:
                label = "Done! Press 'q' to finish"
            if paused:
                label = "PAUSED  " + label
            cv2.putText(frame, label, (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)

            cv2.imshow(win, frame)
            key = cv2.waitKey(30) & 0xFF

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

            if click_raw is not None and idx < len(all_targets):
                name = all_targets[idx]
                img_x = int(click_raw[0] * scale_x)
                img_y = int(click_raw[1] * scale_y)
                coords[name] = (img_x, img_y)
                print(f"  {_display_name(name)}: ({img_x}, {img_y})  [scale {scale_x:.1f}x{scale_y:.1f}]")
                click_raw = None
                idx += 1
                if idx == len(HOLD_BUTTONS):
                    print("\nHold positions done. Now click the edge detector point for each button.")
                    print(f"  Next: {_display_name(all_targets[idx])}\n")
                elif idx < len(all_targets):
                    print(f"  Next: {_display_name(all_targets[idx])}")
                else:
                    print("\nAll points marked. Press 'q' to print results.\n")

            if key == ord("u") and idx > 0:
                idx -= 1
                removed = all_targets[idx]
                coords.pop(removed, None)
                click_raw = None
                print(f"  Undo: {_display_name(removed)}")
                print(f"  Next: {_display_name(all_targets[idx])}")

            if key == ord("q"):
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()

    if len(coords) < len(all_targets):
        print(f"Only {len(coords)}/{len(all_targets)} points marked.")

    print("=" * 50)
    print("CLI params for fret-tuner:\n")
    parts = []
    for name in HOLD_BUTTONS:
        if name in coords:
            x, y = coords[name]
            parts.append(f"--param {name}_X={x}")
            parts.append(f"--param {name}_Y={y}")
    for name in EDGE_BUTTONS:
        if name in coords:
            x, y = coords[name]
            base = name[:-2]
            parts.append(f"--param {base}_EX={x}")
            parts.append(f"--param {base}_EY={y}")
    print("  " + " \\\n  ".join(parts))

    print("\nOr paste into default_params():\n")
    for name in HOLD_BUTTONS:
        if name in coords:
            x, y = coords[name]
            print(f'    "{name}_X": {x}, "{name}_Y": {y},')
    for name in EDGE_BUTTONS:
        if name in coords:
            x, y = coords[name]
            base = name[:-2]
            print(f'    "{base}_EX": {x}, "{base}_EY": {y},')


if __name__ == "__main__":
    main()
