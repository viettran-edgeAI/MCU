# Changelog

All notable changes to this project will be documented in this file.
The format is based on "Keep a Changelog" and groups small, close-together commits into meaningful releases.

## [Unreleased] - 2025-11-17
### Added
- New file-transfer tool: `data_transfer` to move files to ESP32 (ondevice training demo). (171125)
- `data_collector` tool for live data capture using ESP32-CAM; streaming inference test and full pipeline test added. (171125 / 141125)
- New example datasets and examples for streaming inference and on-device training. (171125)
- Configuration: `max_samples`, `set_max_samples()`, `min_leaf`, partial loading support improvements; new Rf_board_config.h macros for SD_card & microSD. (11-10..11-12)

### Changed
- Add `resize()` overloads, `emplace()` and `emplace_back()` to `vector` and `b_vector`. (171125)
- Added `get_all_original_labels()` and `best_training_score()` to `random_forest_mcu`. (171125)

### Fixed
- Clean-up and small bug fixes following the big updates in November. (11-07..11-17)

## [v0.9.0] - 2025-11-07
### Added
- Partial loading mode: allows handling datasets larger than 65,535 samples while enabling model building even when not all data can fit in RAM; this mode disables training API and cross validation (falls back to OOB score). (11-06)
- Improved dynamic node layout and adaptors in `Rf_node_predictor` to scale to large datasets; significant speedups and safety fallbacks. (11-07)
- Added `run_walk` and `mnist` sample datasets and better handling in hog_processor for large row counts. (11-07)

### Changed
- Manual node estimate tuning, partial mode speedups (~4x slower than normal but acceptable). (11-07)

## [v0.8.0] - 2025-10-25
### Added
- PSRAM support enabled across all containers; PSRAM examples moved. (10-18, 10-25)
- MicroSD card support with SDIO-4bit mode; example for microSD usage added. (10-25)

### Changed
- Standardized macros and synchronized examples and data transfer tools for SD usage. (10-25)

## [v0.7.0] - 2025-10-10
### Added
- `TrainingContext` with support for training context management and CLI `num_features` argument. (10-10..10-12)
- `set_unsafe()` to speed up `loadData()` and fallbacks for IO buffer failures. (10-04)

### Changed
- Reduced stack usage to avoid stack overflow; improved inference speed and stack safety. (10-10)

### Fixed
- Buffer size issues for received filenames, fixed. (10-12)

## [v0.6.0] - 2025-09-18
### Added
- Full quantization pipeline and quantization visualizer. (09-18)
- `loadChunk()` to improve forest support for large number of features (1023). (09-04)
- `memory_usage()` helper added to all classes under `mcu` to track RAM usage in MCU environment. (09-03)

### Changed
- Converted tree representation from recursive structure to space-efficient array form (BIG UPGRADE). This reduced tree_node footprint to 4 bytes in many cases and converted construction to BFS-style. (08-15)
- Convert dataset/model unified form and sync with transfer formats. (09-18)

### Fixed
- Critical fixes in `ChainedUnorderedMap` constructor and other container constructors. (09-04)

## [v0.5.0] - 2025-08-02
### Added
- `pre_train` tools v1.0 and v1.1 to support pre-training flow and converting random forest artifacts. (08-02, 08-15)
- Data preprocessing and binary dataset tools for PC and ESP32 to streamline dataset generation and transfer. (07..08)

### Changed
- Restructure of data processing and transfer tool; clean-up and new transfer utilities. (07-05..07-16)

## [v0.4.0] - 2025-07-16
### Added
- `hog_transform` for preprocessing image datasets used by HOG. (07-16)
- Tools for data transfer and capture, including a unified transfer toolkit (3-in-1). (07-05..07-16)

### Fixed
- Improvements in vector `sort()` and `b_vector` bounds checking. (07-03..07-06)

## [v0.3.0] - 2025-06-27
### Added
- `Rf_categorizer` and `categorizer` support; Rf transfer and categorization via Serial/pyserial. (06-27..06-28)
- File manager added to the repository, with helper tools for initial transfer. (06-27)

## [v0.2.0] - 2025-06-24
### Added
- containers: ChainedUnorderedMap, improvements to `b_vector`, `vector`, and `hash_kernel` string handling for Arduino. (06-24..06-25)
- `file_manager` and initial data transfer helpers. (06-27)

### Fixed
- Cleanups and removed old `unordered_map.h`. (06-23..06-27)

## [v0.1.0] - 2025-06-23
### Added
- Initial commit and early project scaffolding for the MCU random forest library; base containers and core APIs for random forest on microcontrollers. (Initial commit)

---
Notes:
- This changelog groups small, scattered commits that are close in time. If you'd like a stricter one-commit-per-version mapping or additional categories, tell me the desired granularity.

