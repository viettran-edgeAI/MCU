#!/usr/bin/env python3
"""
ESP32 Diagnostic Tool
Simple tool to diagnose ESP32 communication issues
"""

import serial
import time
import sys

def diagnose_esp32(port, baud=115200):
    """Run diagnostic tests on ESP32"""
    print(f"🔧 ESP32 Diagnostic Tool")
    print(f"🔌 Port: {port}")
    print(f"⚡ Baud: {baud}")
    print("=" * 40)
    
    try:
        print("📡 Step 1: Opening serial connection...")
        ser = serial.Serial(port, baud, timeout=1)
        print("✅ Serial connection opened successfully")
        
        print("\n📡 Step 2: Waiting for ESP32 to stabilize...")
        time.sleep(3)
        
        print("\n📡 Step 3: Checking for any ESP32 output...")
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            print(f"📨 ESP32 output: {repr(data)}")
        else:
            print("⚠️  No output from ESP32")
            
        print("\n📡 Step 4: Sending newline to trigger ESP32...")
        ser.write(b'\n')
        time.sleep(1)
        
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            print(f"📨 ESP32 response: {repr(data)}")
        else:
            print("⚠️  No response from ESP32")
            
        print("\n📡 Step 5: Sending 'receive' command...")
        ser.write(b'receive\n')
        time.sleep(2)
        
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            print(f"📨 ESP32 response: {repr(data)}")
            if "READY" in data:
                print("✅ ESP32 responded correctly!")
            else:
                print("⚠️  ESP32 response doesn't contain 'READY'")
        else:
            print("❌ No response to 'receive' command")
            
        print("\n📡 Step 6: Listening for 10 seconds...")
        start_time = time.time()
        all_data = ""
        while time.time() - start_time < 10:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                all_data += data
                print(f"📨 {data.strip()}")
            time.sleep(0.1)
            
        if not all_data:
            print("❌ ESP32 is completely silent")
            print("\n🔧 Troubleshooting suggestions:")
            print("   1. Check if ESP32 is powered on (LED should be on)")
            print("   2. Press the ESP32 RESET button")
            print("   3. Make sure the categorizer sketch is uploaded")
            print("   4. Check if another program is using the serial port")
            print("   5. Try a different USB cable")
            
        ser.close()
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        print("\n🔧 Possible solutions:")
        print("   1. Check if the port exists: ls -la /dev/ttyACM*")
        print("   2. Check permissions: sudo usermod -a -G dialout $USER")
        print("   3. Try a different port (ttyUSB0, ttyACM1, etc.)")
        
    except Exception as e:
        print(f"❌ Unexpected error: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 esp32_diagnostic.py <serial_port>")
        print("Example: python3 esp32_diagnostic.py /dev/ttyACM0")
        sys.exit(1)
        
    diagnose_esp32(sys.argv[1])
