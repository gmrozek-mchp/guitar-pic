"""
Note Scheduler Module

Manages timing of note commands, accounting for system latency.
Sends commands to PIC microcontroller at the right time.
"""

import time
import threading
from typing import List, Optional, Callable
from dataclasses import dataclass, field
from queue import PriorityQueue
from enum import Enum

from detector import Note, Chord, Fret


class StumDirection(Enum):
    """Strum bar direction."""
    DOWN = 0
    UP = 1


@dataclass
class ScheduledAction:
    """An action scheduled to execute at a specific time."""
    execute_at: float  # Timestamp when action should execute
    fret_mask: int  # Bitmask of frets to press
    strum: StumDirection  # Strum direction
    hold_duration_ms: int = 50  # How long to hold frets
    priority: int = 0  # Lower = higher priority
    
    def __lt__(self, other):
        """For priority queue ordering."""
        if self.execute_at == other.execute_at:
            return self.priority < other.priority
        return self.execute_at < other.execute_at


@dataclass
class LatencyConfig:
    """System latency configuration.
    
    Default values tuned for Elgato Game Capture HD with component input.
    """
    video_capture_ms: float = 60.0   # Elgato Game Capture HD via component
    vision_processing_ms: float = 15.0
    serial_communication_ms: float = 3.0
    pic_processing_ms: float = 1.0
    mechanical_actuation_ms: float = 15.0
    
    @property
    def total_ms(self) -> float:
        """Total system latency in milliseconds."""
        return (self.video_capture_ms + 
                self.vision_processing_ms + 
                self.serial_communication_ms + 
                self.pic_processing_ms + 
                self.mechanical_actuation_ms)


class NoteScheduler:
    """
    Schedules note actions based on detected notes and timing.
    
    Responsibilities:
    - Calculate when to send commands based on note positions
    - Compensate for system latency
    - Manage alternating strum pattern for fast runs
    - Handle chord grouping
    """
    
    def __init__(
        self,
        latency_config: LatencyConfig = None,
        scroll_speed: float = 0.3,  # pixels per millisecond
        command_callback: Callable[[int, StumDirection, int], None] = None
    ):
        """
        Initialize the scheduler.
        
        Args:
            latency_config: System latency configuration
            scroll_speed: Note highway scroll speed in px/ms
            command_callback: Function to call when action should execute
                            Args: (fret_mask, strum_direction, hold_ms)
        """
        self.latency = latency_config or LatencyConfig()
        self.scroll_speed = scroll_speed
        self.command_callback = command_callback
        
        self._action_queue: PriorityQueue = PriorityQueue()
        self._running = False
        self._executor_thread: Optional[threading.Thread] = None
        self._last_strum = StumDirection.UP  # Alternate starting point
        
        # Track processed notes to avoid duplicates
        self._processed_notes: set = set()
        
    def start(self) -> None:
        """Start the scheduler executor thread."""
        if self._running:
            return
            
        self._running = True
        self._executor_thread = threading.Thread(target=self._executor_loop, daemon=True)
        self._executor_thread.start()
        
    def stop(self) -> None:
        """Stop the scheduler."""
        self._running = False
        if self._executor_thread:
            self._executor_thread.join(timeout=1.0)
            
    def schedule_note(self, note: Note, current_time: float) -> None:
        """
        Schedule a single note for execution.
        
        Args:
            note: The detected note
            current_time: Current timestamp
        """
        # Calculate time until note reaches strike line
        time_to_strike_ms = note.y_position / self.scroll_speed
        
        # Calculate when we need to execute (accounting for latency)
        execute_at = current_time + (time_to_strike_ms - self.latency.total_ms) / 1000.0
        
        # Create fret mask
        fret_mask = 1 << note.fret
        
        # Determine strum direction (alternate for speed)
        strum = self._get_next_strum()
        
        # Schedule the action
        action = ScheduledAction(
            execute_at=execute_at,
            fret_mask=fret_mask,
            strum=strum,
            hold_duration_ms=50 if not note.is_sustain else 500
        )
        
        self._action_queue.put(action)
        
    def schedule_chord(self, chord: Chord, current_time: float) -> None:
        """
        Schedule a chord (multiple simultaneous notes).
        
        Args:
            chord: The detected chord
            current_time: Current timestamp
        """
        # Calculate timing
        time_to_strike_ms = chord.y_position / self.scroll_speed
        execute_at = current_time + (time_to_strike_ms - self.latency.total_ms) / 1000.0
        
        # Determine strum
        strum = self._get_next_strum()
        
        # Check for sustain in any note
        has_sustain = any(n.is_sustain for n in chord.notes)
        
        action = ScheduledAction(
            execute_at=execute_at,
            fret_mask=chord.fret_mask,
            strum=strum,
            hold_duration_ms=50 if not has_sustain else 500
        )
        
        self._action_queue.put(action)
        
    def schedule_from_detection(
        self,
        chords: List[Chord],
        current_time: float
    ) -> int:
        """
        Schedule actions from detected chords.
        
        Args:
            chords: List of detected chords
            current_time: Current timestamp
            
        Returns:
            Number of actions scheduled
        """
        scheduled = 0
        
        for chord in chords:
            # Create unique ID for this chord
            chord_id = (chord.y_position, chord.fret_mask)
            
            # Skip if already processed
            if chord_id in self._processed_notes:
                continue
                
            self._processed_notes.add(chord_id)
            self.schedule_chord(chord, current_time)
            scheduled += 1
        
        # Clean up old processed notes
        self._cleanup_processed()
        
        return scheduled
    
    def _get_next_strum(self) -> StumDirection:
        """Get next strum direction (alternating pattern)."""
        if self._last_strum == StumDirection.DOWN:
            self._last_strum = StumDirection.UP
        else:
            self._last_strum = StumDirection.DOWN
        return self._last_strum
    
    def _cleanup_processed(self) -> None:
        """Remove old entries from processed notes set."""
        # For simplicity, just clear if too large
        if len(self._processed_notes) > 1000:
            self._processed_notes.clear()
    
    def _executor_loop(self) -> None:
        """Main loop that executes scheduled actions."""
        while self._running:
            try:
                if self._action_queue.empty():
                    time.sleep(0.001)  # 1ms sleep when idle
                    continue
                
                # Peek at next action
                action = self._action_queue.get_nowait()
                
                current_time = time.time()
                
                if action.execute_at <= current_time:
                    # Execute now
                    self._execute_action(action)
                elif action.execute_at - current_time < 0.010:
                    # Within 10ms, busy wait
                    while time.time() < action.execute_at:
                        pass
                    self._execute_action(action)
                else:
                    # Put back and wait
                    self._action_queue.put(action)
                    time.sleep(0.001)
                    
            except Exception as e:
                print(f"Scheduler error: {e}")
                
    def _execute_action(self, action: ScheduledAction) -> None:
        """Execute a scheduled action."""
        if self.command_callback:
            self.command_callback(
                action.fret_mask,
                action.strum,
                action.hold_duration_ms
            )
        else:
            # Debug output
            frets = []
            for i, fret in enumerate(Fret):
                if action.fret_mask & (1 << i):
                    frets.append(fret.name)
            print(f"Execute: {'+'.join(frets)} strum {action.strum.name}")


