#!/usr/bin/env python3
"""
Guitar Hero Bot - Main Vision Loop

Captures video, detects notes, and sends commands to PIC controller.
"""

import argparse
import time
import sys
from typing import Optional

import cv2

from capture import VideoCapture, FrameBuffer
from detector import NoteDetector, visualize_detection
from scheduler import NoteScheduler, LatencyConfig, StumDirection
from serial_comm import PICSerial


class GuitarHeroBot:
    """
    Main controller for the Guitar Hero playing bot.
    
    Coordinates video capture, note detection, timing, and actuation.
    """
    
    def __init__(
        self,
        video_device: int = 0,
        serial_port: Optional[str] = None,
        debug_display: bool = False
    ):
        """
        Initialize the bot.
        
        Args:
            video_device: Video capture device index
            serial_port: Serial port for PIC (auto-detect if None)
            debug_display: Show debug visualization window
        """
        self.video_device = video_device
        self.serial_port = serial_port
        self.debug_display = debug_display
        
        # Components
        self.capture: Optional[VideoCapture] = None
        self.detector: Optional[NoteDetector] = None
        self.scheduler: Optional[NoteScheduler] = None
        self.pic: Optional[PICSerial] = None
        
        # Configuration
        self.latency = LatencyConfig()
        
        # State
        self._running = False
        self._paused = False
        
        # Statistics
        self.stats = {
            'frames_processed': 0,
            'notes_detected': 0,
            'notes_scheduled': 0,
            'commands_sent': 0,
            'start_time': 0,
        }
        
    def initialize(self) -> bool:
        """
        Initialize all components.
        
        Returns:
            True if initialization successful
        """
        print("Initializing Guitar Hero Bot...")
        
        # Initialize video capture
        print(f"Opening video device {self.video_device}...")
        self.capture = VideoCapture(device_id=self.video_device)
        if not self.capture.open():
            print("Failed to open video capture")
            return False
        
        # Initialize note detector
        print("Initializing note detector...")
        self.detector = NoteDetector()
        
        # Initialize serial communication
        print("Connecting to PIC controller...")
        self.pic = PICSerial(port=self.serial_port)
        if not self.pic.connect():
            print("Warning: Could not connect to PIC - running in simulation mode")
            # Continue anyway for testing
        
        # Initialize scheduler with command callback
        print("Initializing scheduler...")
        self.scheduler = NoteScheduler(
            latency_config=self.latency,
            command_callback=self._on_command
        )
        
        print("Initialization complete!")
        return True
    
    def shutdown(self) -> None:
        """Shutdown all components."""
        print("\nShutting down...")
        
        if self.scheduler:
            self.scheduler.stop()
            
        if self.pic:
            self.pic.disconnect()
            
        if self.capture:
            self.capture.close()
            
        if self.debug_display:
            cv2.destroyAllWindows()
            
        print("Shutdown complete")
        
    def _on_command(self, frets: int, strum: StumDirection, hold_ms: int) -> None:
        """
        Callback when scheduler triggers a command.
        
        Args:
            frets: Fret bitmask
            strum: Strum direction
            hold_ms: Hold duration
        """
        self.stats['commands_sent'] += 1
        
        if self.pic and self.pic.is_connected():
            self.pic.note(frets, strum, hold_ms)
        
    def calibrate(self) -> bool:
        """
        Run calibration routine.
        
        Returns:
            True if calibration successful
        """
        print("\n=== Calibration Mode ===")
        print("Please ensure Guitar Hero is at the main menu or a song start screen.")
        print("Press 'c' to capture calibration frame, 'q' to finish.")
        
        if not self.capture or not self.detector:
            print("Error: Components not initialized")
            return False
        
        while True:
            ret, frame = self.capture.read_frame()
            if not ret:
                continue
                
            # Show frame
            cv2.imshow("Calibration", frame)
            
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                break
            elif key == ord('c'):
                # Calibrate highway detection
                if self.detector.calibrate_highway(frame):
                    print("Highway calibration successful!")
                    print(f"Region: {self.detector.highway}")
                else:
                    print("Highway calibration failed")
                    
        cv2.destroyAllWindows()
        return True
    
    def run(self) -> None:
        """Run the main detection and actuation loop."""
        if not self.capture or not self.detector or not self.scheduler:
            print("Error: Components not initialized")
            return
            
        print("\n=== Starting Guitar Hero Bot ===")
        print("Press 'q' to quit, 'p' to pause/resume, 'd' to toggle debug display")
        
        self._running = True
        self.stats['start_time'] = time.time()
        self.scheduler.start()
        
        try:
            while self._running:
                # Capture frame
                ret, frame = self.capture.read_frame()
                if not ret:
                    print("Failed to capture frame")
                    time.sleep(0.01)
                    continue
                    
                current_time = time.time()
                self.stats['frames_processed'] += 1
                
                if not self._paused:
                    # Detect notes
                    game_state = self.detector.detect_game_state(frame)
                    self.stats['notes_detected'] += len(game_state.notes)
                    
                    # Schedule actions
                    scheduled = self.scheduler.schedule_from_detection(
                        game_state.chords,
                        current_time
                    )
                    self.stats['notes_scheduled'] += scheduled
                
                # Debug display
                if self.debug_display:
                    vis_frame = visualize_detection(
                        frame,
                        game_state.notes if not self._paused else [],
                        self.detector.highway
                    )
                    
                    # Add stats overlay
                    self._draw_stats(vis_frame)
                    
                    cv2.imshow("Guitar Hero Bot", vis_frame)
                
                # Handle keyboard input
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q'):
                    self._running = False
                elif key == ord('p'):
                    self._paused = not self._paused
                    print("Paused" if self._paused else "Resumed")
                elif key == ord('d'):
                    self.debug_display = not self.debug_display
                    if not self.debug_display:
                        cv2.destroyAllWindows()
                        
        except KeyboardInterrupt:
            print("\nInterrupted by user")
        finally:
            self._running = False
            self._print_stats()
            
    def _draw_stats(self, frame) -> None:
        """Draw statistics overlay on frame."""
        elapsed = time.time() - self.stats['start_time']
        fps = self.stats['frames_processed'] / max(elapsed, 1)
        
        stats_text = [
            f"FPS: {fps:.1f}",
            f"Notes: {self.stats['notes_detected']}",
            f"Scheduled: {self.stats['notes_scheduled']}",
            f"Commands: {self.stats['commands_sent']}",
        ]
        
        if self._paused:
            stats_text.insert(0, "PAUSED")
        
        y = 30
        for text in stats_text:
            cv2.putText(frame, text, (10, y), 
                       cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
            y += 25
            
    def _print_stats(self) -> None:
        """Print final statistics."""
        elapsed = time.time() - self.stats['start_time']
        
        print("\n=== Session Statistics ===")
        print(f"Duration: {elapsed:.1f} seconds")
        print(f"Frames processed: {self.stats['frames_processed']}")
        print(f"Average FPS: {self.stats['frames_processed'] / max(elapsed, 1):.1f}")
        print(f"Notes detected: {self.stats['notes_detected']}")
        print(f"Notes scheduled: {self.stats['notes_scheduled']}")
        print(f"Commands sent: {self.stats['commands_sent']}")


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Guitar Hero Bot - Automatic Guitar Hero player"
    )
    parser.add_argument(
        '-v', '--video-device',
        type=int,
        default=0,
        help="Video capture device index (default: 0)"
    )
    parser.add_argument(
        '-p', '--serial-port',
        type=str,
        default=None,
        help="Serial port for PIC controller (auto-detect if not specified)"
    )
    parser.add_argument(
        '-d', '--debug',
        action='store_true',
        help="Show debug visualization window"
    )
    parser.add_argument(
        '-c', '--calibrate',
        action='store_true',
        help="Run calibration mode"
    )
    parser.add_argument(
        '--list-devices',
        action='store_true',
        help="List available video and serial devices"
    )
    
    args = parser.parse_args()
    
    # List devices mode
    if args.list_devices:
        from capture import list_capture_devices
        print("Video capture devices:", list_capture_devices())
        print("Serial ports:", PICSerial.list_ports())
        return
    
    # Create and run bot
    bot = GuitarHeroBot(
        video_device=args.video_device,
        serial_port=args.serial_port,
        debug_display=args.debug
    )
    
    if not bot.initialize():
        print("Initialization failed")
        sys.exit(1)
    
    try:
        if args.calibrate:
            bot.calibrate()
        else:
            bot.run()
    finally:
        bot.shutdown()


if __name__ == "__main__":
    main()
