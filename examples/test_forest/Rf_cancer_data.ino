/*
 * Cancer Detection Example with microSD Card Support
 * 
 * This example demonstrates breast cancer classification using a Random Forest
 * model trained on the Wisconsin Breast Cancer dataset, with data stored on microSD card.
 * 
 * Features:
 * - Trains and evaluates Random Forest model for binary classification
 * - Performs predictions on 10 sample feature vectors (30 features each)
 * - Distinguishes between malignant (1) and benign (0) tumors
 * - Calculates accuracy and timing metrics
 * - Supports microSD card via SDIO 4-bit interface (SD_MMC) or SPI
 * 
 * Hardware: ESP32-CAM or ESP32 with external microSD card module
 * 
 * Storage selection:
 *   - Set STORAGE_MODE below to RfStorageType::FLASH for LittleFS (internal flash)
 *   - Use RfStorageType::SD_MMC_4BIT for the ESP32 SDIO slot (ESP32-CAM default)
 *   - Use RfStorageType::SD_MMC_1BIT if your board shares SD lines with the camera
 *   - Use RfStorageType::SD_SPI for external SPI-based SD readers
 * 
 * Dataset: Breast cancer features (radius, texture, perimeter, area, etc.)
 */

#define DEV_STAGE    
#define RF_DEBUG_LEVEL 2
// #define RF_USE_PSRAM

#include "random_forest_mcu.h"

using namespace mcu;

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

void setup() {
    Serial.begin(115200);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)

    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.println("║  Breast Cancer Detection - Random Forest Example  ║");
    Serial.println("╚════════════════════════════════════════════════════╝\n");
    
    delay(1000);

    // Initialize filesystem
    Serial.print("💾 Initializing file system... ");
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ FAILED!");
        Serial.println("⚠️  File system initialization failed. Cannot continue.");
        return;
    }
    Serial.println("✅ OK");
    
    manage_files();
    delay(500);
    
    long unsigned start_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    
    // Initialize Random Forest model
    Serial.print("Setting up model 'cancer_data'... ");
    const char* model_name = "cancer_data";
    RandomForest forest = RandomForest(model_name);
    
    // Optional: Configure forest parameters
    // forest.set_num_trees(20);
    // forest.set_random_seed(42);
    // forest.set_training_score(Rf_training_score::OOB_SCORE);

    // Build and train the model
    Serial.print("Building model... ");
    if (!forest.build_model()) {
        Serial.println("❌ FAILED");
        return;
    }

    long unsigned build_time = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.printf("Model built in %lu ms\n", build_time - start_forest);
    

    // Serial.print("Training model (3 epochs)... ");
    // forest.training(3);
    // Serial.println("✅ OK");

    // Load trained forest from filesystem
    Serial.print("Loading forest... ");
    if (!forest.loadForest()) {
        Serial.println("❌ FAILED");
        return;
    }

    // check actual prediction time 
    b_vector<float> sample_1 = MAKE_FLOAT_LIST(14.54,27.54,96.73,658.8,0.1139,0.1595,0.1639,0.07364,0.2303,0.07077,0.37,1.033,2.879,32.55,0.005607,0.0424,0.04741,0.0109,0.01857,0.005466,17.46,37.13,124.1,943.2,0.1678,0.6577,0.7026,0.1712,0.4218,0.1341);
    b_vector<float> sample_2 = MAKE_FLOAT_LIST(14.68,20.13,94.74,684.5,0.09867,0.072,0.07395,0.05259,0.1586,0.05922,0.4727,1.24,3.195,45.4,0.005718,0.01162,0.01998,0.01109,0.0141,0.002085,19.07,30.88,123.4,1138,0.1464,0.1871,0.2914,0.1609,0.3029,0.08216);
    b_vector<float> sample_3 = MAKE_FLOAT_LIST(16.13,20.68,108.1,798.8,0.117,0.2022,0.1722,0.1028,0.2164,0.07356,0.5692,1.073,3.854,54.18,0.007026,0.02501,0.03188,0.01297,0.01689,0.004142,20.96,31.48,136.8,1315,0.1789,0.4233,0.4784,0.2073,0.3706,0.1142);
    b_vector<float> sample_4 = MAKE_FLOAT_LIST(19.81,22.15,130,1260,0.09831,0.1027,0.1479,0.09498,0.1582,0.05395,0.7582,1.017,5.865,112.4,0.006494,0.01893,0.03391,0.01521,0.01356,0.001997,27.32,30.88,186.8,2398,0.1512,0.315,0.5372,0.2388,0.2768,0.07615);
    b_vector<float> sample_5 = MAKE_FLOAT_LIST(13.54,14.36,87.46,566.3,0.09779,0.08129,0.06664,0.04781,0.1885,0.05766,0.2699,0.7886,2.058,23.56,0.008462,0.0146,0.02387,0.01315,0.0198,0.0023,15.11,19.26,99.7,711.2,0.144,0.1773,0.239,0.1288,0.2977,0.07259);
    b_vector<float> sample_6 = MAKE_FLOAT_LIST(13.08,15.71,85.63,520,0.1075,0.127,0.04568,0.0311,0.1967,0.06811,0.1852,0.7477,1.383,14.67,0.004097,0.01898,0.01698,0.00649,0.01678,0.002425,14.5,20.49,96.09,630.5,0.1312,0.2776,0.189,0.07283,0.3184,0.08183);
    b_vector<float> sample_7 = MAKE_FLOAT_LIST(9.504,12.44,60.34,273.9,0.1024,0.06492,0.02956,0.02076,0.1815,0.06905,0.2773,0.9768,1.909,15.7,0.009606,0.01432,0.01985,0.01421,0.02027,0.002968,10.23,15.66,65.13,314.9,0.1324,0.1148,0.08867,0.06227,0.245,0.07773);
    b_vector<float> sample_8 = MAKE_FLOAT_LIST(15.34,14.26,102.5,704.4,0.1073,0.2135,0.2077,0.09756,0.2521,0.07032,0.4388,0.7096,3.384,44.91,0.006789,0.05328,0.06446,0.02252,0.03672,0.004394,18.07,19.08,125.1,980.9,0.139,0.5954,0.6305,0.2393,0.4667,0.09946);
    b_vector<float> sample_9 = MAKE_FLOAT_LIST(21.16,23.04,137.2,1404,0.09428,0.1022,0.1097,0.08632,0.1769,0.05278,0.6917,1.127,4.303,93.99,0.004728,0.01259,0.01715,0.01038,0.01083,0.001987,29.17,35.59,188,2615,0.1401,0.26,0.3155,0.2009,0.2822,0.07526);
    b_vector<float> sample_10 = MAKE_FLOAT_LIST(16.65,21.38,110,904.6,0.1121,0.1457,0.1525,0.0917,0.1995,0.0633,0.8068,0.9017,5.455,102.6,0.006048,0.01882,0.02741,0.0113,0.01468,0.002801,26.46,31.56,177,2215,0.1805,0.3578,0.4695,0.2095,0.3613,0.09564);
    
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

    vector<uint8_t> true_labels = MAKE_UINT8_LIST(1,1,1,1,0,0,0,1,1,0);

    forest.warmup_prediction();

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