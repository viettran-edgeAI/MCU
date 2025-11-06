# Partial Loading Mode Technical Documentation

## Overview

Partial loading mode enables training Random Forest models on microcontrollers with datasets larger than available RAM by loading training data in chunks from storage during tree building, rather than loading the entire dataset into memory.

## Architecture

### Standard Mode vs Partial Loading Mode

**Standard Mode:**
```
┌─────────────┐
│  Storage    │
│  train_data │
└──────┬──────┘
       │ Load entire dataset
       ▼
┌─────────────┐
│     RAM     │
│  train_data │  ← All samples in memory
└──────┬──────┘
       │
       ▼
  Tree Building
```

**Partial Loading Mode:**
```
┌─────────────────────────────────┐
│  Storage: train_data            │
│  Chunk 0 | Chunk 1 | ... | N   │
└──────┬──────────────────────────┘
       │ Load chunks on demand
       ▼
┌─────────────────────────────────┐
│  RAM: TrainChunkAccessor        │
│  LRU Cache (4 chunks)           │
│  ┌───────┬───────┬───────┬───┐ │
│  │Chunk 0│Chunk 1│Chunk 3│...│ │
│  └───────┴───────┴───────┴───┘ │
└──────┬──────────────────────────┘
       │
       ▼
  Tree Building
```

## Key Components

### 1. TrainChunkAccessor

**Location**: `Rf_components.h`

The chunk accessor manages on-demand loading of training data chunks with an LRU cache.

**Features:**
- **Chunked file access**: Reads fixed-size chunks from storage (e.g., 100-200 samples per chunk)
- **LRU caching**: Maintains 4 most recently used chunks in RAM, automatically evicting least recently used
- **Batch operations**: Extracts labels/features for multiple samples in one operation, reducing overhead
- **Cache statistics**: Tracks hit/miss rates for performance monitoring

**Access Patterns:**

The accessor provides two modes of data access:

1. **Individual Access** (slower): Request one sample's label or feature at a time. Each call performs chunk lookup, potential chunk loading, and data extraction.

2. **Batch Access** (optimized): Request labels or a specific feature for all samples in a range. The accessor:
   - Pre-calculates bit extraction parameters once
   - Loads chunks sequentially as needed
   - Amortizes chunk lookup overhead across all samples
   - Returns extracted data in output arrays for fast subsequent access

**Memory Usage:**
- Cache size: 4 chunks × (samples_per_chunk × (1 byte label + packed_feature_bytes))
- Typical: 4 × (100-200 samples × 5-20 bytes) ≈ 2-16 KB

### 2. Chunk Organization

Training data is stored in binary format:

```
File Structure:
┌──────────────────────────────────────────┐
│ Header (6 bytes)                         │
│  - num_samples (4 bytes)                 │
│  - num_features (2 bytes)                │
├──────────────────────────────────────────┤
│ Chunk 0:                                 │
│  Sample 0: [label][packed features]      │
│  Sample 1: [label][packed features]      │
│  ...                                     │
│  Sample N-1: [label][packed features]    │
├──────────────────────────────────────────┤
│ Chunk 1:                                 │
│  ...                                     │
└──────────────────────────────────────────┘

Record Format (per sample):
┌───────┬──────────────────────────────┐
│ Label │ Packed Features              │
│1 byte │ (num_features × quant_bits   │
│       │  / 8) bytes                  │
└───────┴──────────────────────────────┘
```

**Chunk Size Calculation:**

The system calculates optimal chunk size based on:
- Available RAM (typically aims for cache to use ~10% of free heap)
- Feature packing density (bits per sample)
- Number of cache slots (4 chunks)

Formula concept: Each sample requires 1 byte for label plus packed feature bytes (total features × quantization bits / 8). The chunk size is chosen so that 4 chunks fit comfortably in available memory while being large enough to amortize loading overhead.

### 3. Batch Processing Optimization

The key to performance in partial loading mode is **batch processing** - extracting data for multiple samples in one operation rather than individual calls.

**Concept Comparison:**

```
Individual access flow               Batch access flow
┌────────────┐                       ┌────────────┐
│ Sample IDs │   label() / feature() │ Sample IDs │
└─────┬──────┘ ───────┬────────────▶ └─────┬──────┘
  │               │                     │
  ▼               ▼                     ▼
Multiple chunk   Multiple chunk       Few batch requests
lookups (thrash) lookups (thrash)     (cached sequential)
```

**Individual Processing (Slow):**

