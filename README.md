# Quantization - ondevice training - optimized container structure for microcontrollers (STL_MCU)

![Full pipeline overview](/docs/imgs/full_pipeline_overview.png)

This repository provides a compact, memory-conscious C++ toolkit for running quantized random-forest models and on-device training workflows on microcontrollers (ESP32 family). The project focuses on minimizing RAM/flash usage while preserving prediction accuracy through variable-bit quantization and efficient in-memory layouts.

## Quantization — brief introduction

Quantization reduces numeric precision (bits per feature or parameter) to compress datasets and models. When applied carefully, quantization can produce large reductions in storage and memory (often 10–30x) with minimal accuracy loss. STL_MCU's pipeline supports per-feature bit allocation, packed storage formats, and decoder helpers to restore values at inference time.

![Compression & accuracy comparison](/report/imgs/compare_all.png)

Learn more about the quantization pipeline, benchmarks and recommendations in the full report: `report/README.md`.

## Library foundation — optimized containers

The entire library is built on a foundation of special container classes optimized for microcontrollers. These classes are implemented in `STL_MCU.h` and live in the `mcu` namespace. They provide memory-efficient variants of common STL containers (vectors, maps, sets) and additional primitives that prefer contiguous allocations and PSRAM when available.

Key points:

- Namespace: `mcu`
- Primary header: `STL_MCU.h`
- Purpose-built containers: `mcu::vector`, `mcu::b_vector, `mcu::packed_vector`, `mcu::ID_vector`, mcu::unordered_map`, `mcu::unordered_setp`,  and others
- Designed for low fragmentation, small code size, and optional PSRAM use

For API details, configuration macros, and usage examples see the `docs/` folder in this repository.

---

If you'd like, I can also:

- Restore or add a short Quick Start and Installation section to this README, or
- Move selected images to the repository root so they appear in the project overview on GitHub's landing page.

