#!/usr/bin/env python3
# Transfer binary files from PC to ESP32 via serial
# Usage: python3 transfer_dataset.py model_name /dev/ttyACM0
# Automatically finds files in ../../data/result/ folder

import os
import sys
import time
import serial
import struct
import binascii
from pathlib import Path

# Transfer timing and size configuration
# IMPORTANT: Keep CHUNK_SIZE and CHUNK_DELAY in sync with the ESP32 receiver sketch.
# - CHUNK_SIZE should match the ESP32's BUFFER_CHUNK
# - CHUNK_DELAY should be long enough so the ESP32 can flush writes between chunks
CHUNK_SIZE = 256
CHUNK_DELAY = 0.02  # seconds between chunks (V2 uses ACKs, so keep small)
MAX_RETRIES = 5

def find_file(filename):
    """Find the file in the ../../data/result folder"""
    script_dir = Path(__file__).parent
    result_dir = script_dir / ".." / ".." / "data" / "result"
    file_path = result_dir / filename
    
    if file_path.exists():
        return file_path
    else:
        raise FileNotFoundError(f"File {filename} not found in {result_dir}")

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


def transfer_file(file_path, port, baudrate=115200):
    """Transfer binary file to ESP32"""
    print(f"Opening serial port {port} at {baudrate} baud...")

    try:
        ser = serial.Serial(port, baudrate, timeout=5)
        time.sleep(2)  # Wait for ESP32 to initialize

        # Read the binary file
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
        else:
            raise Exception(f"Transfer failed: {response}")

    except serial.SerialException as e:
        print(f"Serial error: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        if 'ser' in locals():
            ser.close()

def main():
    # Accept model name and serial port, then derive <model_name>_nml.bin automatically
    if len(sys.argv) != 3:
        print("Usage: python3 transfer_dataset.py <model_name> <serial_port>")
        print("Example: python3 transfer_dataset.py digit_model /dev/ttyACM0")
        sys.exit(1)

    model_name = sys.argv[1]
    port = sys.argv[2]

    # Interpolate model file name
    filename = f"{model_name}_nml.bin"

    try:
        file_path = find_file(filename)
        print(f"Found file: {file_path}")
        transfer_file(file_path, port)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()