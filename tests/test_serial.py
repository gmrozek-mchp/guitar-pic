"""
Serial Communication Unit Tests

Tests for PIC serial protocol.
"""

import pytest
import sys
import os

# Add vision module to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'vision'))

from serial_comm import (
    PICSerial, Command, StumDirection,
    fret_mask, FRET_GREEN, FRET_RED, FRET_YELLOW, FRET_BLUE, FRET_ORANGE
)


class TestFretMask:
    """Tests for fret mask helper functions."""
    
    def test_fret_mask_single(self):
        """Test creating single fret mask."""
        assert fret_mask(0) == 0b00001  # Green
        assert fret_mask(1) == 0b00010  # Red
        assert fret_mask(4) == 0b10000  # Orange
    
    def test_fret_mask_multiple(self):
        """Test creating multi-fret mask."""
        # Green + Yellow
        assert fret_mask(0, 2) == 0b00101
        
        # All frets
        assert fret_mask(0, 1, 2, 3, 4) == 0b11111
    
    def test_fret_constants(self):
        """Test fret constant values."""
        assert FRET_GREEN == 0b00001
        assert FRET_RED == 0b00010
        assert FRET_YELLOW == 0b00100
        assert FRET_BLUE == 0b01000
        assert FRET_ORANGE == 0b10000
    
    def test_fret_combinations(self):
        """Test combining fret constants."""
        # Power chord: Green + Red
        chord = FRET_GREEN | FRET_RED
        assert chord == 0b00011
        
        # Full chord
        full = FRET_GREEN | FRET_RED | FRET_YELLOW | FRET_BLUE | FRET_ORANGE
        assert full == 0b11111


class TestCommand:
    """Tests for Command enum."""
    
    def test_command_values(self):
        """Test command byte values."""
        assert Command.NOTE == 0x01
        assert Command.FRET_DOWN == 0x02
        assert Command.FRET_UP == 0x03
        assert Command.STRUM == 0x04
        assert Command.WHAMMY == 0x05
        assert Command.PING == 0x10


class TestChecksum:
    """Tests for checksum calculation."""
    
    def test_checksum_single_byte(self):
        """Test checksum of single byte."""
        pic = PICSerial()
        assert pic._calculate_checksum(bytes([0x01])) == 0x01
        assert pic._calculate_checksum(bytes([0xFF])) == 0xFF
    
    def test_checksum_multiple_bytes(self):
        """Test checksum of multiple bytes."""
        pic = PICSerial()
        # XOR of 0x01, 0x03, 0x01
        data = bytes([0x01, 0x03, 0x01])
        expected = 0x01 ^ 0x03 ^ 0x01
        assert pic._calculate_checksum(data) == expected
    
    def test_checksum_zeros(self):
        """Test checksum with zeros."""
        pic = PICSerial()
        assert pic._calculate_checksum(bytes([0x00, 0x00])) == 0x00


class TestStumDirection:
    """Tests for strum direction."""
    
    def test_direction_values(self):
        """Test strum direction values."""
        assert StumDirection.DOWN == 0
        assert StumDirection.UP == 1


class TestPICSerial:
    """Tests for PICSerial class."""
    
    def test_initialization(self):
        """Test serial initialization."""
        pic = PICSerial(port="/dev/test", baudrate=115200)
        assert pic.port == "/dev/test"
        assert pic.baudrate == 115200
        assert pic.serial is None
    
    def test_not_connected_by_default(self):
        """Test is_connected returns false by default."""
        pic = PICSerial()
        assert pic.is_connected() is False
    
    def test_list_ports_returns_list(self):
        """Test list_ports returns a list."""
        ports = PICSerial.list_ports()
        assert isinstance(ports, list)


class TestProtocol:
    """Tests for protocol packet building."""
    
    def test_note_command_structure(self):
        """Test NOTE command payload structure."""
        # NOTE command should have: frets (1) + strum (1) + hold_ms (2) = 4 bytes
        # Protocol: [START][CMD][frets][strum][hold_lo][hold_hi][CHECKSUM][END]
        
        pic = PICSerial()
        
        # Build payload manually
        frets = FRET_GREEN | FRET_RED  # 0x03
        strum = StumDirection.DOWN     # 0x00
        hold_ms = 100                  # 0x64, 0x00 (little endian)
        
        import struct
        payload = struct.pack('<BBH', frets, strum, hold_ms)
        
        assert len(payload) == 4
        assert payload[0] == 0x03  # frets
        assert payload[1] == 0x00  # strum
        assert payload[2] == 0x64  # hold_ms low byte
        assert payload[3] == 0x00  # hold_ms high byte


class MockSerial:
    """Mock serial port for testing."""
    
    def __init__(self):
        self.written_data = bytearray()
        self.read_data = bytearray()
        self._is_open = True
    
    def write(self, data):
        self.written_data.extend(data)
        return len(data)
    
    def read(self, size):
        result = bytes(self.read_data[:size])
        self.read_data = self.read_data[size:]
        return result
    
    def is_open(self):
        return self._is_open
    
    def close(self):
        self._is_open = False


class TestWithMockSerial:
    """Tests using mock serial port."""
    
    def test_send_ping_command(self):
        """Test sending ping command."""
        pic = PICSerial()
        pic.serial = MockSerial()
        pic._connected = True
        
        # Send ping
        result = pic._send_command(Command.PING)
        
        assert result is True
        
        # Verify packet structure
        data = pic.serial.written_data
        assert data[0] == 0xAA  # START
        assert data[1] == Command.PING  # CMD
        # No payload for ping
        assert data[2] == Command.PING  # Checksum (just CMD)
        assert data[3] == 0x55  # END
    
    def test_send_fret_down_command(self):
        """Test sending fret down command."""
        pic = PICSerial()
        pic.serial = MockSerial()
        pic._connected = True
        
        # Send fret down for green
        result = pic.fret_down(FRET_GREEN)
        
        assert result is True
        
        data = pic.serial.written_data
        assert data[0] == 0xAA  # START
        assert data[1] == Command.FRET_DOWN  # CMD
        assert data[2] == FRET_GREEN  # Payload: frets
        # Checksum = CMD XOR frets
        assert data[3] == (Command.FRET_DOWN ^ FRET_GREEN)
        assert data[4] == 0x55  # END


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
