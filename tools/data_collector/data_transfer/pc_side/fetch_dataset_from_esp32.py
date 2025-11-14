#!/usr/bin/env python3
"""Pull an ESP32 dataset over Serial, preserving folder hierarchy.

This utility pairs with dataset_transfer_sender.ino. It requests a dataset
root from the ESP32 and mirrors the directory tree to the local filesystem.

Example:
    python3 fetch_dataset_from_esp32.py --dataset gesture --port /dev/ttyUSB0 \
        --output ./esp32_dataset_dump
"""

import argparse
import shutil
import struct
import sys
import time
from pathlib import Path
from typing import Optional, Tuple

import serial

CMD_HEADER = b"ESP32_XFER"
CMD_DATASET_REQUEST = 0x21
CMD_DATASET_FILE_INFO = 0x22
CMD_DATASET_FILE_CHUNK = 0x23
CMD_DATASET_FILE_END = 0x24
CMD_DATASET_DONE = 0x25

RESP_READY = "READY"
RESP_ERROR = "ERROR"
RESP_DONE = "DONE"

HEADER_LEN = len(CMD_HEADER)
DEFAULT_TIMEOUT = 1.0
RESERVED_FOLDERS = {"_sessions", "_session"}
data_collector_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RESULT_DIR = data_collector_ROOT / "result"


def _strip_dataset_prefix(path: str, dataset: str) -> str:
    trimmed = path.lstrip("/")
    dataset_prefix = dataset.strip("/")
    if dataset_prefix:
        prefix = dataset_prefix + "/"
        if trimmed.startswith(prefix):
            trimmed = trimmed[len(prefix):]
    return trimmed


def send_command(ser: serial.Serial, command: int, payload: bytes = b"") -> None:
    packet = CMD_HEADER + bytes([command]) + payload
    ser.write(packet)
    ser.flush()
    time.sleep(0.01)


