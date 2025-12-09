# Changelog

This changelog documents the sequential, technical evolution of the STL_MCU library and tools.
Entries are grouped: small and close commits are consolidated into date-based releases; large or breaking upgrades are separated as their own releases. Each release lists the high-level changes, followed by technical details and notable commit references (hash prefixes) so you can trace to the exact commit.

------------------------------------------------------------------------------------

## [2025-12-10] - Final update & clean 
- Final update : 
	- rename data_processing -> data_quantization across all docs and code.
	- rename STL_MCU containers name 
	- add buffer in releaseData() process
- Write all documentations , examples and imgs.
- Final cleanup and repo organization.

------------------------------------------------------------------------------------

## [2025-11-27] - finalize transfer tools & rename data_processing tool
- added new tool data_transfer : transfer all needed files to ESP32
- add and normalize command line arguments for all data_transfer tools
- Rename : data_processing  -> data_quantization.


## [2025-11-22] — fix hog transform & optimize model storage
- Fix HOG transform issue - loss of accuracy (at both pc and mcu versions): use artan2() anf sqrt() for angle calculation but slow down the transform (~ x2.5 slower).
- Optimize model storage: dynamically pack Tree_nodes instead of fixed 4(8) bytes.
- Upgrade Tree_node: expand to 64-bit node layout to support large datasets.
- UPgrade packed_vector : support max 32-bit -> unlimited-bit per element.

## [2025-11-20] — Speed up tree building process & handle large datasets

- Speed up tree buliding process: Reworked node statistics and feature selection: replaced per-node unordered_set_s with compact label-count arrays + a purity flag and added a deterministic vector-based feature sampler to eliminate costly allocations. now treee bulding 40-70% faster.
- Critical : fix node_layput calculation issue (caused of drop large model accuracy before)
- Solved : failed problem on  large dataset (~1.000.000 samples)

## [v1.4.0] 2025-11-17 — Feature additions & API surface improvements (grouped)

- New tool: data_transfer — transfer all files to ESP32 (improvements to transfer tooling and workflow).
- On-device training demo: perform online inference and allow model re-training in realtime on-device.
- Container extensions: add `resize()` overload, `emplace()` and `emplace_back()` to `vector` and `b_vector`.
- Random forest API: add `get_all_original_labels()`, `best_training_score()` and other helper APIs to `random_forest_mcu`.
- New example dataset: `finger` and other dataset additions.
		- notable commit: 232fdaa

## [v1.3.1] 2025-11-14 — Maintenance and tooling (grouped)

- File manager & SD card: improvements and API cleanup; removed obsolete macros that interfered with compilation flow.
- Add tests: `full_pipeline` test and `streaming_inference` test to verify end-to-end behavior.
- New tool: `data_collector` for live data capture on ESP32-CAM.
		- notable commit: 6f9adde

## [v1.3.0] 2025-11-12 — HOG transform completion & workspace sync

- Complete HOG transform (ESP32 & PC versions) and sync the transform utilities across the workspace.
- Fix issues with SD Card usage in Arduino IDE pre-compilation stage and add `Rf_board_config.h` for board-specific macros.
- Harmonize macros across workspace for robust compilation.
		- notable commit: e02a660

## [v1.2.1] 2025-11-10 — Transfer improvements & minor fixes

- Update transfer tooling to handle SD card macros and cleanup after big update.
- Add `max_samples` configuration and `set_max_samples()` API to limit dataset size during capture/training.
- Upgrade `ID_vector` from 8-bit to 32-bit; new API `set_bit_per_values()` introduced.
		- notable commits: 990a427, 19d3b71

## [v1.2.0] 2025-11-07 — BIG UPGRADE: dynamic layout & model improvements

- New config parameter `min_leaf` and associated behavior; improves model quality by ensuring leaf node size constraints.
- Convert pre-trained models to a dynamic-layout form so they can be loaded directly on microcontrollers.
- Upgrade `Rf_node_predictor` to adapt when dataset size increases.
- `hog_processor` improved to handle datasets > 65535 samples/rows.
- Added sample datasets `run_walk` and `mnist` for validation.
		- notable commit: 2e1f9fde

## [v1.1.0] 2025-11-06 — BIG UPGRADE: partial loading mode (very large dataset support)

- Add partial loading mode which allows handling datasets larger than available RAM (dataset > 65535 samples).
	- Partial loading acts as a fallback: training and cross-validation that require full in-memory data are disabled; OOB scoring used instead.
	- This mode is significantly slower but enables model building on devices without large RAM.
		- notable commit: ca3db416

