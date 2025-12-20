#!/usr/bin/env python3
"""
Unified File Transfer Utility for ESP32

This script transfers all necessary model files from PC to ESP32 in a single session.
Transfers files to /model_name/ directory on ESP32 filesystem.

Files transferred:
1. From data_quantization/data/result/:
    - {model_name}_qtz.bin (quantizer binary)
    - {model_name}_dp.csv (descriptor payload)
    - {model_name}_nml.bin (normalized dataset for training/testing)
   
2. From hog_transform/result/:
   - {model_name}_hogcfg.json (HOG configuration, optional)
   
3. From pre_train/trained_model/:
   - {model_name}_config.json (model configuration)
   - {model_name}_forest.bin (unified forest file)
   - {model_name}_npd.bin (node predictor model)
   - {model_name}_nlg.csv (training log)

Usage:
  python3 unifier_transfer.py --model_name <model_name> --port <serial_port>

Example:
  python3 unifier_transfer.py --model_name digit_data --port /dev/ttyUSB0
  python3 unifier_transfer.py -m digit_data -p /dev/ttyUSB0
"""

import serial
import time
import os
import sys
import struct
import binascii
from pathlib import Path
import argparse

# Import configuration parser to sync with ESP32
try:
    from config_parser import get_user_chunk_size
except ImportError:
    print("⚠️  Warning: config_parser.py not found. Using default CHUNK_SIZE.")
    def get_user_chunk_size(config_file_path=None, default=220):
        return default

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

# CHUNK_SIZE is automatically extracted from Rf_board_config.h
CHUNK_SIZE = get_user_chunk_size(default=220)
CHUNK_DELAY = 0.02  # delay between chunks in seconds
MAX_RETRIES = 5     # max retries per chunk

# Timeout settings
SERIAL_TIMEOUT = 5  # seconds
ACK_TIMEOUT = 5    # seconds


def get_script_root():
    """Get the tools directory root."""
    script_dir = Path(__file__).resolve().parent
    # pc_side -> data_transfer -> tools
    return script_dir.parent.parent


def find_dataset_files(model_name):
    """Find dataset files from data_quantization/data/result/."""
    tools_root = get_script_root()
    result_dir = tools_root / "data_quantization" / "data" / "result"

    suffixes = ["_qtz.bin", "_dp.csv", "_nml.bin"]
    files = []

    for suffix in suffixes:
        file_path = result_dir / f"{model_name}{suffix}"
        if file_path.exists():
            files.append(str(file_path))
    return files


def find_hog_config_file(model_name):
    """Find HOG config file from hog_transform/result/ (optional)."""
    tools_root = get_script_root()
    result_dir = tools_root / "hog_transform" / "result"
    
    filename = f"{model_name}_hogcfg.json"
    file_path = result_dir / filename
    
    if file_path.exists():
        return str(file_path)
    
    # Fallback to hog_transform root directory
    fallback_path = tools_root / "hog_transform" / filename
    if fallback_path.exists():
        return str(fallback_path)
    
    return None


def find_model_files(model_name):
    """Find all model files from pre_train/trained_model/."""
    tools_root = get_script_root()
    model_dir = tools_root / "pre_train" / "trained_model"
    
    files = []
    
    # Config file (required)
    config_file = model_dir / f"{model_name}_config.json"
    if config_file.exists():
        files.append(str(config_file))
    
    # Forest file (required)
    forest_file = model_dir / f"{model_name}_forest.bin"
    if forest_file.exists():
        files.append(str(forest_file))
    
    # Node predictor file (optional)
    npd_file = model_dir / f"{model_name}_npd.bin"
    if npd_file.exists():
        files.append(str(npd_file))
    
    # Training log file (optional)
    log_file = model_dir / f"{model_name}_nlg.csv"
    if log_file.exists():
        files.append(str(log_file))
    
    return files


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


def wait_for_response(ser, expected_response, timeout=ACK_TIMEOUT, verbose=True):
    """Wait for a specific response from the ESP32."""
    start_time = time.time()
    buffer = b""
    all_received = []
    
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            new_data = ser.read(ser.in_waiting)
            buffer += new_data
            all_received.append(new_data)
            
            if expected_response in buffer:
                return True
                
            if b"ERROR" in buffer:
                if verbose:
                    print(f"❌ ESP32 reported ERROR: {buffer.decode(errors='ignore')}")
                return False
                
            if len(buffer) > 1024:
                buffer = buffer[-512:]
                
        time.sleep(0.005)
        
    if verbose:
        all_data = b"".join(all_received).decode(errors='ignore')
        if all_data:
            print(f"❌ Timeout waiting for '{expected_response.decode()}'.")
            print(f"📥 Received from ESP32:\n{all_data}")
        else:
            print(f"❌ Timeout waiting for '{expected_response.decode()}'. No response from ESP32.")
    return False


