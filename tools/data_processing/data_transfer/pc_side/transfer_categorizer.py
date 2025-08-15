#!/usr/bin/env python3
"""
Simple CSV Transfer to ESP32

This script sends a CSV file to ESP32 via Serial port.
Usage: python3 simple_csv_transfer.py <csv_file> <serial_port>
"""

import sys
import time
import serial
import serial.tools.list_ports
import os
import argparse

def send_csv_to_esp32(csv_file, serial_port, baud_rate=115200, output_filename="categorizer.csv"):
    """
    Send CSV file to ESP32 via Serial
    
    Args:
        csv_file: Path to the CSV file
        serial_port: Serial port name
        baud_rate: Serial communication baud rate
        output_filename: Filename to save on ESP32
    """
    
    try:
        print(f"📁 Reading CSV file: {csv_file}")
        
        # Validate CSV file exists
        if not os.path.exists(csv_file):
            print(f"❌ File not found: {csv_file}")
            return False
            
        file_size = os.path.getsize(csv_file)
        print(f"📊 File size: {file_size} bytes")
        
        # Read CSV content
        with open(csv_file, 'r') as f:
            csv_lines = [line.strip() for line in f.readlines() if line.strip()]
        
        print(f"📄 Total lines: {len(csv_lines)}")
        
        # Connect to ESP32
        print(f"\n🔌 Connecting to {serial_port} at {baud_rate} baud...")
        ser = serial.Serial(serial_port, baud_rate, timeout=2)
        time.sleep(3)  # Wait for ESP32 to initialize
        
        # Clear buffers
        ser.flushInput()
        ser.flushOutput()
        
        # Send start signal
        print("📤 Sending start signal...")
        ser.write(b'START_CSV\n')
        ser.flush()
        time.sleep(0.5)
        
        # Wait for ESP32 ready response
        print("⏳ Waiting for ESP32 ready signal...")
        start_time = time.time()
        ready_received = False
        
        while time.time() - start_time < 10:  # 10 second timeout
            if ser.in_waiting:
                response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                print(f"📨 ESP32: {response.strip()}")
                
                if "READY" in response:
                    ready_received = True
                    break
            time.sleep(0.1)
        
        if not ready_received:
            print("❌ ESP32 not ready")
            return False
        
        # Send filename
        print(f"📝 Sending filename: {output_filename}")
        ser.write(f"{output_filename}\n".encode('utf-8'))
        ser.flush()
        time.sleep(0.5)
        
        # Send CSV data
        print(f"📤 Sending CSV data ({len(csv_lines)} lines)...")
        
        for i, line in enumerate(csv_lines):
            ser.write(f"{line}\n".encode('utf-8'))
            
            # Show progress every 50 lines
            if (i + 1) % 50 == 0:
                progress = ((i + 1) / len(csv_lines)) * 100
                print(f"   Progress: {i + 1}/{len(csv_lines)} lines ({progress:.1f}%)")
            
            # Small delay to prevent overwhelming ESP32
            time.sleep(0.01)
        
        # Send end marker
        print("📋 Sending end marker...")
        ser.write(b'EOF_CSV\n')
        ser.flush()
        
        # Wait for completion response
        print("⏳ Waiting for completion response...")
        start_time = time.time()
        
        while time.time() - start_time < 15:  # 15 second timeout
            if ser.in_waiting:
                response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                print(f"📨 ESP32: {response.strip()}")
                
                if "SUCCESS" in response:
                    print("✅ Transfer completed successfully!")
                    return True
                elif "ERROR" in response:
                    print("❌ Transfer failed on ESP32 side")
                    return False
            
            time.sleep(0.1)
        
        print("⚠️  No confirmation received, but data was sent")
        return True
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        return False
    except FileNotFoundError:
        print(f"❌ File not found: {csv_file}")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    finally:
        if 'ser' in locals():
            ser.close()
            print("🔌 Serial connection closed")

def list_serial_ports():
    """List available serial ports"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("❌ No serial ports found")
        return
    
    print("📡 Available serial ports:")
    for port in ports:
        print(f"  🔌 {port.device} - {port.description}")

def main():
    parser = argparse.ArgumentParser(
        description='Send CSV file to ESP32',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('csv_file', nargs='?', help='Path to CSV file')
    parser.add_argument('serial_port', nargs='?', help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--output', '-o', default="categorizer.csv", help='Output filename on ESP32')
    parser.add_argument('--list-ports', action='store_true', help='List available serial ports')
    
    args = parser.parse_args()
    
    print("🚀 Simple CSV Transfer to ESP32")
    print("=" * 35)
    
    if args.list_ports:
        list_serial_ports()
        return
    
    if not args.csv_file or not args.serial_port:
        print("❌ Missing required arguments")
        print("\nUsage: python3 simple_csv_transfer.py <csv_file> <serial_port>")
        print("       python3 simple_csv_transfer.py --list-ports")
        print("\nExamples:")
        print("  python3 simple_csv_transfer.py categorizer.csv /dev/ttyUSB0")
        print("  python3 simple_csv_transfer.py categorizer.csv COM3")
        return
    
    print(f"📂 CSV File: {args.csv_file}")
    print(f"🔌 Serial Port: {args.serial_port}")
    print(f"⚡ Baud Rate: {args.baud}")
    print(f"💾 Output File: {args.output}")
    print()
    
    success = send_csv_to_esp32(args.csv_file, args.serial_port, args.baud, args.output)
    
    if success:
        print("\n🎉 Transfer completed!")
    else:
        print("\n💥 Transfer failed!")
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
