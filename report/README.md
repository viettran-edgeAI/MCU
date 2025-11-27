# STL_MCU — Quantization & Performance Report

This document summarizes the compression, precision, inference performance, and memory behavior of quantized random-forest models produced by the STL_MCU toolchain. The reports and benchmarks are run on 7 datasets provided in the tools/data_quantization/data/ folder: iris_data, cancer_data, walker_fall, digit_data, run_walk, mnist, emnist with various sizes, number of samples and classes.

**What this file contains:** a brief introduction, compression & precision tables, inference-time benchmark notes and graphs, memory/disk/fragmentation observations, and a short conclusion.

**Note:** 
- The entire model used in this report is built with 20 trees and 2-bit quantization by default.
- The models running on the original dataset were run with the random forest algorithm in scikit-learn with default parameters (20 trees, oob_score) and could be higher if the parameters were tweaked.

---

## Compression & Precision

The table below reports dataset and model compression achieved by variable-bit quantization alongside resulting accuracy changes.

| Dataset | Dataset size (orig → quant) | Dataset ratio | Model size (orig → quant) | Model ratio | Accuracy orig → quant |
|---|---:|---:|---:|---:|---:|
| iris_data (150) | 2.8 KB → 0.3 KB | x9.3 | 30 KB → 2 KB | x15.0 | 1.000 → 1.000 |
| cancer_data (570) | 121 KB → 5.1 KB | x23.7 | 70 KB → 3 KB | x23.3 | 0.965 → 0.956 |
| digit_data (2000) | 2.0 MB → 74 KB | x27.7 | 1.35 MB → 36.9 KB | x37.5 | 0.951 → 0.958 |
| run_walk (89000) | 4.3 MB → 266 KB | x16.6 | 2.58 MB → 17.4 KB | x151.7 | 0.991 → 0.978 |
| walker_fall (2400) | 16.2 MB → 598 KB | x27.8 | 0.32 MB → 16.4 KB | x20.0 | 0.968 → 0.968 |
| Mnist (70000) | 73.4 MB → 2.6 MB | x28.2 | 17.2 MB → 0.563 MB | x30.6 | 0.960 → 0.958 |
| Emnist (274000) | 151 MB → 5.2 MB | x29.0 | 3424 MB → 4.75 MB | x721.3 | 0.823 → 0.795 |

Note: Compression ratios are rounded and computed as (original size / quantized size). Sizes use 1 MB = 1024 KB.

Important clarification: The reported **model_size** mentioned is the model size when loaded into RAM. The model file size can be larger (0 -> 40%) due to the dynamic node layout packing mechanism when the model is loaded into RAM.

Embedded visuals (relative compression and accuracy):

![Combined comparison](./imgs/compare_all.png)

---

## Inference Time (benchmark)

The performance table below summarizes inference time and RAM required across datasets.

| Dataset | # Features | Inference time | 
|---|---:|---:|
| iris_data | 4 | 0.313 ms |
| cancer_data | 30 | 0.389 ms |
| digit_data | 144 | 0.900 ms |
| run_walk | 7 | 0.393 ms |
| Mnist | 72 | 0.763 ms |

Performance plot:

![Inference time vs #features](./imgs/inference_report.png)

Benchmark note: these inference-time results were recorded on an `esp32-cam` board using an SD_MMC interface in 1-bit mode with an external SD card and PSRAM enabled. In practice, inference can be faster when running on devices with built-in flash/PSRAM or with SD_MMC in 4-bit mode (or when model assets are stored on internal flash or faster storage). Use these results as a representative baseline: your board, SD interface mode, PSRAM availability, and storage location (SD vs internal flash) will affect measured throughput.

## Memory Usage, Disk & Fragmentation

The repo includes memory logs and visualizations showing heap usage, largest free block (fragmentation), and free disk over time for representative runs.

Files of interest:

- `esp32_cam_mlog.txt` + `imgs/memory_report.png` — Model building log with walker_fall dataset, running on esp32_cam board with 4MB PSRAM + 2.75MB Flash.
- `esp32c3_mini_mlog.txt` `imgs/esp32_mini_mreport.png` — Model building log with digit_data dataset, running on esp32c3 super mini board with 283 KB RAM + 3 MB Flash.

Memory usage plots:

![esp32_cam board - enable PSRAM](./imgs/esp32_cam_mreport.png)

![esp32c3 super mini board - minimal resources](./imgs/esp32_mini_mreport.png)

**Notes:**
- Since processes run single-core, memory and disk usage times are recorded at the peak times of each stage, not measured in parallel.
- Largest Free Block shows the memory fragmentation, the larger the value the better. The algorithm is optimized to use memory fragments instead of abusing long memories, extremely good against fragmentation.
- FreeDisk is a filesystem storage level. In this source code, many storage types are supported depending on the user's specifications: FATFS, LITTLEFS, SD_MMC_1BIT, SD_MMC_4BIT, SD_SPI
---

## Conclusion

- Quantization yields significant dataset and model compression (commonly 10-30x depending on dataset and bit choices) while preserving accuracy for many use-cases.
- The model size is already optimally packed when stored, but can be even smaller when loaded into RAM thanks to the dynamic node layout packing mechanism.
- inference latency depends most strongly on the number of features (mainly through the quantization layer), followed by the depth of the model. RAM type has a slight impact (PSRAM will be a bit slower)

References and further reading:

- `docs/` - all documentation related to STL_MCU library
