#!/usr/bin/env python3
"""
ESP32 Binary Dataset Transfer Tool

This script transfers binary dataset files to ESP32 via Serial communication.
The ESP32 must be running the corresponding receiver sketch.

Usage: python3 transfer_to_esp32.py <binary_file> <serial_port> <esp32_path>

Example: python3 transfer_to_esp32.py walker_fall_standard.bin /dev/ttyUSB0 /walker_data.bin
"""

import serial
import sys
import time
import os
import struct
from pathlib import Path

# Protocol constants
HEADER_MAGIC = b'ESP32BIN'  # 8-byte magic header
CMD_START_TRANSFER = 0x01
CMD_DATA_CHUNK = 0x02
CMD_END_TRANSFER = 0x03
CMD_ACK = 0xAA
CMD_NACK = 0x55

CHUNK_SIZE = 512  # Bytes per chunk (must match ESP32 sketch)
TRANSFER_TIMEOUT = 30  # seconds
ACK_TIMEOUT = 5  # seconds for each ACK

class ESP32Transferer:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        
    def connect(self):
        """Connect to ESP32"""
        try:
            print(f"🔌 Connecting to ESP32 on {self.port} at {self.baudrate} baud...")
            self.ser = serial.Serial(self.port, self.baudrate, timeout=ACK_TIMEOUT)
            time.sleep(2)  # Give ESP32 time to initialize
            
            # Clear any existing data
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            
            print("✅ Connected to ESP32")
            return True
            
        except serial.SerialException as e:
            print(f"❌ Failed to connect: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from ESP32"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("🔌 Disconnected from ESP32")
    
    def wait_for_ack(self, timeout=ACK_TIMEOUT):
        """Wait for ACK from ESP32"""
        start_time = time.time()
        ack_found = False
        
        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                # Read all available data
                data = self.ser.read(self.ser.in_waiting)
                
                # Look for ACK or NACK in the received data
                for byte in data:
                    if byte == CMD_ACK:
                        ack_found = True
                        break
                    elif byte == CMD_NACK:
                        print(f"🚫 Received NACK (0x{byte:02X})")
                        return False
                
                if ack_found:
                    return True
                    
            time.sleep(0.01)  # Small delay to prevent busy waiting
            
        print(f"⏰ Timeout waiting for ACK ({timeout}s)")
        return False
    
    def send_start_command(self, filename, file_size):
        """Send transfer start command"""
        print(f"📤 Sending start command...")
        print(f"   📁 File: {filename}")
        print(f"   📊 Size: {file_size} bytes")
        
        # Prepare filename (max 64 bytes, null-terminated)
        filename_bytes = filename.encode('utf-8')[:63]
        filename_padded = filename_bytes + b'\x00' * (64 - len(filename_bytes))
        
        # Send: magic + command + filename + file_size
        packet = HEADER_MAGIC + struct.pack('B', CMD_START_TRANSFER) + filename_padded + struct.pack('<I', file_size)
        
        self.ser.write(packet)
        self.ser.flush()
        
        # Wait for ACK
        if self.wait_for_ack():
            print("✅ ESP32 ready to receive")
            return True
        else:
            print("❌ ESP32 not ready or timeout")
            return False
    
    def send_data_chunk(self, chunk_data, chunk_index):
        """Send a data chunk"""
        # Send: magic + command + chunk_index + chunk_size + data
        packet = HEADER_MAGIC + struct.pack('B', CMD_DATA_CHUNK) + struct.pack('<H', chunk_index) + struct.pack('<H', len(chunk_data)) + chunk_data
        
        self.ser.write(packet)
        self.ser.flush()
        
        # Wait for ACK
        return self.wait_for_ack()
    
    def send_end_command(self):
        """Send transfer end command"""
        print("📤 Sending end command...")
        
        packet = HEADER_MAGIC + struct.pack('B', CMD_END_TRANSFER)
        self.ser.write(packet)
        self.ser.flush()
        
        # Wait for final ACK
        if self.wait_for_ack(timeout=10):  # Longer timeout for file writing
            print("✅ Transfer completed successfully")
            return True
        else:
            print("❌ Transfer end failed or timeout")
            return False
    
    def transfer_file(self, binary_file, esp32_path):
        """Transfer binary file to ESP32"""
        try:
            # Check if file exists
            if not os.path.exists(binary_file):
                print(f"❌ File not found: {binary_file}")
                return False
            
            file_size = os.path.getsize(binary_file)
            filename = esp32_path  # Use the ESP32 path as filename
            
            print(f"\n=== ESP32 Binary Transfer ===")
            print(f"📁 Local file: {binary_file}")
            print(f"🎯 ESP32 path: {esp32_path}")
            print(f"📊 File size: {file_size} bytes")
            print(f"📦 Chunks: {(file_size + CHUNK_SIZE - 1) // CHUNK_SIZE}")
            print()
            
            # Send start command
            if not self.send_start_command(filename, file_size):
                return False
            
            # Send file data in chunks
            bytes_sent = 0
            chunk_index = 0
            
            with open(binary_file, 'rb') as f:
                while True:
                    chunk = f.read(CHUNK_SIZE)
                    if not chunk:
                        break
                    
                    print(f"📤 Chunk {chunk_index + 1}: {len(chunk)} bytes ({bytes_sent + len(chunk)}/{file_size})")
                    
                    if not self.send_data_chunk(chunk, chunk_index):
                        print(f"❌ Failed to send chunk {chunk_index}")
                        return False
                    
                    bytes_sent += len(chunk)
                    chunk_index += 1
                    
                    # Progress indicator
                    progress = (bytes_sent * 100) // file_size
                    print(f"   ✅ ACK received - Progress: {progress}%")
            
            # Send end command
            return self.send_end_command()
            
        except Exception as e:
            print(f"❌ Transfer error: {e}")
            return False

def print_usage():
    print("Usage: python3 transfer_to_esp32.py <binary_file> <serial_port> <esp32_path>")
    print()
    print("Arguments:")
    print("  binary_file  : Path to the binary dataset file")
    print("  serial_port  : ESP32 serial port (e.g., /dev/ttyUSB0, COM3)")
    print("  esp32_path   : Target path on ESP32 filesystem (e.g., /walker_data.bin)")
    print()
    print("Examples:")
    print("  python3 transfer_to_esp32.py walker_fall_standard.bin /dev/ttyUSB0 /walker_data.bin")
    print("  python3 transfer_to_esp32.py dataset.bin COM3 /dataset.bin")
    print()
    print("Notes:")
    print("  - ESP32 must be running the data_receiver.ino sketch")
    print("  - Close Arduino IDE Serial Monitor before running this script")
    print("  - Transfer speed depends on baud rate (default: 115200)")

def main():
    if len(sys.argv) != 4:
        print_usage()
        return 1
    
    binary_file = sys.argv[1]
    serial_port = sys.argv[2]
    esp32_path = sys.argv[3]
    
    # Validate inputs
    if not os.path.exists(binary_file):
        print(f"❌ Binary file not found: {binary_file}")
        return 1
    
    if not esp32_path.startswith('/'):
        print(f"❌ ESP32 path must start with '/': {esp32_path}")
        return 1
    
    # Create transferer and connect
    transferer = ESP32Transferer(serial_port)
    
    if not transferer.connect():
        return 1
    
    try:
        # Transfer the file
        success = transferer.transfer_file(binary_file, esp32_path)
        
        if success:
            print(f"\n🎉 Transfer successful!")
            print(f"📁 File saved on ESP32: {esp32_path}")
            print(f"🔄 ESP32 can now load the dataset using the path: {esp32_path}")
            return 0
        else:
            print(f"\n❌ Transfer failed!")
            return 1
            
    finally:
        transferer.disconnect()

if __name__ == "__main__":
    sys.exit(main())
