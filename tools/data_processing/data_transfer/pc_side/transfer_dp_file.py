#!/usr/bin/env python3
"""
Dataset Parameters Transfer Script for ESP32 with V2 Protocol
Transfers CSV files with CRC verification and ACK/NACK

Usage: python3 transfer_dp_file.py <model_name> <serial_port>

PERFORMANCE NOTES:
  - CHUNK_SIZE is automatically synchronized with Rf_board_config.h
  - Board-specific defaults:
    * ESP32-C3/C6: 220 bytes (USB CDC buffer constraint)
    * ESP32-S3: 256 bytes (larger buffer)
    * ESP32: 256 bytes (standard board)
"""

import serial
import time
import sys
import os
import struct
import binascii
from pathlib import Path

# Import configuration parser to sync with ESP32
try:
    from config_parser import get_user_chunk_size
except ImportError:
    print("⚠️  Warning: config_parser.py not found. Using default CHUNK_SIZE.")
    def get_user_chunk_size(config_file_path=None, default=220):
        return default

# CHUNK_SIZE is now automatically extracted from Rf_board_config.h
CHUNK_SIZE = get_user_chunk_size(default=220)  # Auto-synced with Rf_board_config.h
CHUNK_DELAY = 0.02  # seconds
MAX_RETRIES = 5

def _readline_with_timeout(ser, timeout_s=5.0):
    """Read a line with a soft timeout; returns stripped string (can be empty)."""
    end_time = time.time() + timeout_s
    line = b""
    while time.time() < end_time:
        part = ser.readline()
        if part:
            line = part
            break
        time.sleep(0.01)
    return line.decode(errors='ignore').strip()

def find_file(model_name):
    """Find the dataset params CSV file in the ../../data/result folder"""
    script_dir = Path(__file__).parent
    result_dir = script_dir / ".." / ".." / "data" / "result"
    filename = f"{model_name}_dp.csv"
    file_path = result_dir / filename
    
    if file_path.exists():
        return file_path
    else:
        raise FileNotFoundError(f"File {filename} not found in {result_dir}")

def transfer_csv_file(file_path, port, baudrate=115200):
    """Transfer CSV file to ESP32 with V2 protocol (CRC + ACK/NACK)"""
    print(f"Opening serial port {port} at {baudrate} baud...")

    try:
        ser = serial.Serial(port, baudrate, timeout=5)
        time.sleep(2)  # Wait for ESP32 to initialize

        # Read the file as binary
        with open(file_path, 'rb') as f:
            file_data = f.read()

        file_size = len(file_data)
        filename = file_path.name

        print(f"Transferring {filename} ({file_size} bytes)...")

        # Compute full file CRC32
        file_crc = binascii.crc32(file_data) & 0xFFFFFFFF

        # Handshake and metadata (V2 protocol)
        ser.reset_input_buffer()
        ser.write(b"TRANSFER_V2\n")
        time.sleep(0.05)

        filename_bytes = filename.encode('utf-8')
        ser.write(struct.pack('<I', len(filename_bytes)))
        ser.write(filename_bytes)
        ser.write(struct.pack('<I', file_size))
        ser.write(struct.pack('<I', file_crc))
        ser.write(struct.pack('<I', CHUNK_SIZE))

        # Wait for ESP32 ready signal
        response = _readline_with_timeout(ser, timeout_s=8.0)
        if response != "READY_V2":
            raise Exception(f"Unexpected response: {response}")

        print("ESP32 ready, sending file data with CRC and ACKs...")

        # Send file data in chunks with per-chunk CRC and ACK/NACK
        bytes_sent = 0

        for offset in range(0, file_size, CHUNK_SIZE):
            chunk = file_data[offset:offset + CHUNK_SIZE]
            chunk_len = len(chunk)
            chunk_crc = binascii.crc32(chunk) & 0xFFFFFFFF
            header = struct.pack('<III', offset, chunk_len, chunk_crc)

            success = False
            for attempt in range(1, MAX_RETRIES + 1):
                ser.write(header)
                ser.write(chunk)
                ser.flush()

                # Wait for ACK/NACK
                line = _readline_with_timeout(ser, timeout_s=5.0)
                if line.startswith("ACK "):
                    try:
                        ack_off = int(line.split()[1])
                    except Exception:
                        ack_off = -1
                    if ack_off == offset:
                        success = True
                        break
                elif line.startswith("NACK "):
                    if line:
                        print(f"   ↩ {line}")
                    # Retry
                    continue
                # Anything else: small delay and retry
                time.sleep(0.05)

            if not success:
                raise Exception(f"Chunk transfer failed at offset {offset}")

            bytes_sent += chunk_len
            progress = (bytes_sent / file_size) * 100
            print(f"\rProgress: {progress:.1f}% ({bytes_sent}/{file_size} bytes)", end="")
            time.sleep(CHUNK_DELAY)

        # End transfer
        ser.write(b"TRANSFER_END\n")
        print("\nWaiting for ESP32 final confirmation...")

        response = _readline_with_timeout(ser, timeout_s=10.0)
        if response == "TRANSFER_COMPLETE":
            print(f"✓ File {filename} transferred successfully!")
            return True
        else:
            raise Exception(f"Transfer failed: {response}")

    except serial.SerialException as e:
        print(f"Serial error: {e}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        if 'ser' in locals():
            ser.close()

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 transfer_dp_file.py <model_name> <serial_port>")
        print("Example: python3 transfer_dp_file.py digit_model /dev/ttyACM0")
        sys.exit(1)

    model_name = sys.argv[1]
    port = sys.argv[2]

    try:
        file_path = find_file(model_name)
        print(f"Found file: {file_path}")
        if transfer_csv_file(file_path, port):
            print("✅ SUCCESS: Data sent to ESP32")
        else:
            print("❌ FAILED: Could not send data")
            sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()