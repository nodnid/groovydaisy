#!/usr/bin/env python3
"""
Test script for GroovyDaisy binary protocol.

Usage: python3 test_protocol.py [port]
Default port: /dev/tty.usbmodem*
"""

import serial
import serial.tools.list_ports
import sys
import time
import struct

# Protocol constants
SYNC_BYTE = 0xAA
MSG_TICK = 0x01
MSG_TRANSPORT = 0x02
MSG_VOICES = 0x03
MSG_MIDI_IN = 0x04
MSG_CC_STATE = 0x05
MSG_DEBUG = 0xFF

CMD_PLAY = 0x80
CMD_STOP = 0x81
CMD_RECORD = 0x82
CMD_TEMPO = 0x83
CMD_REQ_STATE = 0x90

MSG_NAMES = {
    MSG_TICK: "TICK",
    MSG_TRANSPORT: "TRANSPORT",
    MSG_VOICES: "VOICES",
    MSG_MIDI_IN: "MIDI_IN",
    MSG_CC_STATE: "CC_STATE",
    MSG_DEBUG: "DEBUG",
}

def find_daisy_port():
    """Find the Daisy USB serial port."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'usbmodem' in port.device.lower():
            return port.device
    return None

def checksum(data):
    """Calculate XOR checksum."""
    result = 0
    for b in data:
        result ^= b
    return result

def build_message(msg_type, payload=b''):
    """Build a binary protocol message."""
    length = len(payload)
    header = bytes([SYNC_BYTE, msg_type, length & 0xFF, (length >> 8) & 0xFF])
    data = bytes([msg_type, length & 0xFF, (length >> 8) & 0xFF]) + payload
    chk = checksum(data)
    return header + payload + bytes([chk])

def parse_message(data):
    """Parse a binary protocol message from buffer."""
    if len(data) < 5:
        return None, data

    # Find sync byte
    try:
        sync_idx = data.index(SYNC_BYTE)
        if sync_idx > 0:
            data = data[sync_idx:]
    except ValueError:
        return None, b''

    if len(data) < 5:
        return None, data

    msg_type = data[1]
    length = data[2] | (data[3] << 8)

    total_len = 4 + length + 1  # header + payload + checksum
    if len(data) < total_len:
        return None, data

    payload = data[4:4+length]
    recv_chk = data[4+length]

    # Verify checksum
    calc_chk = checksum(data[1:4+length])
    if recv_chk != calc_chk:
        # Bad checksum, skip this sync byte
        return None, data[1:]

    # Valid message
    return (msg_type, payload), data[total_len:]

def format_message(msg_type, payload):
    """Format a message for display."""
    name = MSG_NAMES.get(msg_type, f"0x{msg_type:02X}")

    if msg_type == MSG_TICK:
        if len(payload) >= 4:
            tick = struct.unpack('<I', payload[:4])[0]
            return f"{name}: tick={tick}"
    elif msg_type == MSG_DEBUG:
        text = payload.decode('utf-8', errors='replace')
        return f"{name}: {text}"

    # Default: show hex
    hex_str = ' '.join(f'{b:02X}' for b in payload)
    return f"{name}: [{hex_str}]"

def main():
    # Find port
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = find_daisy_port()
        if not port:
            print("No Daisy USB port found!")
            print("Available ports:")
            for p in serial.tools.list_ports.comports():
                print(f"  {p.device}")
            sys.exit(1)

    print(f"Connecting to {port}...")

    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
    except Exception as e:
        print(f"Failed to open port: {e}")
        sys.exit(1)

    print("Connected! Receiving messages (Ctrl+C to stop)...")
    print("-" * 60)

    buffer = b''
    tick_count = 0
    last_tick_print = time.time()

    try:
        while True:
            # Read available data
            data = ser.read(256)
            if data:
                buffer += data

            # Parse messages
            while True:
                msg, buffer = parse_message(buffer)
                if msg is None:
                    break

                msg_type, payload = msg

                # Rate limit TICK messages (show summary every second)
                if msg_type == MSG_TICK:
                    tick_count += 1
                    if time.time() - last_tick_print >= 1.0:
                        if len(payload) >= 4:
                            tick = struct.unpack('<I', payload[:4])[0]
                            print(f"TICK: {tick_count}/sec, counter={tick}")
                        tick_count = 0
                        last_tick_print = time.time()
                else:
                    # Print other messages immediately
                    print(format_message(msg_type, payload))

            time.sleep(0.01)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        ser.close()
        print("Disconnected.")

if __name__ == '__main__':
    main()
