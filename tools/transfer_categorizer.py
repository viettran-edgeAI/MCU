#!/usr/bin/env python3
"""
ESP32 Categorizer Transfer Utility

This script sends the categorizer CSV file to ESP32 via Serial port.
Usage: python3 transfer_categorizer.py <csv_file> <serial_port>
"""

import serial
import time
import sys
import argparse

def send_categorizer_to_esp32(csv_file, serial_port, baud_rate=115200):
    """
    Send categorizer CSV file to ESP32 via Serial
    
    Args:
        csv_file: Path to the categorizer CSV file
        serial_port: Serial port name (e.g., 'COM3' on Windows, '/dev/ttyUSB0' on Linux)
        baud_rate: Serial communication baud rate
    """
    
    try:
        # Open serial connection
        print(f"Connecting to {serial_port} at {baud_rate} baud...")
        ser = serial.Serial(serial_port, baud_rate, timeout=1)
        time.sleep(2)  # Wait for ESP32 to initialize
        
        # Read CSV file
        print(f"Reading categorizer file: {csv_file}")
        with open(csv_file, 'r') as f:
            csv_data = f.read().strip()
        
        # Send command to ESP32 to prepare for reception
        print("Sending 'receive' command to ESP32...")
        ser.write(b'receive\n')
        time.sleep(1)
        
        # Wait for ESP32 to respond
        response = ""
        start_time = time.time()
        while time.time() - start_time < 5:  # 5 second timeout
            if ser.in_waiting:
                response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                if "Ready to receive" in response:
                    break
            time.sleep(0.1)
        
        if "Ready to receive" not in response:
            print("ESP32 not ready to receive data")
            print("Response:", response)
            return False
        
        print("ESP32 is ready. Sending categorizer data...")
        
        # Send CSV data line by line
        lines = csv_data.split('\n')
        for i, line in enumerate(lines):
            if line.strip():  # Skip empty lines
                print(f"Sending line {i+1}/{len(lines)}: {line[:50]}{'...' if len(line) > 50 else ''}")
                ser.write(f"{line}\n".encode('utf-8'))
                time.sleep(0.1)  # Small delay between lines
        
        # Send end marker
        print("Sending end marker...")
        ser.write(b'EOF_CATEGORIZER\n')
        
        # Wait for confirmation
        print("Waiting for ESP32 confirmation...")
        response = ""
        start_time = time.time()
        while time.time() - start_time < 30:  # 30 second timeout for processing
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                response += data
                print(data, end='')  # Print ESP32 output in real-time
                
                if "converted successfully" in response:
                    print("\n✓ Categorizer transferred and converted successfully!")
                    return True
                elif "Failed" in response:
                    print("\n✗ Transfer failed!")
                    return False
            time.sleep(0.1)
        
        print("\nTimeout waiting for ESP32 response")
        return False
        
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        return False
    except FileNotFoundError:
        print(f"File not found: {csv_file}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        if 'ser' in locals():
            ser.close()

def list_serial_ports():
    """List available serial ports"""
    import serial.tools.list_ports
    
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found")
        return
    
    print("Available serial ports:")
    for port in ports:
        print(f"  {port.device} - {port.description}")

def main():
    parser = argparse.ArgumentParser(description='Transfer categorizer to ESP32')
    parser.add_argument('csv_file', nargs='?', help='Path to categorizer CSV file')
    parser.add_argument('serial_port', nargs='?', help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--list-ports', action='store_true', help='List available serial ports')
    
    args = parser.parse_args()
    
    if args.list_ports:
        list_serial_ports()
        return
    
    if not args.csv_file or not args.serial_port:
        print("Usage: python3 transfer_categorizer.py <csv_file> <serial_port>")
        print("       python3 transfer_categorizer.py --list-ports")
        print("\nExample:")
        print("  python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyUSB0")
        print("  python3 transfer_categorizer.py categorizer_esp32.csv COM3")
        return
    
    success = send_categorizer_to_esp32(args.csv_file, args.serial_port, args.baud)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