def send_command(ser, command, payload=b""):
    """Send a command packet to the ESP32."""
    packet = CMD_HEADER + struct.pack('B', command) + payload
    ser.write(packet)
    ser.flush()
    time.sleep(0.01)


def transfer_file(ser, file_path, show_progress=True, progress_callback=None, quiet_acks=False):
    """Transfer a single file to ESP32 with CRC verification."""
    if not os.path.exists(file_path):
        print(f"⚠️  Warning: File not found, skipping: {file_path}")
        return False

    file_size = os.path.getsize(file_path)
    filename = os.path.basename(file_path)
    
    if show_progress:
        print(f"\n🚀 Transferring {filename} ({file_size} bytes)")

    # Read the file data
    with open(file_path, 'rb') as f:
        file_data = f.read()
    
    # Compute full file CRC32
    file_crc = binascii.crc32(file_data) & 0xFFFFFFFF

    # Send File Info with metadata
    filename_bytes = filename.encode('utf-8')
    payload = (struct.pack('B', len(filename_bytes)) + filename_bytes + 
               struct.pack('<III', file_size, file_crc, CHUNK_SIZE))
    send_command(ser, CMD_FILE_INFO, payload)

    if not wait_for_response(ser, RESP_ACK, verbose=not quiet_acks):
        if show_progress:
            print("❌ ESP32 did not acknowledge file info.")
        return False

    # Send File Chunks with per-chunk CRC and ACK/NACK
    bytes_sent = 0

    for offset in range(0, file_size, CHUNK_SIZE):
        chunk = file_data[offset:offset + CHUNK_SIZE]
        chunk_len = len(chunk)
        chunk_crc = binascii.crc32(chunk) & 0xFFFFFFFF
        header = struct.pack('<III', offset, chunk_len, chunk_crc)

        success = False
        for attempt in range(1, MAX_RETRIES + 1):
            # Send chunk with header
            send_command(ser, CMD_FILE_CHUNK, header + chunk)

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
                if show_progress and line:
                    sys.stdout.write('\n')
                    print(f"   ↩ {line}")
                if attempt < MAX_RETRIES and show_progress:
                    print(f"⚠️  Retrying chunk at offset {offset} ({attempt}/{MAX_RETRIES})...")
                continue
            time.sleep(0.05)

        if not success:
            if show_progress:
                sys.stdout.write('\n')
                print(f"❌ Chunk transfer failed at offset {offset} after {MAX_RETRIES} retries")
            return False

        bytes_sent += chunk_len
        
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
        
        time.sleep(CHUNK_DELAY)

    if show_progress:
        sys.stdout.write('\n')
        print(f"✅ Finished transferring {filename}")
    return True


