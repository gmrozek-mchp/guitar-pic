"""
Note Detection Module

Detects Guitar Hero notes, chords, and game state from video frames.
"""

import cv2
import numpy as np
from typing import List, Tuple, Optional, NamedTuple
from enum import IntEnum
from dataclasses import dataclass


class Fret(IntEnum):
    """Fret button indices."""
    GREEN = 0
    RED = 1
    YELLOW = 2
    BLUE = 3
    ORANGE = 4


# Color ranges for note detection (HSV format)
# These will need calibration based on actual capture
NOTE_COLORS = {
    Fret.GREEN: {
        'lower': np.array([35, 100, 100]),
        'upper': np.array([85, 255, 255]),
    },
    Fret.RED: {
        'lower': np.array([0, 100, 100]),
        'upper': np.array([10, 255, 255]),
    },
    Fret.YELLOW: {
        'lower': np.array([20, 100, 100]),
        'upper': np.array([35, 255, 255]),
    },
    Fret.BLUE: {
        'lower': np.array([100, 100, 100]),
        'upper': np.array([130, 255, 255]),
    },
    Fret.ORANGE: {
        'lower': np.array([10, 100, 100]),
        'upper': np.array([20, 255, 255]),
    },
}


@dataclass
class Note:
    """Represents a detected note."""
    fret: Fret
    y_position: int  # Vertical position (distance from strike line)
    x_position: int  # Horizontal position (lane center)
    is_sustain: bool = False  # Whether this is a held note
    is_star_power: bool = False  # Whether this is a star power note
    confidence: float = 1.0  # Detection confidence


@dataclass
class Chord:
    """Represents multiple notes to be played simultaneously."""
    notes: List[Note]
    y_position: int  # Average Y position
    
    @property
    def fret_mask(self) -> int:
        """Get bitmask of frets in this chord."""
        mask = 0
        for note in self.notes:
            mask |= (1 << note.fret)
        return mask


@dataclass
class GameState:
    """Current game state extracted from frame."""
    notes: List[Note]
    chords: List[Chord]
    star_power_meter: float  # 0.0 to 1.0
    star_power_active: bool
    multiplier: int  # Current score multiplier (1-4)
    rock_meter: float  # 0.0 to 1.0 (crowd happiness)


class HighwayRegion(NamedTuple):
    """Defines the note highway region on screen."""
    x_start: int
    x_end: int
    y_strike: int  # Y position of strike line
    y_top: int  # Top of visible highway
    lane_width: int


