# Quantization - ondevice training - optimized container structure for microcontrollers (STL_MCU)

![Full pipeline overview](/docs/imgs/full_pipeline_overview.jpg)

This repository provides a compact, memory-efficient C++ toolkit for pre-training, embedding, and running quantum random forest models on microcontrollers, and even goes further - retraining models on the microcontroller itself, allowing them to adapt to new data throughout their lifecycle without reloading code or intervention. The entire project is built on a new foundation library, which includes containers with extreme memory optimization and fragmentation.

## Quantization — brief introduction

- Quantization packs the values ​​in each feature into bins and then normalizes them. This reduces the number of bits required to store each feature value.
- STL_MCU's pipeline supports per-feature bit allocation, packed storage formats, and decoder helpers to restore values at inference time.
- There are 8 quantization modes supported: from 1->8 bit. The default mode is 2-bit quantization.

![Compression & accuracy comparison](/report/imgs/compare_all.png)

Learn more about the quantization pipeline, benchmarks and recommendations in the full report: `report/README.md`.

## Library foundation — optimized containers

The entire library is built on a foundation of special container classes optimized for microcontrollers. These classes are implemented in `STL_MCU.h` and live in the `mcu` namespace. They provide memory-efficient variants of common STL containers (vectors, maps, sets) and additional primitives that prefer contiguous allocations and PSRAM when available.

Key points:

- Namespace: `mcu`
- Primary header: `STL_MCU.h`
- Purpose-built containers: `mcu::vector`, `mcu::b_vector`, `mcu::packed_vector`, `mcu::ID_vector`, `mcu::unordered_map`, `mcu::unordered_set`... 
- Designed for low fragmentation, small code size, and optional PSRAM use.

## Extended support
- Memory : support PSRAM accross all containers and algorithms
- Storage : support LITTLEFS, FATFS, SD_MMC_1BIT, SD_MMC_4BIT, SD_SPI.
- Board : tested on ESP32, ESP32-C3, ESP32-S3, ESP32-CAM, dev modules, super mini boards...
- Cross-platform extension: Macros and declarations support porting to other platforms like Arduino, STM32...

For API details, configuration macros, and usage examples see the [main documentation](./docs/) folder in this repository.

---

