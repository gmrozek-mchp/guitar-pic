# Timing Calibration Guide

## Overview

Accurate note timing is critical for Guitar Hero success. This guide explains how to measure and compensate for system latency to achieve consistent "Good" or "Perfect" ratings.

---

## Understanding System Latency

Total latency is the time from when a note appears on screen to when the button press registers in the game.

### Latency Components

```
┌─────────────────────────────────────────────────────────────────┐
│                      Total System Latency                       │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────┤
│   Video     │  Vision     │   Serial    │    PIC      │ Mechan- │
│   Capture   │  Process    │   Comm      │  Process    │  ical   │
│  30-100ms   │  10-30ms    │   1-5ms     │   <1ms      │ 10-20ms │
└─────────────┴─────────────┴─────────────┴─────────────┴─────────┘
                                                    Total: 50-160ms
```

| Component | Description | Typical Range |
|-----------|-------------|---------------|
| Video Capture | Frame grabbed from capture device | 30-100ms |
| Vision Processing | OpenCV note detection | 10-30ms |
| Serial Communication | Command sent over USB/UART | 1-5ms |
| PIC Processing | Command parsed and queued | <1ms |
| Mechanical | Solenoid actuates button | 10-20ms |

---

## Calibration Methods

### Method 1: Visual Calibration (Recommended)

Uses the in-game calibration tool.

1. **Access calibration** in Guitar Hero settings menu
2. **Let the bot attempt to hit notes** without compensation
3. **Observe hit indicator**:
   - If hits register EARLY → reduce compensation
   - If hits register LATE → increase compensation
4. **Adjust offset** and repeat until centered

### Method 2: Oscilloscope Measurement

Precise measurement of each component.

**Equipment needed**:
- Oscilloscope (or logic analyzer)
- Test points on solenoid driver

**Procedure**:

1. **Measure video latency**:
   - Display a flashing pattern on PS2
   - Compare capture timestamp to display change
   - Record delay

2. **Measure processing latency**:
   - Add timestamps in Python code
   - Log time from frame capture to command send
   
3. **Measure mechanical latency**:
   - Trigger solenoid via PIC
   - Measure time from command to physical button press
   - Use oscilloscope on driver output vs. button contact

### Method 3: Audio Sync

Use game audio as timing reference.

1. **Capture audio** alongside video
2. **Detect beat/note sounds** in audio stream
3. **Compare** visual note position to audio timing
4. **Calculate** the video-to-audio offset

---

## Compensation Implementation

### In Vision System (Python)

```python
# calibration.py

class LatencyCompensator:
    def __init__(self):
        # Measured latency values (in milliseconds)
        self.video_latency = 50      # Adjust after measurement
        self.processing_latency = 15
        self.serial_latency = 3
        self.mechanical_latency = 15
        
    @property
    def total_latency(self):
        return (self.video_latency + 
                self.processing_latency + 
                self.serial_latency + 
                self.mechanical_latency)
    
    def get_lead_time(self, note_distance, scroll_speed):
        """
        Calculate when to send command based on note position.
        
        Args:
            note_distance: Pixels from note to strike line
            scroll_speed: Pixels per millisecond
            
        Returns:
            Milliseconds until command should be sent
        """
        time_to_strike = note_distance / scroll_speed
        lead_time = time_to_strike - self.total_latency
        return max(0, lead_time)
```

### In PIC Firmware (C)

```c
// timing.c

// Pre-calculated delay to compensate for remaining latency
volatile uint16_t command_delay_ms = 0;

void schedule_note(uint8_t frets, uint8_t strum, uint16_t delay_ms) {
    // Store command for delayed execution
    pending_frets = frets;
    pending_strum = strum;
    command_delay_ms = delay_ms;
    
    // Start countdown timer
    start_delay_timer(delay_ms);
}

void delay_timer_isr(void) {
    // Timer expired, execute the pending command
    execute_note(pending_frets, pending_strum);
}
```

---

## Calibration Procedure

### Step 1: Initial Measurement

1. Set all compensation values to 0
2. Run the bot on a slow song (Easy difficulty)
3. Observe whether hits are early or late

### Step 2: Coarse Adjustment

| Observation | Action |
|-------------|--------|
| All hits early | Increase delay by 20ms |
| All hits late | Decrease delay by 20ms |
| Random timing | Check for frame drops or processing spikes |

### Step 3: Fine Tuning

1. Adjust in 5ms increments
2. Test on multiple songs
3. Target: 90%+ "Good" or better ratings

### Step 4: Scroll Speed Calibration

Different songs/difficulties may have different scroll speeds.

1. **Measure scroll speed**:
   - Track a note across multiple frames
   - Calculate pixels per frame
   - Convert to pixels per millisecond

2. **Store per-song or per-difficulty**:
   ```python
   SCROLL_SPEEDS = {
       'easy': 0.3,    # pixels per ms
       'medium': 0.4,
       'hard': 0.5,
       'expert': 0.6,
   }
   ```

---

## Troubleshooting

### Inconsistent Timing

**Symptoms**: Some notes hit perfectly, others miss badly

**Possible causes**:
1. **Frame drops** in video capture
   - Check CPU usage
   - Lower processing resolution
   
2. **Variable scroll speed**
   - Some songs have speed changes
   - Implement dynamic scroll detection

3. **Serial buffer delays**
   - Increase baud rate
   - Reduce command size

### All Notes Late

**Symptoms**: Notes consistently register after the beat

**Solutions**:
1. Reduce total compensation value
2. Check if video capture has additional buffering
3. Verify solenoid is responding quickly

### All Notes Early

**Symptoms**: Notes consistently register before the beat

**Solutions**:
1. Increase total compensation value
2. Add delay in PIC firmware
3. Check if detection is triggering too soon

### First Notes of Song Miss

**Symptoms**: First few notes miss, then timing is correct

**Cause**: System needs to "warm up" / fill processing pipeline

**Solution**: Pre-load compensation, start tracking before first note

---

## Logging for Analysis

Add detailed logging to help diagnose timing issues:

```python
import csv
from datetime import datetime

class TimingLogger:
    def __init__(self, filename="timing_log.csv"):
        self.file = open(filename, 'w', newline='')
        self.writer = csv.writer(self.file)
        self.writer.writerow([
            'timestamp', 'note_id', 'detected_at', 'scheduled_for',
            'executed_at', 'game_result'
        ])
    
    def log_note(self, note_id, detected, scheduled, executed, result):
        self.writer.writerow([
            datetime.now().isoformat(),
            note_id,
            detected,
            scheduled,
            executed,
            result
        ])
```

---

## Reference Values

### Typical Working Values

| Capture Device | Video Latency |
|----------------|---------------|
| EasyCap clone | 80-100ms |
| **Elgato Game Capture HD** | **50-70ms** ◄ Selected |
| Elgato HD60 S+ | 30-40ms |
| AverMedia | 40-60ms |

| PIC Configuration | Processing Time |
|-------------------|-----------------|
| 8MHz internal | <2ms |
| 20MHz external | <1ms |
| 48MHz (USB mode) | <0.5ms |

| Solenoid Type | Response Time |
|---------------|---------------|
| 5V small | 15-25ms |
| 12V small | 10-15ms |
| 12V with driver | 8-12ms |

---

## Calibration Checklist

- [ ] Measure video capture latency
- [ ] Measure vision processing time
- [ ] Measure serial round-trip time
- [ ] Measure mechanical response time
- [ ] Calculate total latency
- [ ] Implement compensation in code
- [ ] Test on Easy difficulty
- [ ] Fine-tune with 5ms adjustments
- [ ] Verify on multiple songs
- [ ] Document final values
