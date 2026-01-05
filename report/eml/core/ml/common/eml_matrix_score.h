#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Eml_matrix_score{
        // Confusion matrix components
        b_vector<rf_sample_type, 8> tp;
        b_vector<rf_sample_type, 8> fp;
        b_vector<rf_sample_type, 8> fn;

        rf_sample_type total_predict = 0;
        rf_sample_type correct_predict = 0;
        rf_label_type num_labels;
        uint8_t metric_score;

        public:
        // Constructor
        Eml_matrix_score(rf_label_type num_labels, uint8_t metric_score) 
            : num_labels(num_labels), metric_score(metric_score) {
            // Ensure vectors have logical length == num_labels and are zeroed
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (rf_label_type i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }
        
        void init(rf_label_type num_labels, uint8_t metric_score) {
            this->num_labels = num_labels;
            this->metric_score = metric_score;
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (rf_label_type i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }

        // Reset all counters
        void reset() {
            total_predict = 0;
            correct_predict = 0;
            // Reset existing buffers safely; ensure length matches num_labels
            if (tp.size() != num_labels) {
                tp.clear(); tp.reserve(num_labels); for (rf_label_type i = 0; i < num_labels; ++i) tp.push_back(0);
            } else { tp.fill(0); }
            if (fp.size() != num_labels) {
                fp.clear(); fp.reserve(num_labels); for (rf_label_type i = 0; i < num_labels; ++i) fp.push_back(0);
            } else { fp.fill(0); }
            if (fn.size() != num_labels) {
                fn.clear(); fn.reserve(num_labels); for (rf_label_type i = 0; i < num_labels; ++i) fn.push_back(0);
            } else { fn.fill(0); }
        }

        // Update confusion matrix with a prediction
        void update_prediction(rf_label_type actual_label, rf_label_type predicted_label) {
            if(actual_label >= num_labels || predicted_label >= num_labels) return;
            
            total_predict++;
            if(predicted_label == actual_label) {
                correct_predict++;
                tp[actual_label]++;
            } else {
                fn[actual_label]++;
                fp[predicted_label]++;
            }
        }

        // Get precision for all labels
        b_vector<pair<rf_label_type, float>> get_precisions() {
            b_vector<pair<rf_label_type, float>> precisions;
            precisions.reserve(num_labels);
            for(rf_label_type label = 0; label < num_labels; label++) {
                float prec = (tp[label] + fp[label] == 0) ? 0.0f : 
                            static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                precisions.push_back(make_pair(label, prec));
            }
            return precisions;
        }

        // Get recall for all labels
        b_vector<pair<rf_label_type, float>> get_recalls() {
            b_vector<pair<rf_label_type, float>> recalls;
            recalls.reserve(num_labels);
            for(rf_label_type label = 0; label < num_labels; label++) {
                float rec = (tp[label] + fn[label] == 0) ? 0.0f : 
                        static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                recalls.push_back(make_pair(label, rec));
            }
            return recalls;
        }

        // Get F1 scores for all labels
        b_vector<pair<rf_label_type, float>> get_f1_scores() {
            b_vector<pair<rf_label_type, float>> f1s;
            f1s.reserve(num_labels);
            for(rf_label_type label = 0; label < num_labels; label++) {
                float prec = (tp[label] + fp[label] == 0) ? 0.0f : 
                            static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                float rec = (tp[label] + fn[label] == 0) ? 0.0f : 
                        static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                float f1 = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
                f1s.push_back(make_pair(label, f1));
            }
            return f1s;
        }

        // Get accuracy for all labels (overall accuracy for multi-class)
        b_vector<pair<rf_label_type, float>> get_accuracies() {
            b_vector<pair<rf_label_type, float>> accuracies;
            accuracies.reserve(num_labels);
            float overall_accuracy = (total_predict == 0) ? 0.0f : 
                                    static_cast<float>(correct_predict) / total_predict;
            for(rf_label_type label = 0; label < num_labels; label++) {
                accuracies.push_back(make_pair(label, overall_accuracy));
            }
            return accuracies;
        }

        // Calculate combined score based on training flags
        float calculate_score() {
            if(total_predict == 0) {
                eml_debug(1, "❌ No valid predictions found!");
                return 0.0f;
            }

            float combined_result = 0.0f;
            uint8_t numFlags = 0;

            // Calculate accuracy
            if(metric_score & 0x01) { // ACCURACY 
                float accuracy = static_cast<float>(correct_predict) / total_predict;
                eml_debug(2, "Accuracy: ", accuracy);
                combined_result += accuracy;
                numFlags++;
            }

            // Calculate precision
            if(metric_score & 0x02) { // PRECISION 
                float total_precision = 0.0f;
                rf_label_type valid_labels = 0;
                
                for(rf_label_type label = 0; label < num_labels; label++) {
                    if(tp[label] + fp[label] > 0) {
                        total_precision += static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                        valid_labels++;
                    }
                }
                
                float precision = valid_labels > 0 ? total_precision / valid_labels : 0.0f;
                eml_debug(2, "Precision: ", precision);
                combined_result += precision;
                numFlags++;
            }

            // Calculate recall
            if(metric_score & 0x04) { // RECALL 
                float total_recall = 0.0f;
                rf_label_type valid_labels = 0;
                
                for(rf_label_type label = 0; label < num_labels; label++) {
                    if(tp[label] + fn[label] > 0) {
                        total_recall += static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                        valid_labels++;
                    }
                }
                
                float recall = valid_labels > 0 ? total_recall / valid_labels : 0.0f;
                eml_debug(2, "Recall: ", recall);
                combined_result += recall;
                numFlags++;
            }

            // Calculate F1-Score
            if(metric_score & 0x08) { // F1_SCORE 
                float total_f1 = 0.0f;
                rf_label_type valid_labels = 0;
                
                for(rf_label_type label = 0; label < num_labels; label++) {
                    if(tp[label] + fp[label] > 0 && tp[label] + fn[label] > 0) {
                        float precision = static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                        float recall = static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                        if(precision + recall > 0) {
                            float f1 = 2.0f * precision * recall / (precision + recall);
                            total_f1 += f1;
                            valid_labels++;
                        }
                    }
                }
                
                float f1_score = valid_labels > 0 ? total_f1 / valid_labels : 0.0f;
                eml_debug(2, "F1-Score: ", f1_score);
                combined_result += f1_score;
                numFlags++;
            }

            // Return combined score
            return numFlags > 0 ? combined_result / numFlags : 0.0f;
        }

        size_t memory_usage() const {
            size_t usage = 0;
            usage += sizeof(total_predict) + sizeof(correct_predict) + sizeof(num_labels) + sizeof(metric_score);
            usage += tp.size() * sizeof(uint16_t);
            usage += fp.size() * sizeof(uint16_t);
            usage += fn.size() * sizeof(uint16_t);
            return usage;
        }
    };

} // namespace eml
