#define DEV_STAGE    
#define RF_DEBUG_LEVEL 2

#include "random_forest_mcu.h"

using namespace mcu;

void setup() {
    Serial.begin(115200);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)

    delay(2000);

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return;
    }
    manage_files();
    delay(1000);
    long unsigned start_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    
    const char* model_name = "iris_data"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5);
    RandomForest forest = RandomForest(model_name); // reproducible by default (can omit random_seed)
    // forest.set_num_trees(20);
    // forest.set_random_seed(42);
    // forest.set_training_score(Rf_training_score::OOB_SCORE); // OOB_SCORE, VALID_SCORE, K_FOLD_SCORE

    forest.build_model();

    forest.training(3);

    forest.loadForest();
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

    char result[32];
    unsigned long start, end, total = 0;
    for (int i = 0; i < samples.size(); i++){
        start = micros();
        forest.predict(samples[i], result, sizeof(result));
        end = micros();
        total += end - start;
        forest.add_actual_label(true_labels[i]);
        Serial.printf("Sample %d - Predicted: %s - Actual: %d\n", i+1, result, true_labels[i]);
    }    
    Serial.printf("Average prediction time: %lu us\n", total / samples.size());
    forest.releaseForest();
    // forest.flush_pending_data();

    float p_score = forest.get_practical_inference_score();
    Serial.printf("Practical Inference Score : %.3f\n", p_score);
    int total_logged = forest.get_total_logged_inference();
    Serial.printf("Total Logged Inference with actual label feedback : %d\n", total_logged);

    // // forest.visual_result(forest.test_data); // Optional visualization


    long unsigned end_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.printf("\nTotal time: %lu ms\n", end_forest - start_forest);
}

void loop() {
    manage_files();
}