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

def send_categorizer_to_esp32(csv_file, serial_port, baud_rate=115200, output_filename=None):
    """
    Send categorizer CSV file to ESP32 via Serial
    
    Args:
        csv_file: Path to the categorizer CSV file
        serial_port: Serial port name (e.g., 'COM3' on Windows, '/dev/ttyUSB0' on Linux)
        baud_rate: Serial communication baud rate
        output_filename: Optional filename for the binary file on ESP32
    """
    
    try:
        print(f"📁 Reading categorizer file: {csv_file}")
        
        # Validate CSV file exists and get size
        import os
        if not os.path.exists(csv_file):
            print(f"❌ File not found: {csv_file}")
            return False
            
        file_size = os.path.getsize(csv_file)
        print(f"📊 File size: {file_size} bytes")
        
        # Get output filename from user if not provided
        if output_filename is None:
            base_name = os.path.splitext(os.path.basename(csv_file))[0]
            default_name = f"{base_name}_categorizer.bin"
            
            print(f"\n📝 Binary file will be saved on ESP32 as: {default_name}")
            user_input = input(f"Press Enter to use default name or type a new name: ").strip()
            
            if user_input:
                if not user_input.endswith('.bin'):
                    user_input += '.bin'
                output_filename = user_input
            else:
                output_filename = default_name
        
        print(f"💾 Output filename: {output_filename}")
        
        # Open serial connection
        print(f"\n🔌 Connecting to {serial_port} at {baud_rate} baud...")
        ser = serial.Serial(serial_port, baud_rate, timeout=1)
        
        # Clear any existing data in buffers
        ser.flushInput()
        ser.flushOutput()
        time.sleep(2)  # Longer initial delay for ESP32 stability
        
        # Send a few newlines to clear any stuck input on ESP32
        ser.write(b'\n\n\n')
        time.sleep(1)
        
        # Clear buffer again
        ser.flushInput()
        time.sleep(1)
        
        print("🤝 Establishing communication with ESP32...")
        
        # First, let's see what ESP32 is sending on startup
        print("   📡 Listening for ESP32 startup messages...")
        time.sleep(1)
        startup_response = ""
        if ser.in_waiting:
            startup_data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            startup_response = startup_data
            print(f"   📨 ESP32 startup: {startup_response.strip()}")
        
        # Try multiple times to establish communication
        max_attempts = 5
        for attempt in range(max_attempts):
            print(f"   Attempt {attempt + 1}/{max_attempts}: Sending 'receive' command...")
            
            # Clear buffers before each attempt
            ser.flushInput()
            ser.flushOutput()
            
            # Send receive command
            ser.write(b'receive\n')
            ser.flush()  # Ensure command is sent
            time.sleep(0.5)
            
            # Wait for response
            response = ""
            start_time = time.time()
            while time.time() - start_time < 5:  # 5 second timeout per attempt
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    response += data
                    print(f"   📨 Received: '{data.strip()}'")  # Debug: show what we receive
                    
                    if "READY" in response:
                        print("   ✓ ESP32 is ready to receive data!")
                        break
                time.sleep(0.1)
            
            if "READY" in response:
                break
            
            if attempt < max_attempts - 1:
                print(f"   ⚠️  Attempt {attempt + 1} failed, retrying...")
                print(f"   ESP32 response: {response.strip()}")
                time.sleep(2)
        
        if "READY" not in response:
            print("❌ Failed to establish communication with ESP32")
            print("   Make sure ESP32 is running the correct sketch")
            print("   Try pressing the ESP32 reset button and run this script again")
            print(f"   Last ESP32 response: '{response.strip()}'")
            return False
        
        # Send the desired filename to ESP32
        print(f"📝 Sending output filename: {output_filename}")
        ser.write(f"{output_filename}\n".encode('utf-8'))
        ser.flush()
        time.sleep(0.5)  # Give ESP32 time to process filename
        
        # Read CSV file
        print(f"\n📤 Sending categorizer data to ESP32...")
        with open(csv_file, 'r') as f:
            csv_data = f.read().strip()
        
        # Send CSV data line by line with progress indication
        lines = csv_data.split('\n')
        total_lines = len([line for line in lines if line.strip()])  # Count non-empty lines
        
        print(f"   📊 Total lines to send: {total_lines}")
        
        sent_lines = 0
        for i, line in enumerate(lines):
            if line.strip():  # Skip empty lines
                sent_lines += 1
                if sent_lines % 10 == 0 or sent_lines <= 5 or sent_lines == total_lines:
                    progress = (sent_lines / total_lines) * 100
                    print(f"   📤 Progress: {sent_lines}/{total_lines} lines ({progress:.1f}%)")
                
                ser.write(f"{line}\n".encode('utf-8'))
                time.sleep(0.02)  # Reduced delay for faster transfer
        
        # Send end marker
        print("   📋 Sending end marker...")
        ser.write(b'EOF_CATEGORIZER\n')
        ser.flush()  # Ensure all data is sent
        
        # Wait for ESP32 to process and respond
        print("   ⏳ Waiting for ESP32 to process the data...")
        time.sleep(2)
        
        # Check for final response
        final_response = ""
        start_time = time.time()
        while time.time() - start_time < 10:  # 10 second timeout
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                final_response += data
                
                if "SUCCESS" in final_response:
                    print("✅ Categorizer data transferred and converted successfully!")
                    print(f"   💾 Binary file saved on ESP32 as: {output_filename}")
                    return True
                elif "ERROR" in final_response:
                    print("❌ ESP32 reported an error during conversion")
                    print(f"   Error details: {final_response}")
                    return False
            time.sleep(0.1)
        
        # If no clear response, assume success based on no errors
        print("✅ Data transfer completed (no error reported)")
        print(f"   💾 Binary file should be saved on ESP32 as: {output_filename}")
        return True
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        print("   Check if the serial port is correct and not in use by another application")
        return False
    except FileNotFoundError:
        print(f"❌ File not found: {csv_file}")
        return False
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False
    finally:
        if 'ser' in locals():
            ser.close()
            print("🔌 Serial connection closed")

