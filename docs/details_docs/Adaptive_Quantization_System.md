# Adaptive Quantization for Sliding-Window Edge Learning

## 1. Introduction

In streaming edge intelligence, data is typically managed within a fixed-size sliding window (FIFO) to accommodate the finite memory of microcontrollers. As this storage window constantly moves forward with incoming batches of data, the statistical properties of the resident samples evolve. A static quantization layer, while efficient for initial data, quickly becomes a bottleneck: it either fails to represent new values appearing at the window's leading edge or wastes resolution on ranges that have long since exited the window's trailing edge.

This chapter details our **Adaptive Quantization System**, designed to synchronize the quantization envelope with the active storage window. By dynamically expanding and contracting the feature ranges ($min_f, max_f$) as the window slides, the system ensures that the limited bit-depth (2-4 bits) is always concentrated on the current data distribution. This "window-following" behavior is implemented through a decoupled feature architecture (`QTZ4`) and a lazy re-quantization strategy that minimizes computational overhead on the ESP32 platform.

## 2. Window-Synchronized Feature Architecture

To enable the quantizer to follow the moving storage window, we employ an independent feature architecture. Each feature $f$ maintains its own quantization rule $Q_f$, defined by a dynamic range $[min_f, max_f]$ that is strictly bound to the samples currently residing in the FIFO buffer.

This decoupling is critical for temporal adaptation. As new data batches enter the window, only the features exhibiting shifts in their local distribution require range adjustments. This allows the system to perform targeted updates with $O(1)$ complexity relative to stable features, preserving the real-time constraints of the edge device.

## 3. Forward Adaptation: Drift-Driven Expansion

As the storage window moves into new data territory, it may encounter feature values that exceed the current quantization envelope. We treat these occurrences as "forward drift" events that necessitate an immediate expansion of the quantizer's range.

### 3.1. Out-of-Range Signaling
During inference on new data batches, the quantization layer monitors for values $x_f \notin [min_f, max_f]$. Instead of silent clamping, the system generates an out-of-range signal. These samples are temporarily stored in a *Drift Buffer* with their raw values. This buffer serves as the "look-ahead" mechanism for the next window update, providing the necessary statistics to expand the range without losing the information contained in the new outliers.

### 3.2. Range Expansion and Overlap Mapping
When the window update is triggered, the new range $[min'_f, max'_f]$ is calculated to encompass the new samples at the leading edge. To maintain consistency with the samples already stored in the window (which were quantized using the previous range), we apply an **Overlap-Based Remapping**. 

For each old bin $b_i$, we determine its new index $M(b_i)$ by finding the bin in the expanded range that shares the maximum spatial overlap:
$$ M(b_i) = \underset{b_j}{\mathrm{argmax}} \left( \text{Overlap}(b_i, b_j) \right) $$
This ensures that as the window expands to cover new data, the historical samples within the window are seamlessly re-indexed to match the new coordinate system.

## 4. Backward Adaptation: FIFO-Driven Contraction

The most significant advantage of our system is its ability to reclaim resolution as the window's trailing edge discards older samples. When the FIFO policy evicts samples that previously defined the extremes of a feature's range, the corresponding quantization bins become empty.

### 4.1. Range Reclamation
During the periodic loading of the training dataset from the storage window, the system performs a histogram analysis. If the extreme bins (e.g., the first or last $N$ bins) are found to be unoccupied by any current samples, the range is contracted:
-   **$min'_f$** is moved forward to the boundary of the first populated bin.
-   **$max'_f$** is moved backward to the boundary of the last populated bin.

This contraction is essential for maintaining high precision. By shrinking the range to fit the *current* window, the fixed number of discrete levels ($2^B$) provides a finer-grained representation of the active data, effectively "zooming in" on the distribution as it shifts.

## 5. Lazy Re-quantization for Moving Windows

To handle the constant movement of the storage window without excessive flash memory wear, we utilize a **Lazy Re-quantization Strategy**. 

As the window slides and the quantizer expands or contracts, the system generates a lightweight *Update Filter*. This filter acts as a bridge between the physical storage (quantized with old window parameters) and the logical model (expecting current window parameters).
1.  **Immediate Remapping**: Data currently in RAM for training is remapped in-place using the filter.
2.  **On-the-Fly Loading**: For samples residing in the flash-based storage window, the filter is applied during the stream-load process. 

This ensures that the quantization layer is always a faithful reflection of the data currently within the storage window, allowing the model to adapt to the "moving present" of the data stream with minimal latency and maximum resolution.
