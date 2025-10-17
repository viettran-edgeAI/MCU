# Quantization Documentation Index

This directory contains comprehensive documentation about the variable quantization system used in the ESP32/PC random forest implementation.

## Documents

### 1. **Rf_quantizer_Technical_Overview.md**
   Comprehensive guide to the quantizer system covering:
   - **PC-side data processing**: Dataset analysis, feature classification, quantile bin generation
   - **Embedded deployment**: Memory optimization strategies, pattern compression
   - **ESP32 optimizations**: Adaptive storage, CTG2 binary format
   - **Variable quantization**: Support for 1-8 bits per feature
   - **Real-time processing**: Sub-millisecond single-sample categorization

### 2. **inference_speedup_technical.md**
   Deep dive into prediction performance optimization including:
   - **Compiler-assisted optimization**: Aggressive inlining, branch prediction
   - **Hot path engineering**: Function call elimination, bit manipulation
   - **Cache optimization**: Memory access patterns, sequential layout
   - **Performance results**: 4.3× speedup achieved (3.0ms → 0.7ms)
   - **Real-world case studies**: Vibration anomaly detection, industrial monitoring
   - **Future opportunities**: SIMD vectorization, batch processing, compile-time specialization

### 3. **1bit_quantization_optimization.md** (in parent docs folder)
   Specific optimization for 1-bit quantized data:
   - Fast path implementation for binary features
   - Single threshold handling (no search needed)
   - ~8× speedup for tree building with 1-bit data
   - Use cases and testing guidelines

## Key Concepts

### Variable Quantization Levels (1-8 bits per feature)

| Bits | Values | Use Case | Memory vs Default |
|------|--------|----------|-------------------|
| **1** | 2 | Binary/categorical features | -50% |
| **2** | 4 | Default balanced setting | Baseline |
| **3** | 8 | Medium precision | +50% |
| **4** | 16 | High precision | +100% |
| **8** | 256 | Near-continuous | +300% |

### System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Development Workflow                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  PC Analysis → Quantizer Gen → CSV Export → ESP32 Deploy │
│      │              │              │            │          │
│      ▼              ▼              ▼            ▼          │
│  • Statistics   • Binning      • Serialization • Loading  │
│  • Feature      • Compression  • Optimization  • Runtime  │
│    Types       • Patterns      • Validation    • Inference│
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Memory Efficiency

**CTG2 Format Impact (144 features, 2-bit default):**
- Original: 18.6 KB
- Compressed: 5.6 KB  
- **Reduction: 70%**

With variable quantization:
- 1-bit: 2.8 KB (85% reduction)
- 3-bit: 8.4 KB (55% reduction)

### Performance Characteristics

**Inference Latency (144 features, 10 trees):**
- Before optimization: 3.0ms
- After optimization: 0.7ms
- **Speedup: 4.3×**
- **Throughput: 1,430 predictions/second**

## Quick Start Guide

### For PC Training

1. Prepare your dataset as CSV
2. Run PC training with configurable quantization:
   ```cpp
   config.quantization_coefficient = 2;  // 1-8 bits
   config.init("dataset.csv");
   ```
3. Export quantizer with optimized quantization
4. Transfer to ESP32 via serial

### For ESP32 Deployment

1. Load quantizer from SPIFFS
2. Quantize sensor input:
   ```cpp
   Rf_sample sample = quantizer.quantizeFeatures(raw_sensor_data);
   ```
3. Run prediction:
   ```cpp
   uint8_t prediction = forest.predict_features(sample.features);
   ```
4. Decision latency: <1ms for real-time responsiveness

## Advanced Topics

### Feature Selection Strategy

- **High importance features**: Use 3-4 bits for precision
- **Medium importance**: Use 2 bits (default)
- **Low importance**: Use 1 bit (minimum memory)
- **Discrete/categorical**: Use 1-2 bits

### Memory Footprint Control

Choose quantization level based on target device:
- **ESP32 (320KB RAM)**: 3-4 bits per feature acceptable
- **ESP8266 (80KB RAM)**: Default 2 bits per feature
- **Extreme constraint**: 1 bit per feature

### Accuracy Considerations

Variable quantization maintains accuracy through:
- Optimal threshold selection (PC-side analysis)
- Feature importance weighting
- Adaptive bin edge placement
- Cross-validation testing

## Optimization Paths

### For Speed
- Use 1-bit quantization when possible
- Enable aggressive compiler optimizations (-O3)
- Deploy to CPU with FPU support

### For Accuracy
- Use 3-4 bits for high-variance features
- Use 2 bits for balanced features
- Validate with cross-validation

### For Memory
- Use 1-bit quantization (50% savings)
- Enable CTG2 pattern compression (70% savings)
- Combine both (85% total reduction possible)

## Related Files

- **Upgrade Implementation**: See `UPGRADE_SUMMARY.md` for variable quantization upgrade details
- **Tree Building**: See `src/random_forest_mcu.h` for 1-bit tree building optimization
- **PC Training**: See `tools/pre_train/random_forest_pc.cpp` for PC training with variable quantization
- **Examples**: See `examples/` for working examples

## Performance Benchmarks

### Memory Usage (144 features)
- v1.0 (fixed float): 18.6 KB
- v1.1 (CTG2): 5.6 KB (70% savings)
- v1.1 + 1-bit: 2.8 KB (85% savings)

### Inference Speed
- Prediction latency: 0.7ms (optimized)
- Throughput: 1,430 predictions/second
- CPU utilization: 23% (77% headroom)

### Application Support
- ✅ Real-time sensor processing (>1kHz)
- ✅ Video frame analysis (30 FPS)
- ✅ Audio classification (<2ms latency)
- ✅ Vibration monitoring (1kHz+)

## Version History

- **v1.0** (Original): Fixed 2-bit quantization
- **v1.1** (CTG2): 70% memory reduction via compression
- **v1.2** (Variable): 1-8 bit support, 1-bit optimization, 4.3× speedup

## Contact & Support

For questions about quantization strategy and implementation, refer to the detailed technical documents listed above.
