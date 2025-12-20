/*
 * Digit Recognition Example
 * 
 * This example demonstrates handwritten digit classification (0-9) using a Random Forest
 * model trained on extracted features from digit images.
 * 
 * Features:
 * - Trains and evaluates Random Forest model for multi-class classification
 * - Performs predictions on 10 sample feature vectors (144 features each)
 * - Classifies digits from 0 to 9
 * - Calculates accuracy and timing metrics
 * 
 * Hardware: ESP32 or compatible microcontroller with LittleFS support
 * Dataset: Handwritten digits with normalized pixel/feature values
 */

#define DEV_STAGE    
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

#include "random_forest_mcu.h"


const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

using namespace mcu;

void setup() {
    Serial.begin(115200);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)

    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.println("║  Digit Recognition - Random Forest Example         ║");
    Serial.println("╚════════════════════════════════════════════════════╝\n");
    
    delay(1000);

    // Initialize filesystem
    Serial.print("💾 Initializing file system... ");
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ FAILED!");
        Serial.println("⚠️  File system initialization failed. Cannot continue.");
        return;
    }
    
    manage_files();
    delay(500);
    
    long unsigned start_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    
    // Initialize Random Forest model
    const char* model_name = "run_walk";
    RandomForest forest = RandomForest(model_name);
    
    // Optional: Configure forest parameters
    forest.set_num_trees(20);
    // forest.set_random_seed(42);
    // forest.set_training_score(OOB_SCORE);
    // forest.enable_partial_loading();


    // Build and train the model
    if (!forest.build_model()) {
        Serial.println("❌ FAILED");
        return;
    }

    long unsigned build_time = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.printf("Model built in %lu ms\n", build_time - start_forest);
    
    // forest.training(2); // limit to 3 epochs

    // Load trained forest from filesystem
    Serial.print("Loading forest... ");
    if (!forest.loadForest()) {
        Serial.println("❌ FAILED");
        return;
    }

    Serial.printf("bits per node: %d\n", forest.bits_per_node());
    Serial.printf("model size in ram: %d\n", forest.model_size_in_ram());
    

    // check actual prediction time 
    vector<float> sample_1 = MAKE_FLOAT_LIST(1,-0.3347,-0.8296,-0.1801,0.8974,-1.2157,-1.801);
    vector<float> sample_2 = MAKE_FLOAT_LIST(1,-0.2145,-1.1873,-0.2604,1.5379,-0.1754,-1.2683);
    vector<float> sample_3 = MAKE_FLOAT_LIST(1,-0.5766,-0.7973,-0.3985,0.9989,1.846,-0.6562);
    vector<float> sample_4 = MAKE_FLOAT_LIST(1,-0.3997,-1.2274,-0.2403,-1.2353,0.0261,1.0681);
    vector<float> sample_5 = MAKE_FLOAT_LIST(1,-0.1888,-0.8161,-0.0891,-1.4805,0.7852,1.8351);
    vector<float> sample_6 = MAKE_FLOAT_LIST(1,-2.6767,-0.2663,-0.2739,-1.418,1.7281,-1.7277);
    vector<float> sample_7 = MAKE_FLOAT_LIST(1,-0.1495,0.5757,-0.1203,-0.4935,-0.259,-0.6393);
    vector<float> sample_8 = MAKE_FLOAT_LIST(1,-2.461,1.0524,-0.5167,1.8504,1.7529,3.0147);
    vector<float> sample_9 = MAKE_FLOAT_LIST(1,-2.5022,-0.0679,-0.465,2.2849,1.5527,3.5458);
    vector<float> sample_10 = MAKE_FLOAT_LIST(1,-3.2087,-0.3255,-0.6363,-2.7878,-1.6206,-1.7272);
    
    vector<b_vector<float>> samples;
    samples.push_back(sample_1);
    samples.push_back(sample_2);
    samples.push_back(sample_3);
    samples.push_back(sample_4);
    samples.push_back(sample_5);
    samples.push_back(sample_6);
    samples.push_back(sample_7);
    samples.push_back(sample_8);
    samples.push_back(sample_9);
    samples.push_back(sample_10);

    vector<uint8_t> true_labels = MAKE_UINT8_LIST(0,0,0,0,0,1,1,1,1,0);

    // Perform predictions on all samples
    unsigned long total_time = 0;
    uint8_t correct_predictions = 0;

    // Warm-up prediction to cache initialization (optional)
    Serial.print("Warming up inference engine... ");
    forest.warmup_prediction();
    
    Serial.println("\n=== Prediction Results ===");
    Serial.println("Sample | Predicted | Actual | Time (μs) | Match");
    Serial.println("-------|-----------|--------|-----------|------");
    
    rf_predict_result_t result;
    for (int i = 0; i < samples.size(); i++){
        forest.predict(samples[i], result); 
        if (!result.success) {
            Serial.printf("  %2d   | FAILED    |        |           | ✗\n", i+1);
            continue;
        }
        
        total_time += result.prediction_time;
        forest.add_actual_label(true_labels[i]);
        
        bool is_correct = (atoi(result.label) == true_labels[i]);
        const char* match = is_correct ? "✓" : "✗";
        if (is_correct) correct_predictions++;
        
        Serial.printf("  %2d   | %-9s | %6d | %9lu | %s\n", 
                     i+1, result.label, true_labels[i], 
                     result.prediction_time, match);
    }
    
    Serial.println("-----------------------------------------------");
    Serial.printf("Total samples: %d\n", samples.size());
    Serial.printf("Correct predictions: %d/%d (%.1f%%)\n", 
                  correct_predictions, samples.size(), 
                  (correct_predictions * 100.0f) / samples.size());
    Serial.printf("Average prediction time: %lu μs\n", total_time / samples.size());
    Serial.println();
    
    // Clean up and compute statistics
    Serial.print("Releasing forest from memory... ");
    if (forest.releaseForest()) {
        Serial.println("✅ OK");
    } else {
        Serial.println("⚠️  WARNING: Release failed");
    }
    
    // Optional: Write pending data to dataset for retraining
    // forest.flush_pending_data();

    // Display inference statistics
    Serial.println("\n=== Model Performance Metrics ===");
    float p_score = forest.get_practical_inference_score();
    Serial.printf("Practical Inference Score: %.3f (%.1f%%)\n", p_score, p_score * 100.0f);
    
    int total_logged = forest.get_total_logged_inference();
    Serial.printf("Total Logged Inferences: %d\n", total_logged);

    long unsigned end_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.println("\n=== Execution Summary ===");
    Serial.printf("Total execution time: %lu ms\n", end_forest - start_forest);
    Serial.println("\n✅ Example completed successfully!\n");
}

void loop() {
    manage_files();
}