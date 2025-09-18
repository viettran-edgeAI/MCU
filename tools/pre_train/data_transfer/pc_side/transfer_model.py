#!/usr/bin/env python3
"""
Pre-trained Model Transfer Utility for ESP32

This script transfers pre-trained random forest model files from PC to ESP32.
Transfers <model_name>_config.json and all <model_name>_tree_*.bin files while preserving filenames.

Usage:
  python3 transfer_model.py <model_name> <serial_port>

Example:
  python3 transfer_model.py my_model /dev/ttyUSB0
"""

import serial
import time
import os
import sys
import struct
import glob
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

# Transfer parameters optimized for ESP32
CHUNK_SIZE =  256  # bytes per chunk
CHUNK_DELAY = 50  # delay between chunks in ms

# Timeout settings
SERIAL_TIMEOUT = 30  # seconds
ACK_TIMEOUT = 30    # seconds

def get_model_files(model_name):
    """Get all model files from the trained_model directory for given model_name."""
    script_dir = os.path.dirname(__file__)
    model_dir = os.path.join(script_dir, '../..', 'trained_model')
    model_dir = os.path.abspath(model_dir)
    
    config_files = []
    forest_files = []
    predictor_files = []
    log_files = []
    
    # Add config file (model-specific JSON)
    config_file = os.path.join(model_dir, f"{model_name}_config.json")
    if os.path.exists(config_file):
        config_files.append(config_file)
    
    # Add unified forest file (replaces individual tree files)
    forest_file = os.path.join(model_dir, f"{model_name}_forest.bin")
    if os.path.exists(forest_file):
        forest_files.append(forest_file)
    
    # Add node predictor model file
    predictor_file = os.path.join(model_dir, f"{model_name}_node_pred.bin")
    if os.path.exists(predictor_file):
        predictor_files.append(predictor_file)
    else:
        # Try alternative locations for node predictor (relative to script)
        alt_predictor_path = os.path.join(script_dir, '..', f"{model_name}_node_pred.bin")
        if os.path.exists(alt_predictor_path):
            predictor_files.append(os.path.abspath(alt_predictor_path))
    
    # Add tree log CSV file (model-specific, now in trained_model folder)
    tree_log_file = os.path.join(model_dir, f"{model_name}_node_log.csv")
    if os.path.exists(tree_log_file):
        log_files.append(tree_log_file)
    
    return config_files, forest_files, predictor_files, log_files

def get_all_model_files(model_name):
    """Get all model files as a single list (for compatibility)."""
    config_files, forest_files, predictor_files, log_files = get_model_files(model_name)
    return config_files + predictor_files + log_files + forest_files

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
                
            # Clear buffer if it gets too large
            if len(buffer) > 1024:
                buffer = buffer[-512:]
                
        time.sleep(0.005)
        
    if verbose:
        print(f"❌ Timeout waiting for '{expected_response.decode()}'. Got: {buffer.decode(errors='ignore')}")
    return False

def send_command(ser, command, payload=b""):
    """Send a command packet to the ESP32."""
    packet = CMD_HEADER + struct.pack('B', command) + payload
    ser.write(packet)
    ser.flush()
    time.sleep(0.01)