## [v1.0.1] 2025-11-01 — Node layout dynamic completion & fixes

- Finish dynamic node layout work, multiple fixes for inference warm-up and traversal loops.
- Added aliases `sample_type` and `label_type`; sync and cleanup for `RF_LABEL_ERROR` and struct alignment optimizations.
		- notable commits: 5407411, a51adf3

## [v1.0.0] 2025-10-27 — BIG UPGRADE: dynamic tree_node layout (space optimization)

- Dynamic tree_node layout: each node layout varies per model and uses minimal bits required.
	- Result: model RAM footprint reduced by ~30–50% for small-feature/label datasets while filesystem model remains unchanged.
	- Allows exceeding previous parameter limits (MAX_FEATURES, MAX_NODES) to support larger datasets and larger trees.
	- Add `RandomForest::warmup_prediction()` to pre-initialize caches and reduce first-inference jitter.
		- notable commit: 29833d95

## [v0.8.0] 2025-10-25 to 2025-10-27 — Performance & container upgrades

- Packed and performance containers updated: `packed_vector`, `packedArray`, `pair` optimizations with attributes for aggressive inlining and better compiler assumptions.
- Allow custom structures up to 4 bytes in `packed_vector`. Add examples and docs for `packed_vector` and `packedArray`.
- Replace `std::vector` usage with `mcu::vector` in key internal components to reduce heap overhead and increase determinism.
- Add `assign()` API to `vector` and moved PSRAM examples.
		- notable commits: d590dd1e, 3a72170e, e6c2f9d

## [v0.8.0] 2025-10-25 — microSD support and examples

- Add microSD support (SDIO 4-bit) and example showing microSD usage.
		- notable commits: c8385673, cfc03c93

## [v0.7.0] 2025-10-18 — BIG_UPGRADE: PSRAM support across containers

- Enable PSRAM allocation support for: Stack, Queue, DeQueue, vector, b_vector, unordered_map_s, unordered_set_s, unordered_map, unordered_set, PackedArray, packed_vector, and ID_vector.
	- Automatic PSRAM detection and graceful fallback to DRAM.
	- `uses_psram()` helper to introspect containers.
	- Backwards compatible behind `RF_USE_PSRAM` macro.
	- Added tests and examples for PSRAM verification.
		- notable commit: a94bb1ab

## [v0.6.0] 2025-10-05 — Filesystem change: SPIFFS -> LittleFS

- Migration from SPIFFS to LittleFS across the project to gain better durability and maintainability on ESP32 filesystem usage.
	- Multiple commits on the same day reflect an iterative migration and compatibility fixes.
		- notable commits: 6861f8c7, fec72ea8, 520101a5

## [v0.5.1] 2025-10-07 — Inference speedup and algorithm adjustments

- Major inference optimization: inference latency reduced from ~3ms to ~0.7ms via low-level optimizations and inlining.
- Remove `unity_threshold` from algorithm and related cleanup.
		- notable commits: ed4e6cb4, 0a410489

## [v0.5.0] 2025-10-06 — File manager and utility fixes

- Enhance `Rf_file_manager` to be robust with different storage backends and error conditions.
		- notable commit: 04f64bf4

## [v0.4.1] 2025-09-04 — Quantization & container updates; unordered_map addition

- Add `loadChunk()` method and upgrade forest ability to handle datasets with large feature counts (e.g., up to 1023 features).
- Add quantization visualizer and rename Categorizer -> Quantizer to reflect new workflow.
- Add `unordered_map` and fix constructors for correctness.
		- notable commits: ebceb7ad, c55571ea, be266ffa

## [v0.4.0] 2025-09-03 — Memory & diagnostics improvements

- Add `memory_usage()` to all `mcu` classes for runtime introspection.
- Deploy k-fold cross validation support and related fixes.
		- notable commits: 201ba242, 5bd4685

## [v0.3.0] 2025-08-30 — BIG UPGRADE: Rf_data container change (memory reduction)

- `Rf_data` container replaced vector<Rf_sample> with raw CHUNK data representation.
	- Resulted in ~40% HEAP savings for large-feature datasets and ~120% savings for small-feature datasets.
	- This is a memory-focused change enabling larger datasets on constrained devices.
		- notable commit: 343f8690

## [v0.2.0] 2025-08-15 — BIG UPGRADE: tree conversion, initializer upgrades, categorizer addition

