import serial

ser = serial.Serial("/dev/ttyACM0", 115200)
print("🕓 Waiting for START...")

# Chờ START
while True:
    line = ser.readline().decode(errors="ignore").strip()
    if line == "START":
        break

# Nhận kích thước file
file_size = 0
while True:
    line = ser.readline().decode(errors="ignore").strip()
    if line.isdigit():
        file_size = int(line)
        break

print(f"📦 Expecting {file_size} bytes...")

output = open("received.bin", "wb")
received = 0

while received < file_size:
    data = ser.read(min(1024, file_size - received))  # đọc theo khối
    output.write(data)
    received += len(data)

output.close()
ser.close()
print(f"✅ Done. Received {received} bytes.")
