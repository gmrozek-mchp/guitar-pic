"""
Serial Communication Module

Handles communication with the PIC microcontroller over USB/UART.
"""

import serial
import serial.tools.list_ports
from typing import Optional, List
import struct
import time
from enum import IntEnum


class Command(IntEnum):
    """Command types for PIC communication."""
    NOTE = 0x01          # Press frets and strum
    FRET_DOWN = 0x02     # Press fret buttons
    FRET_UP = 0x03       # Release fret buttons
    STRUM = 0x04         # Strum only
    WHAMMY = 0x05        # Set whammy position
    WHAMMY_OSC = 0x06    # Oscillate whammy
    TILT = 0x07          # Activate tilt
    PING = 0x10          # Connection test
    CALIBRATE = 0x11     # Enter calibration mode


class StumDirection(IntEnum):
    """Strum direction values."""
    DOWN = 0
    UP = 1


# Protocol constants
START_BYTE = 0xAA
END_BYTE = 0x55


class PICSerial:
    """
    Serial communication with PIC microcontroller.
    
    Protocol format:
    [START][CMD][PAYLOAD...][CHECKSUM][END]
    
    - START: 0xAA
    - CMD: Command byte
    - PAYLOAD: 1-4 bytes depending on command
    - CHECKSUM: XOR of CMD and all payload bytes
    - END: 0x55
    """
    
    def __init__(self, port: Optional[str] = None, baudrate: int = 115200):
        """
        Initialize serial communication.
        
        Args:
            port: Serial port name (e.g., '/dev/ttyUSB0', 'COM3')
                 If None, will attempt auto-detection
            baudrate: Communication speed (default 115200)
        """
        self.port = port
        self.baudrate = baudrate
        self.serial: Optional[serial.Serial] = None
        self._connected = False
        
    @staticmethod
    def list_ports() -> List[str]:
        """List available serial ports."""
        ports = serial.tools.list_ports.comports()
        return [p.device for p in ports]
    
    @staticmethod
    def find_pic_port() -> Optional[str]:
        """
        Attempt to find the PIC's serial port.
        
        Returns:
            Port name if found, None otherwise
        """
        ports = serial.tools.list_ports.comports()
        
        for port in ports:
            # Look for common PIC/USB identifiers
            desc = (port.description + port.manufacturer).lower()
            if any(x in desc for x in ['pic', 'microchip', 'usb serial', 'ft232']):
                return port.device
        
        # Return first port if no match found
        if ports:
            return ports[0].device
        return None
    
    def connect(self) -> bool:
        """
        Open serial connection.
        
        Returns:
            True if connection successful
        """
        if self.port is None:
            self.port = self.find_pic_port()
            if self.port is None:
                print("Error: No serial port found")
                return False
        
        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1.0
            )
            
            # Wait for Arduino/PIC to reset (some boards reset on connect)
            time.sleep(2)
            
            # Clear any startup messages
            self.serial.reset_input_buffer()
            
            # Test connection
            if self.ping():
                self._connected = True
                print(f"Connected to PIC on {self.port}")
                return True
            else:
                print("Warning: Connected but no response to ping")
                self._connected = True  # Still try to use it
                return True
                
        except serial.SerialException as e:
            print(f"Error connecting to {self.port}: {e}")
            return False
    
    def disconnect(self) -> None:
        """Close serial connection."""
        if self.serial is not None:
            self.serial.close()
            self.serial = None
        self._connected = False
        
    def is_connected(self) -> bool:
        """Check if connected."""
        return self._connected and self.serial is not None and self.serial.is_open
    
    def _calculate_checksum(self, data: bytes) -> int:
        """Calculate XOR checksum of data."""
        checksum = 0
        for byte in data:
            checksum ^= byte
        return checksum
    
    def _send_command(self, cmd: Command, payload: bytes = b'') -> bool:
        """
        Send a command to the PIC.
        
        Args:
            cmd: Command type
            payload: Command payload bytes
            
        Returns:
            True if send successful
        """
        if not self.is_connected():
            return False
        
        # Build packet
        data = bytes([cmd]) + payload
        checksum = self._calculate_checksum(data)
        packet = bytes([START_BYTE]) + data + bytes([checksum, END_BYTE])
        
        try:
            self.serial.write(packet)
            return True
        except serial.SerialException as e:
            print(f"Send error: {e}")
            return False
    
    def _read_response(self, timeout: float = 0.1) -> Optional[bytes]:
        """
        Read response from PIC.
        
        Args:
            timeout: Read timeout in seconds
            
        Returns:
            Response payload or None if no response
        """
        if not self.is_connected():
            return None
            
        old_timeout = self.serial.timeout
        self.serial.timeout = timeout
        
        try:
            # Look for start byte
            while True:
                byte = self.serial.read(1)
                if not byte:
                    return None
                if byte[0] == START_BYTE:
                    break
            
            # Read command and payload (assume max 6 bytes for response)
            data = self.serial.read(6)
            if len(data) < 3:  # Minimum: cmd + checksum + end
                return None
            
            # Find end byte
            end_idx = -1
            for i in range(len(data) - 1, 0, -1):
                if data[i] == END_BYTE:
                    end_idx = i
                    break
            
            if end_idx < 2:
                return None
            
            # Extract payload (excluding checksum and end)
            payload = data[:end_idx-1]
            checksum = data[end_idx-1]
            
            # Verify checksum
            if self._calculate_checksum(payload) != checksum:
                return None
            
            return payload
            
        finally:
            self.serial.timeout = old_timeout
    
    # High-level commands
    
    def ping(self) -> bool:
        """
        Test connection with ping command.
        
        Returns:
            True if PIC responds
        """
        if not self._send_command(Command.PING):
            return False
        
        response = self._read_response(timeout=0.5)
        return response is not None
    
    def note(self, frets: int, strum: StumDirection, hold_ms: int = 50) -> bool:
        """
        Play a note (press frets, strum, hold, release).
        
        Args:
            frets: Bitmask of frets to press (bit 0=green, bit 4=orange)
            strum: Strum direction
            hold_ms: How long to hold frets in milliseconds
            
        Returns:
            True if command sent successfully
        """
        # Pack: frets (1 byte) + strum (1 byte) + hold_ms (2 bytes)
        payload = struct.pack('<BBH', frets, strum, hold_ms)
        return self._send_command(Command.NOTE, payload)
    
    def fret_down(self, frets: int) -> bool:
        """
        Press fret buttons (no strum).
        
        Args:
            frets: Bitmask of frets to press
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.FRET_DOWN, bytes([frets]))
    
    def fret_up(self, frets: int) -> bool:
        """
        Release fret buttons.
        
        Args:
            frets: Bitmask of frets to release
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.FRET_UP, bytes([frets]))
    
    def strum(self, direction: StumDirection) -> bool:
        """
        Strum only (no fret change).
        
        Args:
            direction: Strum direction
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.STRUM, bytes([direction]))
    
    def whammy(self, position: int) -> bool:
        """
        Set whammy bar position.
        
        Args:
            position: Whammy position 0-255 (128 = center)
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.WHAMMY, bytes([position & 0xFF]))
    
    def whammy_oscillate(self, speed: int, amplitude: int) -> bool:
        """
        Start whammy oscillation.
        
        Args:
            speed: Oscillation speed 0-255
            amplitude: Oscillation amplitude 0-255
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.WHAMMY_OSC, bytes([speed, amplitude]))
    
    def tilt(self, active: bool) -> bool:
        """
        Activate or deactivate tilt (star power).
        
        Args:
            active: True to tilt, False to return to normal
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.TILT, bytes([1 if active else 0]))
    
    def calibrate(self, mode: int = 0) -> bool:
        """
        Enter calibration mode.
        
        Args:
            mode: Calibration mode (0 = full calibration)
            
        Returns:
            True if command sent successfully
        """
        return self._send_command(Command.CALIBRATE, bytes([mode]))


