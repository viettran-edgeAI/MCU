#!/usr/bin/env python3
"""
Unified ESP32 Data Transfer Utility

This script sends a complete dataset (categorizer, parameters, and binary data)
to an ESP32 device in a single, coordinated process.

Usage:
  python3 unified_transfer.py <base_name> <serial_port>

Example:
  python3 unified_transfer.py digit_data /dev/ttyUSB0
"""

import serial
import time
import os
import sys
import struct
from pathlib import Path

# --- Protocol Constants ---
# Must match the ESP32 receiver sketch
CMD_HEADER = b"ESP32_XFER"
CMD_START_SESSION = 0x01
CMD_FILE_INFO = 0x02
CMD_FILE_CHUNK = 0x03
CMD_END_SESSION = 0x04

RESP_ACK = b"ACK"
RESP_READY = b"READY"
RESP_OK = b"OK"
RESP_ERROR = b"ERROR"

# The following 2 parameters must match exactly esp32 side sketch
# changed them when transfer process failed
CHUNK_SIZE = 256 # bytes per chunk - further reduced for USB CDC compatibility
CHUNK_DELAY = 50  # delay between chunks in ms - increased for USB CDC stability

# Timeout settings
SERIAL_TIMEOUT = 10  # seconds - increased for USB CDC
ACK_TIMEOUT = 30 # seconds - significantly increased for large file transfers with USB CDC

def get_file_paths(base_name):
    """Generate full file paths from a base name in the result folder."""
    result_dir = os.path.join(os.path.dirname(__file__), '../..', 'data', 'result')
    result_dir = os.path.abspath(result_dir)
    return {
        "categorizer": os.path.join(result_dir, f"{base_name}_ctg.csv"),
        "params": os.path.join(result_dir, f"{base_name}_dp.csv"),
        "dataset": os.path.join(result_dir, f"{base_name}_nml.bin")
    }

def wait_for_response(ser, expected_response, timeout=ACK_TIMEOUT, verbose=True):
    """Wait for a specific response from the ESP32."""
    start_time = time.time()
    buffer = b""
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            new_data = ser.read(ser.in_waiting)
            buffer += new_data
            
            # Check if we have the expected response
            if expected_response in buffer:
                if verbose:
                    print(f"✅ Got response: {expected_response.decode()}")
                return True
                
            # Check for error response
            if b"ERROR" in buffer:
                if verbose:
                    print(f"❌ ESP32 reported ERROR: {buffer.decode(errors='ignore')}")
                return False
                
            # Clear buffer if it gets too large to prevent memory issues
            if len(buffer) > 1024:
                buffer = buffer[-512:]  # Keep only the last 512 bytes
                
        time.sleep(0.005)  # Smaller delay for better responsiveness with USB CDC
        
    if verbose:
        print(f"❌ Timeout waiting for '{expected_response.decode()}'. Got: {buffer.decode(errors='ignore')}")
    return False

def send_command(ser, command, payload=b""):
    """Send a command packet to the ESP32."""
    packet = CMD_HEADER + struct.pack('B', command) + payload
    ser.write(packet)
    ser.flush()
    time.sleep(0.01)  # Small delay to ensure command is processed

