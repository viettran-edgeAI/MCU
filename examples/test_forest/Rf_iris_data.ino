/*
 * Iris Classification Example
 * 
 * This example demonstrates iris flower species classification using a Random Forest
 * model trained on the classic Iris dataset.
 * 
 * Features:
 * - Trains and evaluates Random Forest model for 3-class classification
 * - Performs predictions on 10 sample feature vectors (4 features each)
 * - Classifies iris species: Setosa (0), Versicolor (1), Virginica (2)
 * - Calculates accuracy and timing metrics
 * 
 * Hardware: ESP32 or compatible microcontroller with LittleFS support
 * Dataset: Iris flowers with sepal/petal measurements (length, width)
 */

#define DEV_STAGE    
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

#include "random_forest_mcu.h"

using namespace mcu;

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

void setup() {
    Serial.begin(115200);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)

    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.println("║  Iris Classification - Random Forest Example      ║");
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
    Serial.print("Setting up model 'iris_data'... ");
    const char* model_name = "iris_data";
    RandomForest forest = RandomForest(model_name);
    
    // Optional: Configure forest parameters
    // forest.set_num_trees(20);
    // forest.set_random_seed(42);
    // forest.set_training_score(Rf_training_score::OOB_SCORE);
    Serial.println("✅ OK");

    // Build and train the model
    Serial.print("Building model... ");
    if (!forest.build_model()) {
        Serial.println("❌ FAILED");
        return;
    }
    Serial.println("✅ OK");

    long unsigned build_time = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.printf("Model built in %lu ms\n", build_time - start_forest);

    // Serial.print("Training model (3 epochs)... ");
    // forest.training(3);
    // Serial.println("✅ OK");

    // Load trained forest from filesystem
    Serial.print("Loading forest... ");
    long unsigned load_start = GET_CURRENT_TIME_IN_MILLISECONDS;
    if (!forest.loadForest()) {
        Serial.println("❌ FAILED");
        return;
    }
    long unsigned load_end = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.printf("Forest loaded in %lu ms\n", load_end - load_start);
    
    // Optional: Enable dataset extension for online learning
    // forest.enable_extend_base_data();

    // check actual prediction time 
    b_vector<float> sample_1 = MAKE_LIST(float,5.1,3.5,1.4,0.2);
    b_vector<float> sample_2 = MAKE_LIST(float,4.9,3.0,1.4,0.2);
    b_vector<float> sample_3 = MAKE_LIST(float,4.7,3.2,1.3,0.2);
    b_vector<float> sample_4 = MAKE_LIST(float,4.9,2.4,3.3,1.0);
    b_vector<float> sample_5 = MAKE_LIST(float,5.0,3.6,1.4,0.2);
    b_vector<float> sample_6 = MAKE_LIST(float,6.4,3.2,5.3,2.3);
    b_vector<float> sample_7 = MAKE_LIST(float,7.7,3.8,6.7,2.2);
    b_vector<float> sample_8 = MAKE_LIST(float,5.5,2.5,4.0,1.3);
    b_vector<float> sample_9 = MAKE_LIST(float,4.4,2.9,1.4,0.2);
    b_vector<float> sample_10 = MAKE_LIST(float,4.9,3.1,1.5,0.1);
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

    vector<uint8_t> true_labels = MAKE_UINT8_LIST(0,0,0,1,0,2,2,1,0,0);

    // Perform predictions on all samples
    unsigned long total_time = 0;
    uint8_t correct_predictions = 0;
    
    Serial.println("\n=== Prediction Results ===");
    Serial.println("Sample | Predicted | Actual | Time (μs) | Match");
    Serial.println("-------|-----------|--------|-----------|------");
    
    for (int i = 0; i < samples.size(); i++){
        rf_predict_result_t result;
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
    // Empty loop - all processing done in setup()
}