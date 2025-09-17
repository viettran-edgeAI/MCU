import serial
import time

# Adjust your ESP32 port (check with ls /dev/ttyUSB* or /dev/ttyACM*)
PORT = "/dev/ttyACM0"
BAUD = 115200
OUTPUT_FILE = "received.bin"

ser = serial.Serial(PORT, BAUD, timeout=5)
time.sleep(2)  # give ESP32 time to reset

# Send START command
ser.write(b"START\n")

# Read file size
file_size_line = ser.readline().decode().strip()
if not file_size_line.isdigit():
    print("Invalid file size response:", file_size_line)
    exit(1)

file_size = int(file_size_line)
print(f"File size: {file_size} bytes")

received = 0
with open(OUTPUT_FILE, "wb") as f:
    while received < file_size:
        chunk = ser.read(512)  # must match CHUNK_SIZE
        if not chunk:
            print("Timeout or transfer error")
            break
        f.write(chunk)
        received += len(chunk)

        # Send ACK
        ser.write(b"ACK\n")

print(f"Received {received}/{file_size} bytes")
done_msg = ser.readline().decode().strip()
print("ESP32 says:", done_msg)

ser.close()
