"""
Vision System Unit Tests

Tests for note detection and game state extraction.
"""

import pytest
import numpy as np
import cv2
import sys
import os

# Add vision module to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'vision'))

from detector import (
    NoteDetector, Note, Chord, Fret, 
    HighwayRegion, visualize_detection
)


class TestNoteDetector:
    """Tests for NoteDetector class."""
    
    @pytest.fixture
    def detector(self):
        """Create a detector with default highway region."""
        highway = HighwayRegion(
            x_start=180,
            x_end=460,
            y_strike=400,
            y_top=100,
            lane_width=56
        )
        return NoteDetector(highway_region=highway)
    
    @pytest.fixture
    def blank_frame(self):
        """Create a blank test frame."""
        return np.zeros((480, 640, 3), dtype=np.uint8)
    
    def test_initialization(self, detector):
        """Test detector initialization."""
        assert detector.highway is not None
        assert detector._calibrated is True
    
    def test_detect_no_notes(self, detector, blank_frame):
        """Test detection on blank frame returns empty list."""
        notes = detector.detect_notes(blank_frame)
        assert notes == []
    
    def test_green_note_detection(self, detector):
        """Test detection of a green note."""
        # Create frame with a green circle in the green lane
        frame = np.zeros((480, 640, 3), dtype=np.uint8)
        
        # Green lane center (approximately)
        green_x = 208  # x_start + lane_width/2
        note_y = 300   # Above strike line
        
        # Draw a bright green circle
        cv2.circle(frame, (green_x, note_y), 15, (0, 255, 0), -1)
        
        notes = detector.detect_notes(frame)
        
        # Should detect at least one green note
        green_notes = [n for n in notes if n.fret == Fret.GREEN]
        assert len(green_notes) >= 1
    
    def test_chord_grouping(self, detector):
        """Test grouping notes into chords."""
        notes = [
            Note(fret=Fret.GREEN, y_position=100, x_position=200),
            Note(fret=Fret.RED, y_position=105, x_position=260),
            Note(fret=Fret.YELLOW, y_position=200, x_position=320),
        ]
        
        chords = detector.group_into_chords(notes, y_tolerance=20)
        
        # Should have 2 chords: (green+red) and (yellow)
        assert len(chords) == 2
        assert len(chords[0].notes) == 2
        assert len(chords[1].notes) == 1
    
    def test_chord_fret_mask(self, detector):
        """Test chord fret mask calculation."""
        notes = [
            Note(fret=Fret.GREEN, y_position=100, x_position=200),
            Note(fret=Fret.RED, y_position=100, x_position=260),
        ]
        
        chord = Chord(notes=notes, y_position=100)
        
        # Green (bit 0) + Red (bit 1) = 0b00011 = 3
        assert chord.fret_mask == 3
    
    def test_lane_center_calculation(self, detector):
        """Test lane center position calculation."""
        # Green should be leftmost
        green_center = detector._get_lane_center(Fret.GREEN)
        red_center = detector._get_lane_center(Fret.RED)
        orange_center = detector._get_lane_center(Fret.ORANGE)
        
        assert green_center < red_center
        assert red_center < orange_center


class TestNote:
    """Tests for Note dataclass."""
    
    def test_note_creation(self):
        """Test creating a note."""
        note = Note(
            fret=Fret.GREEN,
            y_position=100,
            x_position=200
        )
        
        assert note.fret == Fret.GREEN
        assert note.y_position == 100
        assert note.is_sustain is False
        assert note.is_star_power is False
    
    def test_sustain_note(self):
        """Test creating a sustain note."""
        note = Note(
            fret=Fret.BLUE,
            y_position=150,
            x_position=380,
            is_sustain=True
        )
        
        assert note.is_sustain is True


class TestVisualization:
    """Tests for debug visualization."""
    
    def test_visualize_detection(self):
        """Test visualization doesn't crash."""
        frame = np.zeros((480, 640, 3), dtype=np.uint8)
        notes = [
            Note(fret=Fret.GREEN, y_position=100, x_position=200),
        ]
        highway = HighwayRegion(
            x_start=180,
            x_end=460,
            y_strike=400,
            y_top=100,
            lane_width=56
        )
        
        # Should not raise
        result = visualize_detection(frame, notes, highway)
        
        assert result.shape == frame.shape
        # Visualization should modify the frame
        assert not np.array_equal(result, frame)


class TestFretEnum:
    """Tests for Fret enumeration."""
    
    def test_fret_values(self):
        """Test fret enum values."""
        assert Fret.GREEN == 0
        assert Fret.RED == 1
        assert Fret.YELLOW == 2
        assert Fret.BLUE == 3
        assert Fret.ORANGE == 4
    
    def test_fret_bitmask(self):
        """Test creating bitmasks from frets."""
        mask = (1 << Fret.GREEN) | (1 << Fret.YELLOW)
        assert mask == 0b00101  # 5


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