class WhammyController:
    """
    Controls whammy bar oscillation during sustained notes.
    """
    
    def __init__(self, command_callback: Callable[[int], None] = None):
        """
        Initialize whammy controller.
        
        Args:
            command_callback: Function to call with whammy position (0-255)
        """
        self.command_callback = command_callback
        self._running = False
        self._oscillating = False
        self._thread: Optional[threading.Thread] = None
        
        # Oscillation parameters
        self.frequency_hz = 3.0
        self.min_position = 50
        self.max_position = 200
        
    def start(self) -> None:
        """Start the whammy controller thread."""
        self._running = True
        self._thread = threading.Thread(target=self._oscillation_loop, daemon=True)
        self._thread.start()
        
    def stop(self) -> None:
        """Stop the whammy controller."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)
            
    def start_oscillation(self) -> None:
        """Start whammy oscillation."""
        self._oscillating = True
        
    def stop_oscillation(self) -> None:
        """Stop whammy oscillation and return to center."""
        self._oscillating = False
        if self.command_callback:
            self.command_callback(128)  # Center position
            
    def _oscillation_loop(self) -> None:
        """Generate oscillating whammy movement."""
        import math
        
        phase = 0.0
        last_time = time.time()
        
        while self._running:
            if not self._oscillating:
                time.sleep(0.01)
                continue
                
            current_time = time.time()
            dt = current_time - last_time
            last_time = current_time
            
            # Update phase
            phase += 2 * math.pi * self.frequency_hz * dt
            if phase > 2 * math.pi:
                phase -= 2 * math.pi
                
            # Calculate position (sine wave)
            normalized = (math.sin(phase) + 1) / 2  # 0 to 1
            position = int(self.min_position + 
                         normalized * (self.max_position - self.min_position))
            
            if self.command_callback:
                self.command_callback(position)
                
            time.sleep(0.02)  # 50Hz update rate


# Example usage
if __name__ == "__main__":
    def dummy_callback(frets: int, strum: StumDirection, hold: int):
        fret_names = []
        for i, fret in enumerate(Fret):
            if frets & (1 << i):
                fret_names.append(fret.name)
        print(f"Command: {'+'.join(fret_names)} {strum.name} hold={hold}ms")
    
    scheduler = NoteScheduler(command_callback=dummy_callback)
    scheduler.start()
    
    # Simulate some notes
    from detector import Note, Chord
    
    notes = [
        Note(fret=Fret.GREEN, y_position=300, x_position=200),
        Note(fret=Fret.RED, y_position=200, x_position=260),
        Note(fret=Fret.YELLOW, y_position=100, x_position=320),
    ]
    
    chords = [Chord(notes=[n], y_position=n.y_position) for n in notes]
    
    current_time = time.time()
    scheduler.schedule_from_detection(chords, current_time)
    
    # Wait for execution
    time.sleep(2)
    scheduler.stop()