def transfer_multiple_files(ser, files, title):
    """Transfer multiple files with a combined progress bar."""
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
            
            sys.stdout.write(f'   [{bar}] {overall_progress:.1f}% - {filename}\r')
            sys.stdout.flush()
        
        if transfer_file(ser, file_path, show_progress=False, progress_callback=progress_callback, quiet_acks=True):
            total_bytes_sent += file_size
            success_count += 1
            
            overall_progress = (total_bytes_sent / total_size) * 100
            bar_length = 50
            filled_len = int(bar_length * total_bytes_sent // total_size)
            bar = '█' * filled_len + '-' * (bar_length - filled_len)
            
            sys.stdout.write(f'   [{bar}] {overall_progress:.1f}% - ✅ {filename}\r')
            sys.stdout.flush()
            
            if i == len(files) - 1:
                sys.stdout.write('\n')
        else:
            sys.stdout.write(f'\n   ❌ Failed: {filename}\n')
    
    print(f"✅ Completed {title}: {success_count}/{len(files)} files transferred")
    return success_count


def collect_all_files(model_name):
    """Collect all files to transfer for the given model."""
    files_by_category = {
        'dataset': [],
        'hog_config': [],
        'model': []
    }
    
    # 1. Dataset files
    dataset_files = find_dataset_files(model_name)
    files_by_category['dataset'].extend(dataset_files)
    
    # 2. HOG config file (optional)
    hog_config = find_hog_config_file(model_name)
    if hog_config:
        files_by_category['hog_config'].append(hog_config)
    
    # 3. Model files
    model_files = find_model_files(model_name)
    files_by_category['model'].extend(model_files)
    
    return files_by_category


def print_file_summary(files_by_category):
    """Print a summary of files to be transferred."""
    print("\n🔍 Searching for model files...")
    
    total_files = 0
    total_size = 0
    
    # Dataset files
    if files_by_category['dataset']:
        print("\n📊 Dataset files:")
        for file_path in files_by_category['dataset']:
            size = os.path.getsize(file_path)
            total_size += size
            total_files += 1
            filename = os.path.basename(file_path)
            if filename.endswith('_qtz.bin'):
                label = 'Quantizer binary'
            elif filename.endswith('_dp.csv'):
                label = 'Descriptor payload'
            elif filename.endswith('_nml.bin'):
                label = 'Normalized dataset'
            else:
                label = 'Dataset file'
            print(f"   • {filename} ({size} bytes) - {label}")
    else:
        print("\n⚠️  No dataset files found (optional)")
    
    # HOG config files
    if files_by_category['hog_config']:
        print("\n🖼️  HOG Configuration files:")
        for file_path in files_by_category['hog_config']:
            size = os.path.getsize(file_path)
            total_size += size
            total_files += 1
            print(f"   • {os.path.basename(file_path)} ({size} bytes)")
    else:
        print("\n⚠️  No HOG config file found (optional)")
    
    # Model files
    if files_by_category['model']:
        print("\n🌳 Model files:")
        for file_path in files_by_category['model']:
            size = os.path.getsize(file_path)
            total_size += size
            total_files += 1
            filename = os.path.basename(file_path)
            if filename.endswith('_config.json'):
                print(f"   📋 {filename} ({size} bytes) - Configuration")
            elif filename.endswith('_forest.bin'):
                print(f"   🌲 {filename} ({size} bytes) - Unified Forest")
            elif filename.endswith('_npd.bin'):
                print(f"   🧮 {filename} ({size} bytes) - Node Predictor")
            elif filename.endswith('_nlg.csv'):
                print(f"   📊 {filename} ({size} bytes) - Training Log")
            else:
                print(f"   • {filename} ({size} bytes)")
    else:
        print("\n❌ No model files found (required!)")
    
    if total_files == 0:
        print("\n❌ No files found to transfer!")
        return False
    
    print(f"\n📦 Total: {total_files} files, {total_size} bytes ({total_size/1024:.1f} KB)")
    return True


def main():
    parser = argparse.ArgumentParser(description="Unified File Transfer Utility for ESP32")
    parser.add_argument('--model_name', '-m', required=True, help='Name of the model to transfer')
    parser.add_argument('--port', '-p', required=True, help='Serial port for ESP32')
    args = parser.parse_args()

    model_name = args.model_name
    serial_port = args.port
    
    # Collect all files
    files_by_category = collect_all_files(model_name)
    
    # Print summary
    if not print_file_summary(files_by_category):
        return 1
    
    # Flatten all files
    all_files = (files_by_category['dataset'] + 
                 files_by_category['hog_config'] + 
                 files_by_category['model'])
    
    # Connect to ESP32
    print(f"\n🔌 Connecting to ESP32 on {serial_port}...")
    try:
        ser = serial.Serial(serial_port, 115200, timeout=SERIAL_TIMEOUT)
        time.sleep(2)  # Allow ESP32 to reset
        print("✅ Connected!")
        
        # Clear any pending data
        time.sleep(0.5)
        if ser.in_waiting > 0:
            startup_data = ser.read(ser.in_waiting).decode(errors='ignore')
            print(f"📟 ESP32 startup messages:\n{startup_data}")
    except Exception as e:
        print(f"❌ Failed to connect: {e}")
        return 1

    try:
        # Start transfer session
        print("\n🚀 Starting unified transfer session...")
        session_name = model_name + "_unified"
        session_bytes = session_name.encode('utf-8')
        payload = struct.pack('B', len(session_bytes)) + session_bytes
        
        print(f"📤 Sending START_SESSION command with session name: {session_name}")
        send_command(ser, CMD_START_SESSION, payload)
        
        print("⏳ Waiting for ESP32 READY response...")
        if not wait_for_response(ser, RESP_READY):
            print("❌ ESP32 did not respond with READY.")
            print("\n💡 Troubleshooting tips:")
            print("   1. Make sure unifier_receiver.ino is uploaded to the ESP32")
            print("   2. Press the RESET button on the ESP32 and try again")
            print("   3. Check that the ESP32 is not running a different program")
            print("   4. Open the Arduino Serial Monitor to see what the ESP32 is doing")
            return 1

        print("✅ ESP32 is ready to receive files.")

        # Transfer all files
        total_success = 0
        
        for file_path in all_files:
            if transfer_file(ser, file_path):
                total_success += 1
            else:
                filename = os.path.basename(file_path)
                print(f"❌ Failed to transfer {filename}")

        # End session
        print("\n🏁 Ending transfer session...")
        send_command(ser, CMD_END_SESSION)
        
        if wait_for_response(ser, RESP_OK):
            print("✅ Transfer session completed successfully!")
            print(f"📈 Successfully transferred {total_success}/{len(all_files)} files.")
            
            if total_success == len(all_files):
                print(f"🎉 All files transferred successfully to /{model_name}/ on ESP32!")
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
        import traceback
        traceback.print_exc()
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    exit(main())