def transfer_file(ser, file_path, show_progress=True, progress_callback=None, quiet_acks=False):
    """Transfer a single model file to ESP32."""
    if not os.path.exists(file_path):
        print(f"⚠️  Warning: File not found, skipping: {file_path}")
        return False

    file_size = os.path.getsize(file_path)
    filename = os.path.basename(file_path)
    
    if show_progress:
        print(f"\n🚀 Transferring {filename} ({file_size} bytes)")

    # 1. Send File Info
    filename_bytes = filename.encode('utf-8')
    payload = struct.pack('B', len(filename_bytes)) + filename_bytes + struct.pack('<I', file_size)
    send_command(ser, CMD_FILE_INFO, payload)

    if not wait_for_response(ser, RESP_ACK, verbose=not quiet_acks):
        if show_progress:
            print("❌ ESP32 did not acknowledge file info.")
        return False

    # 2. Send File Chunks
    with open(file_path, 'rb') as f:
        bytes_sent = 0
        retry_count = 0
        max_retries = 5
        consecutive_failures = 0
        
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break

            send_command(ser, CMD_FILE_CHUNK, chunk)
            time.sleep(CHUNK_DELAY/1000)
            
            # Wait for ACK with retry mechanism
            if not wait_for_response(ser, RESP_ACK, timeout=ACK_TIMEOUT, verbose=False):
                retry_count += 1
                consecutive_failures += 1
                if retry_count > max_retries:
                    if show_progress:
                        sys.stdout.write('\n')
                        print(f"❌ Failed to get ACK for chunk after {max_retries} retries.")
                    return False
                else:
                    # Seek back and retry the chunk
                    f.seek(f.tell() - len(chunk))
                    if show_progress:
                        sys.stdout.write('\n')
                        print(f"⚠️  Retrying chunk {retry_count}/{max_retries}...")
                    
                    backoff_delay = min(5.0, 0.5 * consecutive_failures)
                    time.sleep(backoff_delay)
                    continue
            else:
                retry_count = 0
                consecutive_failures = 0
            
            bytes_sent += len(chunk)
            
            # Call progress callback if provided
            if progress_callback:
                progress_callback(bytes_sent, file_size)
            elif show_progress:
                # Individual file progress bar
                progress = (bytes_sent / file_size) * 100
                bar_length = 40
                filled_len = int(bar_length * bytes_sent // file_size)
                bar = '█' * filled_len + '-' * (bar_length - filled_len)
                
                sys.stdout.write(f'   [{bar}] {progress:.1f}% complete\r')
                sys.stdout.flush()

    if show_progress:
        sys.stdout.write('\n')
        print(f"✅ Finished transferring {filename}")
    return True

def transfer_multiple_files(ser, files, title):
    """Transfer multiple files with a single combined progress bar."""
    total_size = sum(os.path.getsize(f) for f in files if os.path.exists(f))
    total_bytes_sent = 0
    
    print(f"\n🚀 {title}")
    print(f"📦 Transferring {len(files)} files ({total_size} bytes total)")
    
    success_count = 0
    
    for i, file_path in enumerate(files):
        filename = os.path.basename(file_path)
        file_size = os.path.getsize(file_path) if os.path.exists(file_path) else 0
        
        def progress_callback(bytes_sent, file_total):
            current_file_progress = total_bytes_sent + bytes_sent
            overall_progress = (current_file_progress / total_size) * 100
            bar_length = 50
            filled_len = int(bar_length * current_file_progress // total_size)
            bar = '█' * filled_len + '-' * (bar_length - filled_len)
            
            # Update the same line with current file being transferred
            sys.stdout.write(f'   [{bar}] {overall_progress:.1f}% - {filename}\r')
            sys.stdout.flush()
        
        if transfer_file(ser, file_path, show_progress=False, progress_callback=progress_callback, quiet_acks=True):
            total_bytes_sent += file_size
            success_count += 1
            
            # Final update for this file showing completion
            overall_progress = (total_bytes_sent / total_size) * 100
            bar_length = 50
            filled_len = int(bar_length * total_bytes_sent // total_size)
            bar = '█' * filled_len + '-' * (bar_length - filled_len)
            
            # Clear the line and show completion
            sys.stdout.write(f'   [{bar}] {overall_progress:.1f}% - ✅ {filename}\r')
            sys.stdout.flush()
            
            # Only move to next line at the very end or if this is the last file
            if i == len(files) - 1:
                sys.stdout.write('\n')
        else:
            # Move to new line for error message
            sys.stdout.write(f'\n   ❌ Failed: {filename}\n')
    
    print(f"✅ Completed {title}: {success_count}/{len(files)} files transferred")
    return success_count

def check_model_files(model_name):
    """Check for model files and print a report for the given model_name."""
    print("\n🔍 Searching for model files...")
    config_files, forest_files, predictor_files, log_files = get_model_files(model_name)
    all_files = config_files + predictor_files + log_files + forest_files
    
    if not all_files:
        print("❌ No model files found!")
        return False
    
    print(f"📁 Found {len(all_files)} model files:")
    
    if config_files:
        print("   📋 Configuration files:")
        for file_path in config_files:
            size = os.path.getsize(file_path)
            print(f"      • {os.path.basename(file_path)} ({size} bytes)")
    
    if predictor_files:
        print("   🧮 Node Predictor files:")
        for file_path in predictor_files:
            size = os.path.getsize(file_path)
            print(f"      • {os.path.basename(file_path)} ({size} bytes)")
    
    if log_files:
        print("   📊 Training Log files:")
        for file_path in log_files:
            size = os.path.getsize(file_path)
            print(f"      • {os.path.basename(file_path)} ({size} bytes)")
    
    if forest_files:
        print("   🌳 Unified Forest files:")
        forest_total_size = sum(os.path.getsize(f) for f in forest_files)
        for file_path in forest_files:
            size = os.path.getsize(file_path)
            print(f"      • {os.path.basename(file_path)} ({size} bytes) - Contains all decision trees")
    
    total_size = sum(os.path.getsize(f) for f in all_files)
    print(f"📊 Total size: {total_size} bytes ({total_size/1024:.1f} KB)")
    return True

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 transfer_model.py <model_name> <serial_port>")
        print("Example: python3 transfer_model.py my_model /dev/ttyUSB0")
        return 1

    model_name = sys.argv[1]
    serial_port = sys.argv[2]
    
    # Check for model files
    if not check_model_files(model_name):
        return 1
    
    config_files, forest_files, predictor_files, log_files = get_model_files(model_name)
    total_files = len(config_files) + len(predictor_files) + len(log_files) + len(forest_files)
    
    print(f"\n🔌 Connecting to ESP32 on {serial_port}...")
    try:
        ser = serial.Serial(serial_port, 115200, timeout=SERIAL_TIMEOUT)
        time.sleep(2)  # Allow ESP32 to reset
        print("✅ Connected!")
    except Exception as e:
        print(f"❌ Failed to connect: {e}")
        return 1

    try:
        # Start transfer session
        print("\n🚀 Starting model transfer session...")
        session_name = model_name + "_transfer"
        session_bytes = session_name.encode('utf-8')
        payload = struct.pack('B', len(session_bytes)) + session_bytes
        send_command(ser, CMD_START_SESSION, payload)

        if not wait_for_response(ser, RESP_READY):
            print("❌ ESP32 did not respond with READY.")
            return 1

        print("✅ ESP32 is ready to receive model files.")

        # Transfer files with combined progress bars
        total_success = 0
        
        # Transfer config files individually (usually just one)
        if config_files:
            for file_path in config_files:
                if transfer_file(ser, file_path):
                    total_success += 1
                else:
                    print(f"❌ Failed to transfer {os.path.basename(file_path)}")
        
        # Transfer node predictor files individually
        if predictor_files:
            for file_path in predictor_files:
                filename = os.path.basename(file_path)
                if transfer_file(ser, file_path):
                    total_success += 1
                    print(f"✅ Node predictor transferred successfully!")
                else:
                    print(f"❌ Failed to transfer {filename}")
                    print("⚠️  Note: ESP32 will use default memory estimation without node predictor")
        
        # Transfer log files individually
        if log_files:
            for file_path in log_files:
                filename = os.path.basename(file_path)
                if transfer_file(ser, file_path):
                    total_success += 1
                    print(f"✅ Training log transferred successfully!")
                else:
                    print(f"❌ Failed to transfer {filename}")
                    print("⚠️  Note: ESP32 will not have training history without log file")
        
        # Transfer unified forest file
        if forest_files:
            for file_path in forest_files:
                filename = os.path.basename(file_path)
                if transfer_file(ser, file_path):
                    total_success += 1
                    print(f"✅ Unified forest file transferred successfully!")
                else:
                    print(f"❌ Failed to transfer {filename}")
                    print("⚠️  Critical: ESP32 cannot function without the forest file")

        # End session
        print("\n🏁 Ending transfer session...")
        send_command(ser, CMD_END_SESSION)
        
        if wait_for_response(ser, RESP_OK):
            print("✅ Transfer session completed successfully!")
            print(f"📈 Successfully transferred {total_success}/{total_files} files.")
            
            if total_success == total_files:
                print("🎉 All model files transferred successfully!")
                return 0
            else:
                print("⚠️  Some files failed to transfer.")
                return 1
        else:
            print("❌ ESP32 did not confirm session end.")
            return 1

    except KeyboardInterrupt:
        print("\n\n⚠️  Transfer interrupted by user.")
        return 1
    except Exception as e:
        print(f"\n❌ Transfer failed: {e}")
        return 1
    finally:
        ser.close()

if __name__ == "__main__":
    exit(main())