# Convenience functions for fret masks
def fret_mask(*frets) -> int:
    """
    Create fret bitmask from fret numbers.
    
    Args:
        frets: Fret numbers (0=green, 1=red, 2=yellow, 3=blue, 4=orange)
        
    Returns:
        Bitmask
        
    Example:
        fret_mask(0, 2)  # Green + Yellow = 0b00101 = 5
    """
    mask = 0
    for fret in frets:
        mask |= (1 << fret)
    return mask


# Named fret constants
FRET_GREEN = 0b00001
FRET_RED = 0b00010
FRET_YELLOW = 0b00100
FRET_BLUE = 0b01000
FRET_ORANGE = 0b10000


# Example usage
if __name__ == "__main__":
    print("Available serial ports:", PICSerial.list_ports())
    
    # Try to connect
    pic = PICSerial()
    
    if pic.connect():
        print("Connection successful!")
        
        # Test sequence
        print("Testing frets...")
        for i in range(5):
            pic.note(1 << i, StumDirection.DOWN, 100)
            time.sleep(0.2)
        
        print("Testing chord...")
        pic.note(FRET_GREEN | FRET_RED | FRET_YELLOW, StumDirection.DOWN, 200)
        time.sleep(0.5)
        
        print("Testing whammy...")
        pic.whammy(200)
        time.sleep(0.5)
        pic.whammy(128)
        
        pic.disconnect()
    else:
        print("Could not connect to PIC")