When evaluating a split with 100 samples and 5 features, the naive approach calls the accessor 500+ times:
- For each feature, loop through all samples
- Each sample: request its label (chunk lookup + extraction)
- Each sample: request feature value (chunk lookup + bit unpacking)
- Total: 100 samples × 5 features × 2 calls = 1000+ accessor operations

This causes repeated chunk lookups for the same samples, cache thrashing, and massive overhead from function calls and repeated chunk validation.

**Batch Processing (Fast):**

The optimized approach extracts data once and reuses it:
- Extract all labels for the node's samples in one batch operation (~4 chunk loads for typical partition)
- For each feature being evaluated:
  - Extract that feature's values for all samples in one batch operation (~4 chunk loads)
  - Use the pre-extracted label and feature arrays (simple array indexing, no accessor calls)
  
Total: ~24 chunk loads (1 label batch + 5 feature batches × ~4 chunks each) vs 1000+ individual operations

The batch operation amortizes the chunk lookup and bit unpacking overhead across all samples, transforming the access pattern from random (thrashing) to sequential (cache-friendly).

### 4. Index Sorting by Chunk

To maximize cache hits, sample indices are sorted by chunk before batch operations.

**Purpose:**
- Groups samples from the same chunk together in the indices array
- Ensures batch operations access chunks sequentially rather than randomly
- Reduces cache thrashing by maintaining spatial locality

**The Algorithm:**

The sorting function uses a lightweight counting sort that's stable within each chunk:
1. Scan through the sample IDs to determine which chunks are present and count samples per chunk
2. Calculate starting positions for each chunk in the output
3. Redistribute samples so all samples from chunk 0 come first, then chunk 1, etc.
4. Within each chunk group, samples maintain their original relative order (stable sort)

**Applied At Key Points:**

The sorting is performed:
- **At tree root**: Before starting breadth-first building, sort all samples by chunk. This ensures the first batch operations benefit from sequential access.
- **After each split**: When partitioning samples into left/right children, each partition is re-sorted by chunk. This maintains chunk locality as the tree grows deeper.

**Impact on Access Pattern:**
```
Before sorting (random access):
Samples: [523, 45, 892, 234, 567, ...]
Chunks:  [ 5,   0,   8,   2,   5, ...]  ← Random
Cache: Miss, Miss, Miss, Miss, Hit, ...

After sorting (sequential access):
Samples: [45, 234, 523, 567, 892, ...]
Chunks:  [ 0,   2,   5,   5,   8, ...]  ← Sequential
Cache: Miss, Miss, Miss, Hit, Miss, ...
```

## Performance Characteristics

### Time Complexity

| Operation | Standard Mode | Partial Loading |
|-----------|---------------|-----------------|
| Dataset load | O(N) once | O(1) |
| Feature access (individual) | O(1) | O(1) + chunk load |
| Feature access (batch of M) | O(M) | O(M/chunk_size) amortized |
| Tree building | O(N×F×log(N)) | O(N×F×log(N)) + chunk overhead |

### Space Complexity

| Component | Standard Mode | Partial Loading |
|-----------|---------------|-----------------|
| Training data | Full dataset in RAM | 4 chunks in cache |
| Typical RAM usage | N × (1 + features/8) bytes | 4 × samples_per_chunk × (1 + features/8) bytes |
| Example (5000 samples, 100 features, 2-bit quant) | ~70 KB | ~8 KB |

### Cache Performance

**Expected Hit Rates:**
- **Root node**: 70-90% (samples sorted by chunk)
- **Child nodes**: 60-80% (partitioned subsets maintain some chunk locality)
- **Overall**: 70-85% typical

**Factors Affecting Cache Hits:**
- Chunk size vs partition size
- Tree depth (deeper trees = smaller partitions)
- Random feature selection pattern
- Quality of index sorting

## Configuration

### Enabling Partial Loading

**Automatic (recommended):**

Enable auto-configuration to let the system decide when to use partial loading based on dataset size and available RAM. The system automatically switches to partial loading when the dataset size exceeds 50 KB or when the dataset size is large relative to available heap memory.

**Manual:**

You can force enable or disable partial loading mode explicitly using the enable/disable methods on the RandomForest object.

### Chunk Size Tuning

Default chunk size is calculated automatically, but understanding the trade-offs helps:

**Calculation Strategy:**

The optimal chunk size balances two competing factors:
1. **Memory usage**: Larger chunks require more cache memory
2. **Access efficiency**: Larger chunks reduce the number of disk reads

The system calculates chunk size based on:
- Available heap memory (targets ~10% for cache)
- Bytes per sample (1 byte label + packed features)
- Number of cache slots (4)