def transfer_file(ser, file_path, esp32_filename):
    """Handles the transfer of a single file with a progress bar."""
    if not os.path.exists(file_path):
        print(f"⚠️  Warning: File not found, skipping: {file_path}")
        return False

    file_size = os.path.getsize(file_path)
    # Handle case for empty files
    if file_size == 0:
        print(f"\n🚀 Skipping empty file: {Path(file_path).name}")
        # We still need to inform the ESP32 to create the empty file
        filename_bytes = esp32_filename.encode('utf-8')
        payload = struct.pack('B', len(filename_bytes)) + filename_bytes + struct.pack('<I', 0)
        send_command(ser, CMD_FILE_INFO, payload)
        if not wait_for_response(ser, RESP_ACK):
            print("❌ ESP32 did not acknowledge empty file info.")
            return False
        return True
        
    print(f"\n🚀 Transferring {Path(file_path).name} -> {esp32_filename} ({file_size} bytes)")

    # 1. Send File Info
    filename_bytes = esp32_filename.encode('utf-8')
    payload = struct.pack('B', len(filename_bytes)) + filename_bytes + struct.pack('<I', file_size)
    send_command(ser, CMD_FILE_INFO, payload)

    if not wait_for_response(ser, RESP_ACK):
        print("❌ ESP32 did not acknowledge file info.")
        return False

    # 2. Send File Chunks
    with open(file_path, 'rb') as f:
        bytes_sent = 0
        retry_count = 0
        max_retries = 5  # Increased retries for USB CDC
        consecutive_failures = 0  # Track consecutive failures
        
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break  # End of file

            send_command(ser, CMD_FILE_CHUNK, chunk)
            
            # Longer delay for USB CDC stability
            time.sleep(CHUNK_DELAY/1000)
            
            # Wait for ACK with retry mechanism
            if not wait_for_response(ser, RESP_ACK, timeout=ACK_TIMEOUT, verbose=False):
                retry_count += 1
                consecutive_failures += 1
                if retry_count > max_retries:
                    sys.stdout.write('\n') # Newline after progress bar
                    print(f"❌ Failed to get ACK for chunk after {max_retries} retries.")
                    return False
                else:
                    # Seek back and retry the chunk
                    f.seek(f.tell() - len(chunk))
                    sys.stdout.write('\n') # Newline after progress bar
                    print(f"⚠️  Retrying chunk {retry_count}/{max_retries}...")
                    
                    # Progressive backoff for consecutive failures
                    backoff_delay = min(5.0, 0.5 * consecutive_failures)
                    time.sleep(backoff_delay)
                    continue
            else:
                retry_count = 0  # Reset retry count on success
                consecutive_failures = 0  # Reset consecutive failure count
            
            bytes_sent += len(chunk)
            
            # Draw progress bar
            progress = (bytes_sent / file_size) * 100
            bar_length = 40
            filled_len = int(bar_length * bytes_sent // file_size)
            bar = '█' * filled_len + '-' * (bar_length - filled_len)
            
            # Use carriage return to stay on the same line
            sys.stdout.write(f'   [{bar}] {progress:.1f}% complete\r')
            sys.stdout.flush()

    sys.stdout.write('\n') # Newline after progress bar is complete
    print(f"✅ Finished sending file: {Path(file_path).name}")
    return True


def check_and_report_files(files_to_check):
    """Checks for file existence and prints a report."""
    print("\n🔍 Searching for required files...")
    for file_type, file_path in files_to_check.items():
        if os.path.exists(file_path):
            print(f"  [✅] Found {file_type}: {file_path}")
        else:
            print(f"  [⚠️] Missing {file_type}: {file_path} (will be skipped)")
    print("-" * 40)


def main():
    """Main execution function."""
    if len(sys.argv) != 3:
        print(f"Usage: python3 {sys.argv[0]} <base_name> <serial_port>")
        print("Example: python3 unified_transfer.py digit_data /dev/ttyUSB0")
        sys.exit(1)

    base_name = sys.argv[1]
    port = sys.argv[2]
    baudrate = 115200

    files_to_send = get_file_paths(base_name)

    # Check for files before attempting to connect
    check_and_report_files(files_to_send)

    try:
        print(f"🔌 Connecting to ESP32 on {port} at {baudrate}bps...")
        with serial.Serial(port, baudrate, timeout=SERIAL_TIMEOUT) as ser:
            # Clear any pending data and wait for stable connection
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            time.sleep(3) # Extended wait for USB CDC connection to establish
            
            # Test connection stability
            print("🔄 Testing connection stability...")
            time.sleep(2)  # Additional time for USB CDC

            # 1. Start Session
            print("🤝 Initiating transfer session...")
            # Payload: <basename_len (1B)> <basename (str)>
            basename_bytes = base_name.encode('utf-8')
            payload = struct.pack('B', len(basename_bytes)) + basename_bytes
            send_command(ser, CMD_START_SESSION, payload)

            if not wait_for_response(ser, RESP_READY):
                print("❌ ESP32 is not ready. Is the correct sketch running?")
                return

            # 2. Transfer each file
            transfer_file(ser, files_to_send["categorizer"], f"/{base_name}_ctg.csv")
            transfer_file(ser, files_to_send["params"], f"/{base_name}_dp.csv")
            transfer_file(ser, files_to_send["dataset"], f"/{base_name}_nml.bin")

            # 3. End Session
            print("\n🏁 Ending transfer session.")
            send_command(ser, CMD_END_SESSION)
            if not wait_for_response(ser, RESP_OK):
                print("❌ Session did not close cleanly.")
            else:
                print("🎉 Unified transfer complete! ✨")

    except serial.SerialException as e:
        print(f"🔥 Serial Error: {e}")
        print("   Please check the port name and ensure no other program (like Serial Monitor) is using it.")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