def read_exact(ser: serial.Serial, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        chunk = ser.read(length - len(data))
        if not chunk:
            raise TimeoutError("Serial timeout while reading payload")
        data.extend(chunk)
    return bytes(data)


def wait_for_response(ser: serial.Serial, timeout: float = 5.0) -> str:
    end_time = time.time() + timeout
    buffer = bytearray()
    while time.time() < end_time:
        line = ser.readline()
        if not line:
            continue
        text = line.decode(errors="ignore").strip()
        if not text:
            continue
        if text.startswith(RESP_READY):
            return RESP_READY
        if text.startswith(RESP_ERROR):
            return RESP_ERROR
        if text.startswith(RESP_DONE):
            return RESP_DONE
        print(f"[ESP32] {text}")
    raise TimeoutError("Timeout waiting for ESP32 response")


def read_next_command(ser: serial.Serial) -> int:
    buffer = bytearray()
    while True:
        byte = ser.read(1)
        if not byte:
            continue
        # Skip newlines quickly
        if byte in b"\r\n":
            continue
        buffer += byte
        if len(buffer) > HEADER_LEN:
            buffer = buffer[-HEADER_LEN:]
        if bytes(buffer) == CMD_HEADER:
            command = ser.read(1)
            while not command:
                command = ser.read(1)
            return command[0]


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def format_size(bytes_count: int) -> str:
    if bytes_count < 1024:
        return f"{bytes_count} B"
    if bytes_count < 1024 * 1024:
        return f"{bytes_count / 1024.0:.1f} KB"
    return f"{bytes_count / (1024.0 * 1024.0):.2f} MB"


def transfer_dataset(ser: serial.Serial, dataset: str) -> Tuple[Path, Optional[Path]]:
    dataset_bytes = dataset.encode("utf-8")
    if len(dataset_bytes) > 250:
        raise ValueError("Dataset name too long")

    payload = bytes([len(dataset_bytes)]) + dataset_bytes
    print(f"🚀 Requesting dataset '{dataset}' from ESP32...")
    send_command(ser, CMD_DATASET_REQUEST, payload)

    resp = wait_for_response(ser, timeout=8.0)
    if resp == RESP_ERROR:
        print("❌ ESP32 reported an error while preparing dataset")
        raise RuntimeError("ESP32 did not accept dataset request")
    print("✅ ESP32 ready, streaming dataset...")

    dataset_dir = DEFAULT_RESULT_DIR / dataset
    dataset_dir.mkdir(parents=True, exist_ok=True)

    current_file = None
    current_path = None
    expected_size = 0
    received_bytes = 0
    files_received = 0
    total_bytes = 0
    skipping_current = False
    camera_config_path: Optional[Path] = None
    skipped_files = 0
    expected_config_name = f"{dataset.lower()}_camera_config.json"

    try:
        while True:
            command = read_next_command(ser)
            if command == CMD_DATASET_FILE_INFO:
                path_len_bytes = read_exact(ser, 2)
                path_len = struct.unpack("<H", path_len_bytes)[0]
                original_path = read_exact(ser, path_len).decode("utf-8", errors="ignore")
                relative_path = _strip_dataset_prefix(original_path, dataset)
                if not relative_path:
                    relative_path = original_path.lstrip("/")
                size_bytes = read_exact(ser, 4)
                expected_size = struct.unpack("<I", size_bytes)[0]

                if current_file:
                    current_file.close()
                    current_file = None

                path_obj = Path(relative_path)
                if ".." in path_obj.parts:
                    raise ValueError(f"Illegal path from ESP32: {relative_path}")

                skipping_current = any(part.lower() in RESERVED_FOLDERS for part in path_obj.parts)
                current_path = None
                received_bytes = 0

                if skipping_current:
                    print(f"   ⏭️  Skipping reserved path {relative_path}")
                else:
                    target_path = dataset_dir / path_obj
                    ensure_parent(target_path)
                    current_file = open(target_path, "wb")
                    current_path = target_path
                    if path_obj.name.lower() == expected_config_name:
                        camera_config_path = target_path
                    print(f"   ↪ Receiving {relative_path} ({format_size(expected_size)})")

            elif command == CMD_DATASET_FILE_CHUNK:
                header = read_exact(ser, 2)
                chunk_len = struct.unpack("<H", header)[0]
                chunk_data = read_exact(ser, chunk_len) if chunk_len else b""
                if current_file and chunk_len and not skipping_current:
                    current_file.write(chunk_data)
                if not skipping_current:
                    received_bytes += chunk_len

            elif command == CMD_DATASET_FILE_END:
                size_bytes = read_exact(ser, 4)
                reported_size = struct.unpack("<I", size_bytes)[0]
                if current_file:
                    current_file.flush()
                    current_file.close()
                    current_file = None
                if not skipping_current and reported_size != received_bytes:
                    print(f"⚠️  Size mismatch for {current_path}: expected {expected_size}, got {received_bytes}")
                if not skipping_current:
                    files_received += 1
                    total_bytes += received_bytes
                else:
                    skipped_files += 1
                skipping_current = False
                current_path = None

            elif command == CMD_DATASET_DONE:
                payload = read_exact(ser, 12)
                total_files = struct.unpack("<I", payload[:4])[0]
                total_streamed = struct.unpack("<Q", payload[4:])[0]
                print("\n🏁 Dataset stream complete")
                print(f"   Files: {files_received} / {total_files}")
                print(f"   Bytes: {format_size(total_bytes)} / {format_size(total_streamed)}")
                if skipped_files:
                    print(f"   Skipped: {skipped_files} (reserved paths)")
                break
            else:
                print(f"⚠️  Unexpected command 0x{command:02X}, skipping")
    finally:
        if current_file:
            current_file.close()

    try:
        done_resp = wait_for_response(ser, timeout=2.0)
        if done_resp == RESP_DONE:
            print("✅ ESP32 reported transfer DONE")
    except TimeoutError:
        pass

    print(f"\n📂 Dataset mirrored to: {dataset_dir}")
    return dataset_dir, camera_config_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch dataset from ESP32 over Serial")
    parser.add_argument("--dataset", default="gesture",
                        help="Dataset root folder on ESP32 (default: gesture)")
    parser.add_argument("--port", required=True,
                        help="Serial port (e.g. /dev/ttyUSB0)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help="Serial read timeout in seconds")
    args = parser.parse_args()

    DEFAULT_RESULT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"🔌 Connecting to ESP32 on {args.port}...")
    try:
        with serial.Serial(args.port, 115200, timeout=args.timeout) as ser:
            time.sleep(2.0)
            if ser.in_waiting:
                startup = ser.read(ser.in_waiting).decode(errors="ignore")
                startup = startup.strip()
                if startup:
                    print(f"[ESP32 boot]\n{startup}\n")
            dataset_dir, config_file = transfer_dataset(ser, args.dataset)
    except FileNotFoundError:
        print(f"❌ Serial port not found: {args.port}")
        return 1
    except TimeoutError as exc:
        print(f"❌ Timeout: {exc}")
        return 1
    except KeyboardInterrupt:
        print("\n⚠️  Transfer aborted by user")
        return 1
    except Exception as exc:
        print(f"❌ Transfer failed: {exc}")
        return 1

    config_target = None
    if config_file and config_file.exists():
        try:
            config_target = DEFAULT_RESULT_DIR / f"{args.dataset}_camera_config.json"
            shutil.copy2(config_file, config_target)
            print(f"📄 Camera config copied to: {config_target}")
        except Exception as exc:
            print(f"⚠️  Failed to copy camera config: {exc}")
    else:
        print("⚠️  Camera config file was not received")

    print(f"📁 Dataset stored at: {dataset_dir}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