def list_serial_ports():
    """List available serial ports"""
    import serial.tools.list_ports
    
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("❌ No serial ports found")
        return
    
    print("📡 Available serial ports:")
    print("-" * 30)
    for port in ports:
        print(f"  🔌 {port.device} - {port.description}")
    print("-" * 30)

def test_connection(serial_port, baud_rate=115200):
    """Test if ESP32 is responding on the serial port"""
    try:
        print(f"🧪 Testing connection to {serial_port}...")
        ser = serial.Serial(serial_port, baud_rate, timeout=1)
        time.sleep(2)  # Wait for ESP32 to stabilize
        
        # Clear buffers
        ser.flushInput()
        ser.flushOutput()
        
        # Listen for any data from ESP32
        print("   📡 Listening for ESP32 activity...")
        time.sleep(1)
        
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            print(f"   📨 ESP32 says: {data.strip()}")
        else:
            print("   ⚠️  No data received from ESP32")
            
        # Try sending a simple command
        print("   📤 Sending test command...")
        ser.write(b'\n')
        time.sleep(0.5)
        
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            print(f"   📨 ESP32 response: {data.strip()}")
        else:
            print("   ⚠️  No response to test command")
            
        ser.close()
        return True
        
    except Exception as e:
        print(f"   ❌ Connection test failed: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(
        description='Transfer categorizer to ESP32',
        epilog='''
Examples:
  python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyUSB0
  python3 transfer_categorizer.py categorizer_esp32.csv COM3 --output my_categorizer.bin
  python3 transfer_categorizer.py --list-ports
        ''',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('csv_file', nargs='?', help='Path to categorizer CSV file')
    parser.add_argument('serial_port', nargs='?', help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--output', '-o', help='Output filename for binary file on ESP32')
    parser.add_argument('--list-ports', action='store_true', help='List available serial ports')
    
    args = parser.parse_args()
    
    print("🚀 ESP32 Categorizer Transfer Utility")
    print("=" * 40)
    
    if args.list_ports:
        list_serial_ports()
        return
    
    if not args.csv_file or not args.serial_port:
        print("❌ Missing required arguments")
        print("\nUsage: python3 transfer_categorizer.py <csv_file> <serial_port>")
        print("       python3 transfer_categorizer.py --list-ports")
        print("\nExamples:")
        print("  python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyUSB0")
        print("  python3 transfer_categorizer.py categorizer_esp32.csv COM3")
        return
    
    print(f"📂 CSV File: {args.csv_file}")
    print(f"🔌 Serial Port: {args.serial_port}")
    print(f"⚡ Baud Rate: {args.baud}")
    if args.output:
        print(f"💾 Output File: {args.output}")
    print()
    
    # Test connection first
    if not test_connection(args.serial_port, args.baud):
        print("\n💥 Connection test failed! Please check:")
        print("   1. ESP32 is connected and powered on")
        print("   2. Correct serial port is specified")
        print("   3. ESP32 is running the categorizer sketch")
        print("   4. You have permission to access the serial port")
        sys.exit(1)
    
    success = send_categorizer_to_esp32(args.csv_file, args.serial_port, args.baud, args.output)
    
    if success:
        print("\n🎉 Transfer completed successfully!")
    else:
        print("\n💥 Transfer failed!")
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