class NoteDetector:
    """
    Detects notes from Guitar Hero gameplay frames.
    
    The detection process:
    1. Crop to highway region
    2. Convert to HSV color space
    3. Apply color masks for each fret
    4. Find contours for note gems
    5. Calculate positions relative to strike line
    """
    
    def __init__(self, highway_region: Optional[HighwayRegion] = None):
        """
        Initialize the detector.
        
        Args:
            highway_region: The region of the frame containing the note highway.
                          If None, will attempt auto-detection.
        """
        self.highway = highway_region
        self._calibrated = highway_region is not None
        
        # Detection parameters (may need tuning)
        self.min_note_area = 100  # Minimum contour area for a note
        self.max_note_area = 5000  # Maximum contour area
        
    def calibrate_highway(self, frame: np.ndarray) -> bool:
        """
        Auto-detect the highway region from a frame.
        
        This should be called on a frame where the highway is clearly visible.
        
        Args:
            frame: A gameplay frame
            
        Returns:
            True if calibration successful
        """
        # TODO: Implement auto-detection of highway region
        # Strategy:
        # 1. Look for the strike line (horizontal line near bottom)
        # 2. Find the 5 lane markers
        # 3. Determine highway boundaries
        
        # For now, use default values for 640x480 capture
        # These values will need adjustment based on actual game/capture
        self.highway = HighwayRegion(
            x_start=180,
            x_end=460,
            y_strike=400,
            y_top=100,
            lane_width=56
        )
        self._calibrated = True
        
        return True
    
    def detect_notes(self, frame: np.ndarray) -> List[Note]:
        """
        Detect all notes in the current frame.
        
        Args:
            frame: BGR frame from video capture
            
        Returns:
            List of detected notes
        """
        if not self._calibrated:
            self.calibrate_highway(frame)
        
        notes = []
        
        # Crop to highway region
        highway_frame = frame[
            self.highway.y_top:self.highway.y_strike + 50,
            self.highway.x_start:self.highway.x_end
        ]
        
        # Convert to HSV for color detection
        hsv = cv2.cvtColor(highway_frame, cv2.COLOR_BGR2HSV)
        
        # Detect notes for each fret color
        for fret in Fret:
            fret_notes = self._detect_fret_notes(hsv, fret)
            notes.extend(fret_notes)
        
        return notes
    
    def _detect_fret_notes(self, hsv_frame: np.ndarray, fret: Fret) -> List[Note]:
        """
        Detect notes of a specific fret color.
        
        Args:
            hsv_frame: HSV color space frame (cropped to highway)
            fret: Which fret color to detect
            
        Returns:
            List of notes for this fret
        """
        notes = []
        
        # Get color range for this fret
        color = NOTE_COLORS[fret]
        
        # Create mask for this color
        mask = cv2.inRange(hsv_frame, color['lower'], color['upper'])
        
        # Clean up mask
        kernel = np.ones((3, 3), np.uint8)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
        
        # Find contours
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        # Calculate expected lane X position
        lane_center = self._get_lane_center(fret)
        
        for contour in contours:
            area = cv2.contourArea(contour)
            
            if self.min_note_area < area < self.max_note_area:
                # Get contour center
                M = cv2.moments(contour)
                if M["m00"] > 0:
                    cx = int(M["m10"] / M["m00"])
                    cy = int(M["m01"] / M["m00"])
                    
                    # Check if in correct lane (with tolerance)
                    if abs(cx - lane_center) < self.highway.lane_width // 2:
                        # Calculate distance from strike line
                        # Note: cy is relative to cropped frame
                        distance = (self.highway.y_strike - self.highway.y_top) - cy
                        
                        # Check for sustain (elongated vertical shape)
                        _, _, w, h = cv2.boundingRect(contour)
                        is_sustain = h > w * 2
                        
                        note = Note(
                            fret=fret,
                            y_position=distance,
                            x_position=cx + self.highway.x_start,
                            is_sustain=is_sustain,
                            confidence=min(1.0, area / 1000)
                        )
                        notes.append(note)
        
        return notes
    
    def _get_lane_center(self, fret: Fret) -> int:
        """Get the X center position for a fret lane (relative to cropped highway)."""
        highway_width = self.highway.x_end - self.highway.x_start
        lane_width = highway_width / 5
        return int(lane_width * (fret + 0.5))
    
    def group_into_chords(self, notes: List[Note], y_tolerance: int = 20) -> List[Chord]:
        """
        Group notes at similar Y positions into chords.
        
        Args:
            notes: List of detected notes
            y_tolerance: Maximum Y distance to consider notes as chord
            
        Returns:
            List of chords
        """
        if not notes:
            return []
        
        # Sort by Y position
        sorted_notes = sorted(notes, key=lambda n: n.y_position)
        
        chords = []
        current_chord_notes = [sorted_notes[0]]
        
        for note in sorted_notes[1:]:
            if abs(note.y_position - current_chord_notes[0].y_position) <= y_tolerance:
                current_chord_notes.append(note)
            else:
                # Save current chord and start new one
                avg_y = sum(n.y_position for n in current_chord_notes) // len(current_chord_notes)
                chords.append(Chord(notes=current_chord_notes, y_position=avg_y))
                current_chord_notes = [note]
        
        # Don't forget last chord
        avg_y = sum(n.y_position for n in current_chord_notes) // len(current_chord_notes)
        chords.append(Chord(notes=current_chord_notes, y_position=avg_y))
        
        return chords
    
    def detect_star_power_meter(self, frame: np.ndarray) -> float:
        """
        Detect the star power meter fill level.
        
        Args:
            frame: Full gameplay frame
            
        Returns:
            Fill level from 0.0 to 1.0
        """
        # TODO: Implement star power meter detection
        # The meter is usually in a specific screen location
        # Look for the characteristic glow/color
        return 0.0
    
    def detect_game_state(self, frame: np.ndarray) -> GameState:
        """
        Extract complete game state from frame.
        
        Args:
            frame: Full gameplay frame
            
        Returns:
            GameState with all detected information
        """
        notes = self.detect_notes(frame)
        chords = self.group_into_chords(notes)
        star_power = self.detect_star_power_meter(frame)
        
        return GameState(
            notes=notes,
            chords=chords,
            star_power_meter=star_power,
            star_power_active=False,  # TODO: detect
            multiplier=1,  # TODO: detect
            rock_meter=0.5  # TODO: detect
        )