**Size Impact:**

- **Smaller chunks** (50-100 samples): 
  - Lower memory footprint
  - More frequent chunk loads from storage
  - Faster cache eviction
  
- **Larger chunks** (200-500 samples): 
  - Higher memory requirement
  - Fewer chunk loads
  - Better cache hit rates for large partitions

- **Optimal**: The sweet spot depends on your available RAM and typical partition sizes during tree building. For ESP32 with several hundred KB free, chunks of 150-250 samples work well.

## API Usage

### Basic Workflow

Initialize the RandomForest with your model name, enable partial loading mode (or let auto-config decide), then build the model. 

```
Configuration flow
┌──────────────────────────┐
│ Configure RandomForest   │
│ - Model name             │
│ - Enable partial loading │
│ - Set hyperparameters    │
└──────────────┬───────────┘
               │
               ▼
┌──────────────────────────┐
│ Execute build_model()    │
│ - Single forest build    │
│ - Uses chunk accessor    │
└──────────────┬───────────┘
               │
               ▼
┌──────────────────────────┐
│ Forest ready for         │
│ inference/evaluation     │
└──────────────────────────┘
```

**Important**: The grid search training method (which tests multiple hyperparameter combinations) is disabled in partial loading mode because it would take too long. Instead, use the build_model method which builds a single forest with the current configuration parameters.

Set your desired hyperparameters (min_split, max_depth, etc.) before calling build_model.

### Monitoring Performance

After training completes, the system prints cache statistics showing the number of cache hits and misses, along with the hit rate percentage.

**Interpreting Results:**
- **Hit rate > 70%**: Good performance, sorting is working effectively
- **Hit rate 40-70%**: Acceptable, may benefit from larger chunks or better data distribution
- **Hit rate < 40%**: Poor performance, check chunk size and verify sorting is applied
- **Hit rate 0%**: Critical issue - sorting not applied or cache not functioning

A high hit rate indicates that batch operations are successfully reusing cached chunks, while low rates suggest random access patterns that defeat the cache.

### Memory Monitoring

Monitor heap memory before and after training to verify memory efficiency:

**Expected Results**: Partial loading mode should use approximately 10-20 KB for the chunk cache, versus 50-200+ KB for loading the full dataset in standard mode. The exact savings depend on dataset size and feature dimensionality.

## Implementation Details

### Tree Building Process

The tree building algorithm follows these key steps:

```
Tree construction overview
┌─────────────────────────┐
│ indices (chunk-ordered) │
└────────────┬────────────┘
       │
       ▼
┌─────────────────────────┐
│ queue_nodes (BFS order) │
└────────────┬────────────┘
       │ pop current node
       ▼
┌─────────────────────────┐
│ Batch analyze samples   │
├────────────┬────────────┤
│ if leaf    │ else split │
└────┬───────┴──────┬─────┘
   │              │
   ▼              ▼
 create leaf   partition indices
           │
           ▼
      sort partitions by chunk
           │
           ▼
       push children to queue
```

**1. Index Preparation**

Convert the sample IDs for this tree into a working indices array, then immediately sort these indices by chunk number. This ensures all samples from chunk 0 are grouped together, followed by chunk 1, etc.

**2. Breadth-First Construction**

Process nodes level-by-level using a queue:

**3. Node Analysis (Batch Processing)**

For the current node's sample range:
- Use batch extraction to get all labels at once
- Analyze label distribution to determine if this should be a leaf
- Calculate majority label and label diversity

**4. Stopping Criteria Check**

If the node meets any stopping condition (pure labels, minimum samples, max depth), create a leaf node with the majority label and continue to next node.

**5. Feature Selection and Split Finding (Batch Processing)**

For each randomly selected feature:
- Use batch extraction to get that feature's values for all samples in the node
- Using the pre-extracted labels and features (both in arrays), count value-label combinations
- Evaluate all threshold candidates to find the split that maximizes information gain
- Track the best split across all features

**6. Partitioning (Batch Processing)**

Once the best split is found:
- Use batch extraction to get the split feature's values for all samples
- Partition the indices array into left (≤ threshold) and right (> threshold) groups
- Re-sort both partitions by chunk to maintain cache efficiency for child nodes

**7. Child Node Creation**

Create left and right child nodes, initialize them as temporary leaves, and add them to the processing queue. The actual leaf/split determination happens when those nodes are processed.

**8. Repeat**

Continue processing nodes from the queue until all nodes are either leaves or fully split.

