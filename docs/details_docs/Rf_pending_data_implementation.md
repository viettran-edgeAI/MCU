# Rf_pending_data Class Implementation

## Overview
The `Rf_pending_data` class is designed to handle the collection and processing of prediction samples for potential retraining of the Random Forest model on ESP32 microcontrollers. It manages a buffer of samples that are waiting for actual labels to be provided, and then processes them for dataset extension and inference logging.

## Key Features

### 1. **Automatic Buffer Management**
- Stores predicted samples in a buffer along with their predicted labels
- Automatically flushes when buffer reaches `max_pending_samples` (default: 100)
- Tracks actual labels provided by users/sensors with timeout handling

### 2. **Dataset Extension (`write_to_base_data`)**
- **Extend Mode**: Adds new labeled samples to the end of the base dataset
- **Replace Mode**: Removes old samples from the beginning and adds new ones (maintains dataset size)
- Only processes samples with valid actual labels (not 255)
- Updates `config.num_samples` accordingly

### 3. **Inference Logging (`write_to_infer_log`)**
- Logs prediction accuracy in a compact binary format
- Header structure:
  ```
  Magic Number (4 bytes): 0x494E4652 ("INFR")
  Number of Labels (1 byte): num_labels
  Prediction Counts (2 bytes × num_labels): Total predictions per label
  ```
- Data structure: Packed bits (8 predictions per byte, 1=correct, 0=incorrect)
- Automatic file trimming when exceeding `MAX_INFER_LOGFILE_SIZE` (2KB)

## Class Interface

### Core Methods

```cpp
// Add a sample with predicted label to buffer
void add_pending_sample(const Rf_sample& sample, 
                       Rf_data* base_data_ptr = nullptr, 
                       Rf_config* config_ptr = nullptr, 
                       const char* infer_log_file = nullptr);

// Add actual label from user/sensor feedback
void add_actual_label(uint8_t true_label);

// Process all pending samples and clear buffers
void flush_pending_data(Rf_data& base_data, 
                       Rf_config& config, 
                       const char* infer_log_file, 
                       bool extend_base_data = true);
```

### Utility Methods

```cpp
uint16_t get_pending_count() const;     // Number of samples in buffer
uint16_t get_labeled_count() const;     // Number of labels received
bool is_buffer_full() const;            // Check if auto-flush will trigger
```

### Configuration

```cpp
uint16_t max_pending_samples = 100;     // Buffer size before auto-flush
unsigned long max_wait_time;            // Timeout for label responses (default: ~24 days)
```

## Integration with RandomForest Class

The `RandomForest` class now includes:

```cpp
// Automatic sample collection during prediction
String predict(const float* features, size_t length);

// Manual label addition
void add_actual_label(const String& label);
template<typename T> void add_actual_label(const T& label);

// Manual buffer management
void flush_pending_data(bool extend_base_data = true);
uint16_t get_pending_samples_count() const;
uint16_t get_labeled_samples_count() const;
bool is_pending_buffer_full() const;
```

## Usage Workflow

### 1. **Normal Prediction with Data Collection**
```cpp
RandomForest forest("model_name");

// Make predictions (automatically adds to pending buffer)
float features[] = {1.2, 0.8, 2.1, 3.0, 0.5};
String prediction = forest.predict(features, 5);

// Provide actual label when available
forest.add_actual_label("correct_class");
```

### 2. **Manual Buffer Management**
```cpp
// Check buffer status
if(forest.is_pending_buffer_full()) {
    Serial.println("Buffer full, flushing...");
    forest.flush_pending_data(true); // Extend dataset
}

// Get statistics
Serial.printf("Pending: %d, Labeled: %d\n", 
              forest.get_pending_samples_count(),
              forest.get_labeled_samples_count());
```

### 3. **Automatic Processing**
The system automatically:
- Collects samples during `predict()` calls
- Flushes when buffer is full
- Handles label timeouts
- Maintains inference logs
- Trims log files when too large

## Memory Efficiency

### Design Considerations
- Uses `mcu::b_vector` for memory-efficient storage
- Packed prediction bits (8 per byte) in log files
- Chunked processing to avoid large memory allocations
- Automatic cleanup and SPIFFS file management

### Memory Usage
- Approximately 1KB RAM per 100 pending samples (depending on feature count)
- Log files limited to 2KB with automatic trimming
- Base dataset extensions saved directly to SPIFFS

## Error Handling

- Invalid labels (255) are ignored during processing
- File I/O errors are logged but don't crash the system
- Memory allocation failures trigger graceful degradation
- Buffer overflow protection with automatic flushing

## Performance Notes

- O(1) sample addition to buffer
- O(n) processing during flush operations
- Minimal impact on prediction latency
- Efficient binary file formats for fast I/O

## Future Enhancements

1. **Adaptive Buffer Sizing**: Adjust buffer size based on available memory
2. **Label Confidence**: Store prediction confidence scores
3. **Streaming Processing**: Process samples immediately for very low memory systems
4. **Compression**: Compress older log entries
5. **Network Sync**: Synchronize data with external servers
