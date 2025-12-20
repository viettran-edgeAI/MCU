# Data Collector Tools

This directory contains tools for collecting and transferring image datasets from ESP32 devices to your PC.

## Components

### 📡 Dataset Capture Server
- **Location**: `dataset_capture_server/`
- **Purpose**: ESP32 sketch that runs a Wi-Fi web interface for capturing image datasets
- **Features**:
  - Real-time camera streaming
  - Dataset and class label configuration
  - Image capture with automatic organization
  - Camera configuration export

### 🔄 Data Transfer
- **Location**: `data_transfer/`
- **Purpose**: Transfer captured datasets from ESP32 to PC
- **Components**:
  - `esp32_side/dataset_transfer_sender.ino` - ESP32 sender sketch
  - `pc_side/dataset_receiver.py` - Python script to receive datasets

### 📁 Results
- **Location**: `result/`
- **Purpose**: Storage for transferred datasets and configuration files
- **Contents**: Dataset folders with images and camera configuration JSON files

## Quick Start

1. **Upload capture server** to ESP32:
   ```bash
   # Open dataset_capture_server.ino in Arduino IDE and upload
   ```

2. **Configure and capture** dataset via web interface (default: http://esp32-cam-ip)

3. **Transfer to PC**:
   ```bash
   cd data_transfer/pc_side
   python3 dataset_receiver.py --dataset <dataset_name> --port <serial_port>
   ```

4. **Use with HOG transform** (see `README_hog_integration.md`)

## Integration

The captured datasets are designed to work seamlessly with:
- `tools/hog_transform/` - Feature extraction pipeline
- `tools/data_quantization/` - Dataset processing and quantization

---

# Using `data_collector` output with `hog_transform`

This toolkit allows you to:

1. Capture a dataset on the ESP32 (images + labels).
2. Transfer the dataset and its camera configuration file to the PC (`result/` folder).
3. Use that dataset as input to the HOG feature extraction tool in `tools/hog_transform/`.

The key step after collecting and transferring a dataset is to configure the `hog_config.json` file so that `hog_transform` knows which dataset and camera settings to use.

## Runtime capture server overview

`tools/data_collector/dataset_capture_server.ino` runs directly on the ESP32-CAM and serves a Wi-Fi dashboard, letting you:

- Set the dataset name that becomes the root folder (`tools/data_collector/result/<dataset_name>/`).
- Supply the class label you plan to capture before pressing **Start Capture**.
- Stream frames, stop, and finish the session before the built-in save flow moves images under `<dataset_name>/<class_name>/`.
- Export a camera configuration JSON named `<dataset_name>_camera_config.json` so downstream tooling knows the exact lens settings.

The dashboard now enforces this ordering: the dataset name and class label must both be configured before a capture session can begin, and class entry happens at the beginning of a session instead of after it finishes. The server sanitizes both values (removing reserved names such as `_sessions`/`_session`) and reuses them in the transfer pipeline and camera config export.

---

## 1. What you get from `data_collector`

After you run the capture and transfer pipeline, you will have:

- A dataset folder under `tools/data_collector/result/`, e.g.:
  - `tools/data_collector/result/gesture/`
- A camera configuration JSON file for this dataset, e.g.:
  - `tools/data_collector/result/gesture/gesture_camera_config.json`

The camera config file contains fields describing:

- Sensor resolution (width, height)
- Pixel format / color mode
- Cropping / ROI info (if used)
- Any ESP32-specific camera parameters needed for HOG

You will reuse **exactly** these ESP32 camera settings in the next step.

---

## 2. Overall workflow

### High-level pipeline

```text
+-----------+      capture      +-------------+      transfer      +-----------------+
|  ESP32    |  -------------->  |   dataset   |  -------------->  |       PC        |
|  camera   |   images+labels   |  on ESP32   |   dataset+config  |   result/ dir   |
+-----------+                    +-------------+                   +-----------------+
                                                                   |
                                                                   v
                                                           +-----------------+
                                                           |  hog_transform  |
                                                           | (tools/hog_*)   |
                                                           +-----------------+
```

### With file locations

```text
[1] Capture on ESP32
    - Sketch: tools/data_collector/dataset_capture_server.ino

[2] Transfer to PC
    - ESP32 side: tools/data_collector/data_transfer/esp32_side/dataset_transfer_sender.ino
    - PC side:    tools/data_collector/data_transfer/pc_side/dataset_receiver.py

[3] Result on PC
  - Dataset:    tools/data_collector/result/<dataset_name>/<dataset_name>.csv (or equivalent dataset files) at the root of the dataset folder.
  - Camera cfg: tools/data_collector/result/<dataset_name>/<dataset_name>_camera_config.json

[4] HOG transform
    - Config:     tools/hog_transform/hog_config.json
    - Tool code:  tools/hog_transform/
```

---

## 3. Configure `hog_config.json` for a new dataset

After the dataset is collected and transferred, you must tell `hog_transform` which dataset to use and how the ESP32 camera was configured.

### Step 1 – Choose `dataset_name`

Decide the dataset name you are using. This is the folder name created under `result/`. For example:

- If the dataset is in `tools/data_collector/result/gesture/gesture/`, then:
  - `dataset_name` is `gesture`.

Open `tools/hog_transform/hog_config.json` and set the `dataset_name` field:

```jsonc
{
  "dataset_name": "gesture",
  // ... other fields ...
}
```

> Use **only** the dataset folder name (no full path). The `hog_transform` tool knows that datasets live under `tools/data_collector/result/`.

### Step 2 – Copy ESP32 camera settings

Open the camera config file generated by `data_collector`, for example:

- `tools/data_collector/result/gesture/gesture/gesture_camera_config.json`

In `hog_config.json`, there is an `esp32` (or similar) object that stores the ESP32 camera configuration. **Copy this block directly from the camera config file** so that it matches the real capture settings.

Conceptually, you will do something like this:

```jsonc
// In tools/hog_transform/hog_config.json
{
  "dataset_name": "gesture",

  "esp32": {
    // Copy everything inside this object
    // directly from <dataset_name>_camera_config.json

    "frame_width":  96,
    "frame_height": 96,
    "pixel_format": "GRAYSCALE",
    "fps": 30
    // ... any other fields present in the camera config ...
  }

  // ... other HOG configuration parameters ...
}
```

**Important:**

- Do **not** change field names inside the `esp32` block.
- Keep all values exactly the same as in `<dataset_name>_camera_config.json`.
- If the camera config file contains additional parameters (crop, offsets, etc.), copy them all into the `esp32` section of `hog_config.json`.

This ensures that the HOG transform is computed with the **same geometry and format** as the images captured on the ESP32.

---

## 4. End-to-end workflow diagram

```text
            +-----------------------------+
            |  1. Capture on ESP32        |
            |  - dataset_capture_server   |
            |    (ESP32 sketch)           |
            +-------------+---------------+
                          |
                          | images + labels
                          v
            +-----------------------------+
            |  2. Store on ESP32          |
            |  - local SPIFFS / flash     |
            +-------------+---------------+
                          |
                          | serial transfer
                          v
            +-----------------------------+
            |  3. Transfer to PC          |
            |  - dataset_transfer_sender  |
            |    (ESP32)                  |
            |  - dataset_receiver.py      |
            |    (Python, PC)             |
            +-------------+---------------+
                          |
                          | files written under
                          | tools/data_collector/result/
                          v
            +-----------------------------+
            |  4. Configure hog_config    |
            |  - set dataset_name         |
            |  - copy esp32 block from    |
            |    <dataset_name>_camera_   |
            |    config.json              |
            +-------------+---------------+
                          |
                          | run hog_transform
                          v
            +-----------------------------+
            |  5. HOG feature extraction  |
            |  - outputs features for RF  |
            +-----------------------------+
```

With this setup, every dataset captured via `data_collector` can be used consistently with `hog_transform`, ensuring that the model sees exactly the same image geometry as the ESP32 camera during deployment.
