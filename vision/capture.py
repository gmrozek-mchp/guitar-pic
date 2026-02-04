"""
Video Capture Module

Handles video capture from PS2 via USB capture device.
Provides frames for note detection processing.
"""

import cv2
import numpy as np
from typing import Optional, Tuple
import time


class VideoCapture:
    """
    Manages video capture from USB capture device.
    
    Attributes:
        device_id: The video device index (usually 0, 1, or 2)
        width: Capture width in pixels
        height: Capture height in pixels
        fps: Target frames per second
    """
    
    # Default capture settings for PS2 composite video
    DEFAULT_WIDTH = 640
    DEFAULT_HEIGHT = 480
    DEFAULT_FPS = 30
    
    def __init__(
        self,
        device_id: int = 0,
        width: int = DEFAULT_WIDTH,
        height: int = DEFAULT_HEIGHT,
        fps: int = DEFAULT_FPS
    ):
        """
        Initialize video capture.
        
        Args:
            device_id: Video device index
            width: Desired capture width
            height: Desired capture height
            fps: Desired frame rate
        """
        self.device_id = device_id
        self.width = width
        self.height = height
        self.fps = fps
        self.cap: Optional[cv2.VideoCapture] = None
        self._last_frame_time: float = 0
        self._frame_count: int = 0
        
    def open(self) -> bool:
        """
        Open the video capture device.
        
        Returns:
            True if successfully opened, False otherwise
        """
        self.cap = cv2.VideoCapture(self.device_id)
        
        if not self.cap.isOpened():
            print(f"Error: Could not open video device {self.device_id}")
            return False
        
        # Configure capture settings
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        self.cap.set(cv2.CAP_PROP_FPS, self.fps)
        
        # Verify actual settings
        actual_width = self.cap.get(cv2.CAP_PROP_FRAME_WIDTH)
        actual_height = self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
        actual_fps = self.cap.get(cv2.CAP_PROP_FPS)
        
        print(f"Video capture opened: {actual_width}x{actual_height} @ {actual_fps} fps")
        
        return True
    
    def close(self) -> None:
        """Release the video capture device."""
        if self.cap is not None:
            self.cap.release()
            self.cap = None
    
    def read_frame(self) -> Tuple[bool, Optional[np.ndarray]]:
        """
        Read a single frame from the capture device.
        
        Returns:
            Tuple of (success, frame) where frame is a numpy array in BGR format
        """
        if self.cap is None or not self.cap.isOpened():
            return False, None
        
        ret, frame = self.cap.read()
        
        if ret:
            self._last_frame_time = time.time()
            self._frame_count += 1
        
        return ret, frame
    
    def get_frame_timestamp(self) -> float:
        """Get timestamp of last captured frame."""
        return self._last_frame_time
    
    def get_frame_count(self) -> int:
        """Get total number of frames captured."""
        return self._frame_count
    
    def __enter__(self):
        """Context manager entry."""
        self.open()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()


class FrameBuffer:
    """
    Buffer for storing recent frames for multi-frame analysis.
    
    Useful for tracking note movement across frames to calculate scroll speed.
    """
    
    def __init__(self, max_size: int = 10):
        """
        Initialize frame buffer.
        
        Args:
            max_size: Maximum number of frames to store
        """
        self.max_size = max_size
        self._frames: list = []
        self._timestamps: list = []
    
    def add_frame(self, frame: np.ndarray, timestamp: float) -> None:
        """
        Add a frame to the buffer.
        
        Args:
            frame: The frame to add
            timestamp: Capture timestamp
        """
        self._frames.append(frame.copy())
        self._timestamps.append(timestamp)
        
        # Remove oldest if over capacity
        while len(self._frames) > self.max_size:
            self._frames.pop(0)
            self._timestamps.pop(0)
    
    def get_frames(self) -> list:
        """Get all buffered frames."""
        return self._frames
    
    def get_latest(self) -> Tuple[Optional[np.ndarray], Optional[float]]:
        """Get the most recent frame and timestamp."""
        if not self._frames:
            return None, None
        return self._frames[-1], self._timestamps[-1]
    
    def clear(self) -> None:
        """Clear all buffered frames."""
        self._frames.clear()
        self._timestamps.clear()


def list_capture_devices(max_devices: int = 10) -> list:
    """
    List available video capture devices.
    
    Args:
        max_devices: Maximum number of devices to check
        
    Returns:
        List of available device indices
    """
    available = []
    
    for i in range(max_devices):
        cap = cv2.VideoCapture(i)
        if cap.isOpened():
            available.append(i)
            cap.release()
    
    return available


# Example usage and testing
if __name__ == "__main__":
    print("Available capture devices:", list_capture_devices())
    
    # Test capture
    with VideoCapture(device_id=0) as cap:
        print("Press 'q' to quit, 's' to save a screenshot")
        
        while True:
            ret, frame = cap.read_frame()
            
            if not ret:
                print("Failed to read frame")
                break
            
            # Display frame
            cv2.imshow("Guitar Hero Capture", frame)
            
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                break
            elif key == ord('s'):
                filename = f"screenshot_{cap.get_frame_count()}.png"
                cv2.imwrite(filename, frame)
                print(f"Saved {filename}")
        
        cv2.destroyAllWindows()