class ScrollSpeedCalculator:
    """
    Calculates the scroll speed of the note highway.
    
    Tracks notes across frames to determine pixels per millisecond.
    """
    
    def __init__(self):
        self._history: List[Tuple[float, List[Note]]] = []
        self._scroll_speed: float = 0.3  # Default: 0.3 px/ms
        
    def update(self, timestamp: float, notes: List[Note]) -> None:
        """
        Update with new frame data.
        
        Args:
            timestamp: Frame timestamp in seconds
            notes: Notes detected in this frame
        """
        self._history.append((timestamp, notes))
        
        # Keep only recent history
        if len(self._history) > 30:
            self._history.pop(0)
        
        # Calculate speed if we have enough history
        if len(self._history) >= 5:
            self._calculate_speed()
    
    def _calculate_speed(self) -> None:
        """Calculate scroll speed from history."""
        # TODO: Track individual notes across frames
        # Compare Y position changes over time
        # Average to get reliable speed estimate
        pass
    
    @property
    def speed(self) -> float:
        """Get current scroll speed in pixels per millisecond."""
        return self._scroll_speed


# Debug visualization
def visualize_detection(frame: np.ndarray, notes: List[Note], highway: HighwayRegion) -> np.ndarray:
    """
    Draw detected notes on frame for debugging.
    
    Args:
        frame: Original frame
        notes: Detected notes
        highway: Highway region
        
    Returns:
        Frame with visualization overlay
    """
    vis = frame.copy()
    
    # Draw highway region
    cv2.rectangle(vis, 
                  (highway.x_start, highway.y_top),
                  (highway.x_end, highway.y_strike),
                  (255, 255, 255), 1)
    
    # Draw strike line
    cv2.line(vis,
             (highway.x_start, highway.y_strike),
             (highway.x_end, highway.y_strike),
             (0, 255, 255), 2)
    
    # Draw detected notes
    colors = {
        Fret.GREEN: (0, 255, 0),
        Fret.RED: (0, 0, 255),
        Fret.YELLOW: (0, 255, 255),
        Fret.BLUE: (255, 0, 0),
        Fret.ORANGE: (0, 165, 255),
    }
    
    for note in notes:
        y = highway.y_strike - note.y_position
        cv2.circle(vis, (note.x_position, y), 10, colors[note.fret], -1)
    
    return vis


# Example usage
if __name__ == "__main__":
    print("Note Detector Module")
    print("Run with a test image to verify detection")
    
    # Test with a sample image if available
    import sys
    if len(sys.argv) > 1:
        img = cv2.imread(sys.argv[1])
        if img is not None:
            detector = NoteDetector()
            notes = detector.detect_notes(img)
            print(f"Detected {len(notes)} notes:")
            for note in notes:
                print(f"  {note.fret.name} at y={note.y_position}")
            
            # Show visualization
            vis = visualize_detection(img, notes, detector.highway)
            cv2.imshow("Detection", vis)
            cv2.waitKey(0)