The critical insight is that batch extraction happens only a few times per node (once for labels, once per evaluated feature), rather than hundreds of times with individual access. Combined with chunk sorting, this transforms a cache-hostile random access pattern into a cache-friendly sequential pattern.

### Split Finding Optimization

The split finding algorithm demonstrates the power of batch processing:

**1. One-Time Label Extraction**

At the start of split finding for a node, batch extract all labels for the samples in that node's range. Store these labels in an array. This single batch operation replaces what would have been hundreds of individual label requests (one per sample per feature evaluated).

**2. Calculate Base Impurity**

Using the pre-extracted label array, calculate the node's base impurity (Gini or entropy). This is pure array processing with no accessor calls.

**3. Feature Evaluation Loop**

For each candidate feature:
- **Batch extract**: Get that feature's values for all samples in one operation
- **Count values**: Loop through the cached labels and feature values arrays, building a count matrix of [value × label] combinations
- **Evaluate thresholds**: For each threshold candidate, split the count matrix and calculate information gain

**4. Return Best Split**

After evaluating all features, return the feature and threshold that maximized information gain.

The transformation is dramatic: Instead of calling the accessor N×F times (samples × features) with repeated chunk lookups, we call it F+1 times (one label batch + one feature batch per feature). All the expensive computation operates on fast array data in RAM.

**Data Flow:**
1. Accessor reads chunk from storage → cache
2. Batch extraction unpacks bits → output array
3. Split evaluation reads arrays repeatedly (fast, no storage I/O)

This separation of I/O (batch extraction) from computation (split evaluation) is the key optimization.

```
Split evaluation data flow
┌──────────────┐   sequential reads   ┌──────────────┐
│ Dataset file │ ───────────────────▶ │ Chunk cache  │
└──────────────┘                       └──────┬───────┘
                                             │ batch_extract
                                             ▼
                                   ┌──────────────────────┐
                                   │ Label array          │
                                   │ Feature value array  │
                                   └────────┬─────────────┘
                                            │
                                            ▼
                                   ┌──────────────────────┐
                                   │ Gain calculation     │
                                   │ (pure RAM work)      │
                                   └──────────────────────┘
```
## Limitations

### 1. Grid Search Training Disabled

**Reason**: Grid search requires building multiple forests with different hyperparameters (varying min_split and max_depth across ranges), which is extremely slow in partial loading mode. Each combination could take 10-20 seconds, and testing 10-20 combinations would require several minutes.

**Workaround**: 

**Strategy 1 - Subset Tuning**: Use standard mode to tune hyperparameters on a representative subset of your data (e.g., 20-30% randomly sampled). Once you find good parameters, apply them to the full dataset in partial loading mode.

**Strategy 2 - Manual Testing**: Manually test a few carefully chosen parameter combinations with build_model(), based on domain knowledge or previous experience.

**Strategy 3 - Use Defaults**: Start with the library's default hyperparameters, which work reasonably well for most datasets.

When you attempt to call the training method in partial loading mode, it will return immediately with a warning message suggesting you use build_model instead.

### 2. Slower Than Standard Mode

**Expected Performance:**
- Standard mode: 2-5 seconds for typical model
- Partial loading (optimized): 10-20 seconds for same model
- Speedup vs unoptimized: 6-10×

**Reason**: Chunk loading overhead, even with 70-85% cache hits

**When to Use:**
- Dataset > available RAM
- Memory-constrained devices (ESP32-C3, small PSRAM)
- Large datasets (5000+ samples with many features)

### 3. Requires Binary Dataset Format

Partial loading only works with binary `.bin` format datasets, not CSV or other formats during training.

## Troubleshooting

### Low Cache Hit Rate (< 40%)

**Symptoms:**

Cache statistics show very low hit rate, such as 4-5%, or even 0%.

**Possible Causes:**
1. **Index sorting not working**: The chunk sorting function may not be called or not working correctly
2. **Chunk size too small**: If chunks contain very few samples relative to partition sizes, samples get scattered across many chunks
3. **Very fragmented data**: Sample IDs are distributed such that few samples share chunks even after sorting

**Solutions:**

**For cause 1**: Verify that chunk sorting is being invoked at tree root and after splits. Check debug output for messages indicating sorting activity.

**For cause 2**: Increase the chunk size by modifying the calculation in Rf_data to return a larger value (300-500 samples instead of 100-200). This requires more cache memory but improves hit rates.

**For cause 3**: If your dataset has inherent fragmentation, consider reorganizing it so sample IDs are more sequential, or increasing cache size if you have available RAM.