- Convert tree structure from recursive to array form; tree_node size reduced to 4 bytes and BFS-based construction applied.
	- Faster traversal, reduced stack/recursion requirements, and smaller memory footprint for tree nodes.
- Upgrade `initializer_list` support for `vector` and add `Rf_categorizer`.
		- notable commits: 7ad94cb7, b892685e

## [v0.1.1] 2025-07 — 2025-08 — Tooling, transfer utilities, pre_train & processing pipelines

- Add and refine `data_transfer`, `data_quantization`, `pre_train` and `processing_data` workflows and tools for dataset conversion, categorization, and binary dataset generation.
- Add many helper tools for ESP32-side transfer and PC-side processing (binary convert tool, categorizer, dataset_params).
- Add `pre_train` tools and early versions of `processing_data` integration.
		- notable commits across this period: 4b7705e, c7548c6e, f757004e

## [v0.1.0] 2025-06-23 — Core container and baseline features (initial release)

- Add core containers and components: `vector`, `b_vector`, `ID_vector`, `packed_vector`, `packedArray`, `pair`, `hash_kernel`, `unordered_map_s` and `unordered_set_s` variants.
- Add `Rf_components`, `Rf_file_manager`, `random_forest_mcu` initial integration and `hog_transform` toolkit.
- Add initial examples and early ESP32 transfer tools.
- Initial commit and repository scaffolding.
		- initial commit: 65db40ac (2025-06-23)

------------------------------------------------------------

- Small / close commits: for many date ranges (for example 2025-10-25..2025-10-27 and 2025-11-10..2025-11-17) a series of incremental commits were consolidated into a single release entry to reduce noise while preserving important technical highlights.
- Major upgrades: commits whose message contained "BIG UPGRADE", "BIG UPDATE", or other clear markers were treated as major releases and documented separately with explicit technical changes.
- Tracing: where possible I referenced a representative commit short-hash (7 chars) to make it easy to locate the specific change in git.

How to extend/change this layout
- For semantic versioning, we can map each date-based section to an explicit version number when you decide a versioning scheme (e.g. v1.0.0, v1.1.0). If you prefer, I can propose a semver mapping based on the breaking-changes and feature additions.

If you'd like, I can:
- Split any grouped release into finer-grained entries (per-commit) if you need a full exhaustive single-commit changelog.
- Add a `BREAKING CHANGES` section where applicable and map to semver.
- Generate release tags and a GitHub Releases-compatible `CHANGELOG.md` layout.

-- end

BREAKING CHANGES
----------------

The following releases include changes that may break backward compatibility or require migration steps. Each entry mentions the affected area, the migration implication, and a reference commit.

- v0.2.0 (2025-08-15) — Tree conversion (recursive -> array form)
	- Change: Tree nodes were converted from a recursive structure to an array-based representation (BFS construction). Each tree node became 4 bytes.
	- Impact: Any external tooling or saved model format that expected the old recursive structure should be migrated. Pre-existing model builders and any serialization/parsing code should be updated to the new array format.
	- Commit reference: 7ad94cb

- v0.3.0 (2025-08-30) — Rf_data CHUNK representation
	- Change: The `Rf_data` container changed from storing `vector<Rf_sample>` to raw CHUNK data representation to save heap memory.
	- Impact: Code that depended on `Rf_sample` vector semantics (direct indexing, iterators, or size-of assumptions) must be adapted to the new chunked API or helper methods added for compatibility.
	- Commit reference: 343f869

- v0.6.0 (2025-10-05) — Filesystem migration SPIFFS -> LittleFS
	- Change: The project migrated from SPIFFS to LittleFS for on-device storage.
	- Impact: Path handling, filesystem initialization and available APIs may differ. If your deployment or tooling expects SPIFFS behavior, update your flash partitioning and mounting code. Also check any saved data formats for compatibility.
	- Commit references: 6861f8c7, fec72ea8, 520101a5

- v1.0.0 (2025-10-27) — Dynamic tree_node layout
	- Change: The tree_node layout became dynamic per-model, with node-level bit-packing to minimize RAM. This is a fundamental change to in-memory model representation and inference internals.
	- Impact: Model files on disk remain compatible with the previous versions' on-disk format in many cases, but in-memory loading and runtime assumptions (node size, packing) changed. If you maintain binary model loaders or custom inference wrappers, validate and update them. Unit tests that relied on specific struct sizes or memory layouts must be updated as well.
	- Commit reference: 29833d95

-- end


