#!/usr/bin/env python3
"""
Simple Dataset Parameters Transfer Script for ESP32
Sends filename first, then CSV content to ESP32 via serial communication
"""

import serial
import time
import csv
import sys
import os
from pathlib import Path

def send_csv_to_esp32(csv_file, port='/dev/ttyUSB0', baudrate=115200):
    """Send filename and CSV file content to ESP32 line by line"""
    print(f"🚀 Simple transfer: {csv_file} -> ESP32 ({port})")
    
    # Check if CSV file exists
    if not os.path.exists(csv_file):
        print(f"❌ File not found: {csv_file}")
        return False
    
    try:
        # Connect to ESP32
        ser = serial.Serial(port, baudrate, timeout=5)
        time.sleep(2)  # Wait for ESP32 to initialize
        print(f"✅ Connected to ESP32")
        
        # Extract just the filename (without path)
        filename = Path(csv_file).name
        print(f"📝 Sending filename: {filename}")
        
        # Send filename first with special marker
        ser.write(f"FILENAME:{filename}\n".encode())
        ser.flush()
        time.sleep(0.5)  # Give ESP32 time to process filename
        
        # Read and send CSV file content
        with open(csv_file, 'r') as file:
            lines = file.readlines()
            
        print(f"📄 Sending {len(lines)} data lines...")
        
        for i, line in enumerate(lines):
            # Send each line
            line = line.strip()
            if line:  # Skip empty lines
                ser.write((line + '\n').encode())
                ser.flush()
                print(f"📤 Sent line {i+1}/{len(lines)}: {line[:50]}...")
                time.sleep(0.1)  # Small delay between lines
        
        # Send end marker
        ser.write("END\n".encode())
        ser.flush()
        print("🏁 Transfer completed")
        
        ser.close()
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 simple_transfer.py <csv_file> [port] [baudrate]")
        print("Example: python3 transfer_dataset_params.py dataset_params.csv /dev/ttyUSB0 115200")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else '/dev/ttyUSB0'
    baudrate = int(sys.argv[3]) if len(sys.argv) > 3 else 115200
    
    success = send_csv_to_esp32(csv_file, port, baudrate)
    
    if success:
        print("✅ SUCCESS: Data sent to ESP32")
    else:
        print("❌ FAILED: Could not send data")
        sys.exit(1)

if __name__ == "__main__":
    main()