### Out of Memory During Training

**Symptoms:**

Error messages indicating failed memory allocation for chunk buffers or read operations.

**Causes:**
- Chunk size too large for available memory
- Memory fragmentation preventing large contiguous allocations
- Other components using too much RAM simultaneously

**Solutions:**

**Reduce chunk size**: Modify the chunk size calculation to return smaller values (50-100 samples). This increases chunk loads but reduces peak memory usage.

**Simplify cache**: If desperate for memory, reduce cache size from 4 slots to 2 by modifying the CACHE_SIZE constant in TrainChunkAccessor. This will hurt cache hit rate but may allow training to complete.

**Temporarily disable features**: If using validation, inference logging, or detailed debug output, temporarily disable these to free up memory during training.

### Extremely Slow Training (> 60 seconds)

**Symptoms:**
- Forest building time exceeds 1 minute for a small model (few trees, moderate depth)
- Cache hit rate near 0%

**Solutions:**

1. **Verify batch processing**: Ensure the code is using batch extraction methods (batch_extract_labels, batch_extract_feature) rather than individual accessor calls. Check that you're running the optimized version of the code.

2. **Check chunk sorting**: Verify that sortIndicesByChunk is being applied at the tree root initialization. Without this initial sort, all subsequent operations suffer from cache misses.

3. **Monitor chunk access pattern**: Add debug logging to see how many times chunks are loaded versus accessed. You should see many accesses per chunk load (high reuse).

4. **Increase cache size**: If you have available RAM (several hundred KB free), you can increase CACHE_SIZE from 4 to 6 or 8 to hold more chunks simultaneously. This helps with deeper trees where multiple branches are active.

## Performance Benchmarks

### Typical Results (ESP32-S3, 8MB PSRAM)

**Dataset**: 5000 samples, 100 features, 2-bit quantization, 10 trees

| Mode | RAM Usage | Build Time | Cache Hit Rate |
|------|-----------|------------|----------------|
| Standard | ~70 KB | 2.5 s | N/A |
| Partial (unoptimized) | ~8 KB | 60 s | 0% |
| Partial (optimized) | ~8 KB | 10-15 s | 75-85% |

**Optimization Impact:**
- Memory reduction: 90% (70 KB → 8 KB)
- Time overhead: 4-6× slower than standard mode
- Speedup vs unoptimized partial: 4-6×

### Scalability

| Dataset Size | Standard Mode | Partial Mode | RAM Savings |
|--------------|---------------|--------------|-------------|
| 1000 samples | 15 KB / 0.8s | 8 KB / 3s | 47% |
| 5000 samples | 70 KB / 2.5s | 8 KB / 12s | 89% |
| 10000 samples | 140 KB / 5s | 10 KB / 25s | 93% |
| 20000 samples | ❌ OOM | 12 KB / 55s | N/A |

## Best Practices

### 1. Use Standard Mode When Possible
Partial loading is slower - only use when necessary (dataset > available RAM).

### 2. Tune Chunk Size
Balance between cache size and chunk load frequency based on available RAM.

### 3. Monitor Cache Performance
Check cache hit rates after training to verify optimization is working.

### 4. Disable Grid Search
Use `build_model()` instead of `training()` in partial loading mode.

### 5. Pre-sort Dataset
If possible, pre-sort samples by chunk in the dataset file for better initial cache performance.

### 6. Test on Subset First
Validate model configuration on small dataset in standard mode before training full dataset in partial mode.

## Future Optimizations

### 1. Adaptive Chunk Size
Dynamically adjust chunk size based on partition sizes during tree building.

### 2. Predictive Chunk Pre-loading
Load next likely chunk in background while processing current chunk.

### 3. Multi-Feature Batch Extraction
Extract multiple features simultaneously to reduce chunk access overhead.

### 4. Compressed Chunk Storage
Use run-length encoding or other compression for sparse feature values.

### 5. SIMD Bit Unpacking
Use ESP32 SIMD instructions for faster batch bit unpacking operations.

## Conclusion

Partial loading mode enables training Random Forest models on memory-constrained microcontrollers by trading computation time for memory efficiency. With proper optimization (batch processing, chunk sorting, LRU caching), the performance overhead can be reduced to 4-6× while achieving 90%+ memory savings.

**Key Takeaway**: Use partial loading mode only when necessary, and always verify cache hit rates to ensure optimizations are working effectively.

**Important limitation**: In partial loading mode both the `training()` grid-search helper and k-fold cross validation are disabled. Build single configurations with `build_model()` instead when working in this mode.
