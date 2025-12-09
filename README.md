# Quantization - ondevice training for microcontrollers 

![Full pipeline overview](/docs/imgs/full_pipeline_overview.jpg)

This repository provides a compact, memory-efficient C++ toolkit for pre-training, embedding, and running quantum random forest models on microcontrollers, and even goes further - retraining models on the microcontroller itself, allowing them to adapt to new data throughout their lifecycle without reloading code or intervention. The entire project is built on a new foundation library, which includes containers with extreme memory optimization and defragmentation.

## Quantization — brief introduction

- Quantization packs the values ​​in each feature into bins and then normalizes them. This reduces the number of bits required to store each feature value.
- STL_MCU's pipeline supports per-feature bit allocation, packed storage formats, and decoder helpers to restore values at inference time.
- There are 8 quantization modes supported: from 1->8 bit. The default mode is 2-bit quantization.

![Compression & accuracy comparison](/report/imgs/compare_all.png)

Learn more about the quantization pipeline, benchmarks and recommendations in the [project report](./report/README.md).

## Library Foundation — Optimized Containers

The entire library is built on a foundation of special container classes optimized for microcontrollers. These classes are implemented in `STL_MCU.h` and reside in the `mcu` namespace. They provide super memory-efficient variants of common STL containers (vector, map, set) and additional primitives that prioritize contiguous allocation and PSRAM when possible.

Key Points: 
- Namespace: `mcu`
- new hash kernel based containers: `unordered_map_s`, `unordered_set_s`, `unordered_map`, `unordered_set`.
- new vector architectures: `vector` & `b_vector` , `packed_vector` & `ID_vector`.
- Designed for low fragmentation, small code size, and optional PSRAM use.

Read more about these containers in the [STL_MCU documentation](./docs/STL_MCU.md).

## Demonstration examples
- Demo of complete workflow: collect data, pre-train (on pc), run model, re-train and update model in real time entirely on esp32 - [ondevice training](./examples/retrain_ondevice_demo/)

## Extended support
- Memory : support PSRAM accross all containers and algorithms
- Storage : support LITTLEFS, FATFS, SD_MMC_1BIT, SD_MMC_4BIT, SD_SPI.
- Chip & Board : tested on ESP32, ESP32-C3, ESP32-S3, ESP32-CAM, dev modules, super mini boards...
- Cross-platform extension: Macros and declarations support porting to other platforms like Arduino, STM32...

For API details, configuration macros, and usage examples see the [main documentation](./docs/) folder in this repository.

---

