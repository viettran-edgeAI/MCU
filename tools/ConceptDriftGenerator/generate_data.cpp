/**
 * @file generate_data.cpp
 * @brief Concept Drift Dataset Generator
 * 
 * A C++ implementation inspired by THU-Concept-Drift-Datasets-v1.0
 * Generates synthetic datasets with controlled concept drift for evaluating
 * adaptive machine learning models on resource-constrained microcontrollers.
 * 
 * Supports drift types:
 *   - abrupt: Instant decision boundary change
 *   - gradual: Linear transition over drift_width instances
 *   - sudden: Step-wise changes at fixed intervals
 *   - recurrent: Oscillating drift pattern
 * 
 * Supports boundary types:
 *   - linear: Rotating hyperplane decision boundary
 *   - circular: Cake-rotation style (angle-based classification)
 *   - chocolate: Grid-based rotation classification
 *   - hash: Feature-hash based classification
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <algorithm>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

// Simple JSON parser for our parameters
struct DatasetConfig {
    std::string name = "default";
    int num_features = 2;
    int num_labels = 2;
    int n_values = 256;
    int burn_in = 1000;
    int n_instances = 5000;
    double drift_magnitude_prior = 0.3;
    double drift_magnitude_conditional = 0.5;
    double drift_magnitude_linear = 0.5;
    double noise_level = 0.02;
    bool drift_priors = true;
    bool drift_conditional = true;
    int seed = 42;
    std::string type = "abrupt";  // abrupt, gradual, sudden, recurrent
    int drift_width = 500;
    std::string boundary_type = "linear";  // linear, circular, chocolate, hash
    
    // Extended parameters for THU-style datasets
    double x_spinaxis = 0.0;
    double y_spinaxis = 0.0;
    int num_drift_points = 5;  // For sudden/recurrent: number of drift points
    bool add_noise = true;
    bool add_redundant = false;
    int num_redundant_features = 3;
};

class ConceptDriftGenerator {
private:
    DatasetConfig config;
    std::mt19937 rng;
    std::uniform_real_distribution<double> uniform_dist{0.0, 1.0};
    std::normal_distribution<double> normal_dist{0.0, 1.0};
    
    // State variables
    int condition_count = 0;
    double current_angle = 0.0;
    
public:
    ConceptDriftGenerator(const DatasetConfig& cfg) : config(cfg), rng(cfg.seed) {}
    
    // Initialize sample with uniform distribution in [-10, 10]
    std::vector<std::vector<double>> initSampleLinear(int n) {
        std::uniform_real_distribution<double> dist(-10.0, 10.0);
        std::vector<std::vector<double>> data(n, std::vector<double>(2));
        for (int i = 0; i < n; i++) {
            data[i][0] = dist(rng);
            data[i][1] = dist(rng);
        }
        return data;
    }
    
    // Initialize sample in circular distribution (radius 10)
    std::vector<std::vector<double>> initSampleCircle(int n) {
        std::uniform_real_distribution<double> r_dist(0.0, 100.0);
        std::uniform_real_distribution<double> theta_dist(0.0, 2 * M_PI);
        std::vector<std::vector<double>> data(n, std::vector<double>(2));
        for (int i = 0; i < n; i++) {
            double rr = r_dist(rng);
            double theta = theta_dist(rng);
            data[i][0] = std::sqrt(rr) * std::cos(theta);
            data[i][1] = std::sqrt(rr) * std::sin(theta);
        }
        return data;
    }
    
    // Initialize multi-dimensional sample
    std::vector<std::vector<double>> initSampleMultiDim(int n, int dim) {
        std::uniform_real_distribution<double> dist(-10.0, 10.0);
        std::vector<std::vector<double>> data(n, std::vector<double>(dim));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < dim; j++) {
                data[i][j] = dist(rng);
            }
        }
        return data;
    }
    
    // Linear decision boundary condition
    bool linearCondition(double x, double y, double x_axis, double y_axis, 
                         const std::string& drift_type, int sample_count) {
        if (drift_type == "abrupt") {
            if (condition_count == 1) {
                return (y - y_axis - (x - x_axis)) >= 0;
            } else {
                return (y - y_axis - (x - x_axis)) <= 0;
            }
        }
        
        // For gradual, sudden, recurrent
        double angle = M_PI / 4.0 - static_cast<double>(condition_count) / sample_count * M_PI;
        if (std::abs(angle - M_PI / 2) < 1e-10 || std::abs(angle + M_PI / 2) < 1e-10) {
            return (x - x_axis) >= 0;
        }
        return (y - y_axis - (x - x_axis) * std::tan(angle)) >= 0;
    }
    
    // Circular (Cake) decision boundary condition
    bool circularCondition(double x, double y, const std::string& drift_type, int sample_count) {
        double angle = std::atan2(y, x) * 180.0 / M_PI;
        if (angle < 0) angle += 360.0;
        
        if (drift_type != "abrupt") {
            angle += static_cast<double>(condition_count) / sample_count * 30.0;
        }
        
        if (drift_type == "abrupt" && condition_count != 1) {
            return (static_cast<int>(angle / 30) % 2) != 0;
        }
        return (static_cast<int>(angle / 30) % 2) == 0;
    }
    
    // Chocolate grid rotation condition
    bool chocolateCondition(double x, double y, double rotation_angle) {
        double cos_a = std::cos(rotation_angle);
        double sin_a = std::sin(rotation_angle);
        double x_rot = x * cos_a - y * sin_a;
        double y_rot = x * sin_a + y * cos_a;
        return (static_cast<int>(x_rot / 5) + static_cast<int>(y_rot / 5)) % 2 == 0;
    }
    
    // Hash-based classification (for high-dimensional data)
    int hashClassify(const std::vector<double>& features, int num_labels, double drift_factor = 0.0) {
        uint64_t hash = 0;
        for (size_t i = 0; i < features.size(); i++) {
            // Apply drift by shifting feature values
            double shifted = features[i] + drift_factor * (i % 2 == 0 ? 1.0 : -1.0);
            int64_t quantized = static_cast<int64_t>(shifted * 1000);
            hash ^= std::hash<int64_t>{}(quantized) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return static_cast<int>(hash % num_labels);
    }
    
    // Add noise to data
    void addNoise(std::vector<std::vector<double>>& data, double noise_std = 0.02) {
        for (auto& row : data) {
            for (auto& val : row) {
                val *= (1.0 + normal_dist(rng) * noise_std);
            }
        }
    }
    
    // Add redundant features
    std::vector<std::vector<double>> addRedundantFeatures(
        const std::vector<std::vector<double>>& data, int num_redundant) {
        std::uniform_real_distribution<double> dist(-10.0, 10.0);
        std::vector<std::vector<double>> extended(data.size());
        
        for (size_t i = 0; i < data.size(); i++) {
            extended[i] = data[i];
            for (int j = 0; j < num_redundant; j++) {
                extended[i].push_back(dist(rng));
            }
        }
        return extended;
    }
    
    // Calculate drift progress (0.0 to 1.0)
    double getDriftProgress(int instance_idx) {
        if (instance_idx < config.burn_in) {
            return 0.0;
        }
        
        int drift_idx = instance_idx - config.burn_in;
        
        if (config.type == "abrupt") {
            return 1.0;
        }
        else if (config.type == "gradual") {
            if (drift_idx >= config.drift_width) {
                return 1.0;
            }
            return static_cast<double>(drift_idx) / config.drift_width;
        }
        else if (config.type == "sudden") {
            int step_size = (config.n_instances - config.burn_in) / config.num_drift_points;
            int step = drift_idx / step_size;
            return std::min(1.0, static_cast<double>(step) / (config.num_drift_points - 1));
        }
        else if (config.type == "recurrent") {
            int cycle_length = (config.n_instances - config.burn_in) / config.num_drift_points;
            int cycle = drift_idx / cycle_length;
            double phase = static_cast<double>(drift_idx % cycle_length) / cycle_length;
            // Oscillate between 0 and 1
            if (cycle % 2 == 0) {
                return phase;
            } else {
                return 1.0 - phase;
            }
        }
        return 0.0;
    }
    
    // Generate Linear Rotation dataset
    std::pair<std::vector<std::vector<double>>, std::vector<int>> generateLinear() {
        auto data = initSampleLinear(config.n_instances);
        std::vector<int> labels(config.n_instances);
        
        condition_count = 0;
        
        for (int i = 0; i < config.n_instances; i++) {
            double progress = getDriftProgress(i);
            condition_count = static_cast<int>(progress * config.n_instances);
            
            if (config.type == "abrupt") {
                condition_count = (i >= config.burn_in) ? -1 : 1;
            }
            
            double angle = progress * config.drift_magnitude_linear * M_PI;
            double cos_a = std::cos(angle);
            double sin_a = std::sin(angle);
            
            // Rotate decision boundary
            double x = data[i][0] - config.x_spinaxis;
            double y = data[i][1] - config.y_spinaxis;
            double x_rot = x * cos_a - y * sin_a;
            
            labels[i] = (x_rot >= 0) ? 0 : 1;
            
            // Apply noise
            if (uniform_dist(rng) < config.noise_level) {
                labels[i] = 1 - labels[i];
            }
        }
        
        if (config.add_noise) {
            addNoise(data, 0.02);
        }
        
        return {data, labels};
    }
    
    // Generate Cake Rotation dataset (circular boundaries)
    std::pair<std::vector<std::vector<double>>, std::vector<int>> generateCakeRotation() {
        auto data = initSampleCircle(config.n_instances);
        std::vector<int> labels(config.n_instances);
        
        for (int i = 0; i < config.n_instances; i++) {
            double progress = getDriftProgress(i);
            double rotation = progress * 30.0 * config.drift_magnitude_conditional;
            
            double angle = std::atan2(data[i][1], data[i][0]) * 180.0 / M_PI;
            if (angle < 0) angle += 360.0;
            angle += rotation;
            
            labels[i] = (static_cast<int>(angle / 30) % 2 == 0) ? 0 : 1;
            
            // Multi-class support
            if (config.num_labels > 2) {
                labels[i] = static_cast<int>(angle / (360.0 / config.num_labels)) % config.num_labels;
            }
            
            // Apply noise
            if (uniform_dist(rng) < config.noise_level) {
                labels[i] = static_cast<int>(uniform_dist(rng) * config.num_labels);
            }
        }
        
        if (config.add_noise) {
            addNoise(data, 0.02);
        }
        
        return {data, labels};
    }
    
    // Generate Chocolate Rotation dataset
    std::pair<std::vector<std::vector<double>>, std::vector<int>> generateChocolateRotation() {
        auto data = initSampleLinear(config.n_instances);
        std::vector<int> labels(config.n_instances);
        
        for (int i = 0; i < config.n_instances; i++) {
            double progress = getDriftProgress(i);
            double rotation = progress * M_PI / 2.0 * config.drift_magnitude_conditional;
            
            if (chocolateCondition(data[i][0], data[i][1], rotation)) {
                labels[i] = 0;
            } else {
                labels[i] = 1;
            }
            
            // Apply noise
            if (uniform_dist(rng) < config.noise_level) {
                labels[i] = 1 - labels[i];
            }
        }
        
        if (config.add_noise) {
            addNoise(data, 0.02);
        }
        
        return {data, labels};
    }
    
    // Generate Hash-based dataset (for high-dimensional data)
    std::pair<std::vector<std::vector<double>>, std::vector<int>> generateHash() {
        auto data = initSampleMultiDim(config.n_instances, config.num_features);
        std::vector<int> labels(config.n_instances);
        
        for (int i = 0; i < config.n_instances; i++) {
            double progress = getDriftProgress(i);
            
            // Apply prior drift by shifting feature distributions
            if (config.drift_priors && progress > 0) {
                for (int f = 0; f < config.num_features; f++) {
                    data[i][f] += progress * config.drift_magnitude_prior * 5.0 * ((f % 2 == 0) ? 1.0 : -1.0);
                }
            }
            
            // Apply conditional drift
            double drift_factor = progress * config.drift_magnitude_conditional * 5.0;
            labels[i] = hashClassify(data[i], config.num_labels, drift_factor);
            
            // Apply noise
            if (uniform_dist(rng) < config.noise_level) {
                labels[i] = static_cast<int>(uniform_dist(rng) * config.num_labels);
            }
        }
        
        if (config.add_noise) {
            addNoise(data, 0.02);
        }
        
        return {data, labels};
    }
    
    // Generate Rolling Torus dataset
    std::pair<std::vector<std::vector<double>>, std::vector<int>> generateRollingTorus() {
        auto circle_data = initSampleCircle(config.n_instances);
        std::vector<std::vector<double>> data(config.n_instances, std::vector<double>(2));
        std::vector<int> labels(config.n_instances);
        
        // Place samples in two fixed tori
        for (int i = 0; i < config.n_instances; i++) {
            if (uniform_dist(rng) < 0.5) {
                data[i][0] = circle_data[i][0] + 10;  // Right torus center
                data[i][1] = circle_data[i][1] + 10;
            } else {
                data[i][0] = circle_data[i][0] - 10;  // Left torus center
                data[i][1] = circle_data[i][1] + 10;
            }
        }
        
        // Rolling torus for classification
        for (int i = 0; i < config.n_instances; i++) {
            double progress = getDriftProgress(i);
            double x_rolling = -35.0 + progress * 70.0;  // Torus rolls from left to right
            double y_rolling = 10.0;
            
            double dx = data[i][0] - x_rolling;
            double dy = data[i][1] - y_rolling;
            bool in_rolling = (dx * dx + dy * dy) <= 100;  // radius 10
            
            double dx2 = data[i][0] - 10;
            double dy2 = data[i][1] - 10;
            bool in_fixed = (dx2 * dx2 + dy2 * dy2) <= 100;
            
            if (in_rolling) {
                labels[i] = in_fixed ? 1 : 0;
            } else {
                labels[i] = in_fixed ? 0 : 1;
            }
            
            // Apply noise
            if (uniform_dist(rng) < config.noise_level) {
                labels[i] = 1 - labels[i];
            }
        }
        
        if (config.add_noise) {
            addNoise(data, 0.02);
        }
        
        return {data, labels};
    }
    
    // Main generation function
    std::pair<std::vector<std::vector<double>>, std::vector<int>> generate() {
        std::pair<std::vector<std::vector<double>>, std::vector<int>> result;
        
        if (config.boundary_type == "linear") {
            result = generateLinear();
        }
        else if (config.boundary_type == "circular" || config.boundary_type == "cake") {
            result = generateCakeRotation();
        }
        else if (config.boundary_type == "chocolate") {
            result = generateChocolateRotation();
        }
        else if (config.boundary_type == "rolling_torus" || config.boundary_type == "torus") {
            result = generateRollingTorus();
        }
        else {  // Default: hash-based for high-dimensional
            result = generateHash();
        }
        
        // Add redundant features if requested
        if (config.add_redundant && config.num_redundant_features > 0) {
            result.first = addRedundantFeatures(result.first, config.num_redundant_features);
        }
        
        return result;
    }
    
    // Save to CSV
    void saveToCSV(const std::string& filename,
                   const std::vector<std::vector<double>>& data,
                   const std::vector<int>& labels) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << std::endl;
            return;
        }
        
        // Write header
        int num_cols = data[0].size();
        for (int i = 0; i < num_cols; i++) {
            file << "x" << (i + 1);
            if (i < num_cols - 1) file << ",";
        }
        file << ",label\n";
        
        // Write data
        file << std::fixed << std::setprecision(6);
        for (size_t i = 0; i < data.size(); i++) {
            for (size_t j = 0; j < data[i].size(); j++) {
                file << data[i][j] << ",";
            }
            file << labels[i] << "\n";
        }
        
        file.close();
        std::cout << "Saved: " << filename << " (" << data.size() << " samples, " 
                  << num_cols << " features)" << std::endl;
    }
};

// Simple JSON string value extractor
std::string extractString(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    
    size_t start = json.find("\"", pos + 1);
    if (start == std::string::npos) return "";
    
    size_t end = json.find("\"", start + 1);
    if (end == std::string::npos) return "";
    
    return json.substr(start + 1, end - start - 1);
}

// Simple JSON numeric value extractor
double extractNumber(const std::string& json, const std::string& key, double default_val = 0.0) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    
    pos = json.find(":", pos);
    if (pos == std::string::npos) return default_val;
    
    // Skip whitespace
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    // Check if it's a string (then try to parse as number)
    if (json[pos] == '"') return default_val;
    
    // Find end of number
    size_t end = pos;
    while (end < json.length() && (std::isdigit(json[end]) || json[end] == '.' || 
           json[end] == '-' || json[end] == 'e' || json[end] == 'E' || json[end] == '+')) {
        end++;
    }
    
    try {
        return std::stod(json.substr(pos, end - pos));
    } catch (...) {
        return default_val;
    }
}

// Simple JSON boolean value extractor
bool extractBool(const std::string& json, const std::string& key, bool default_val = false) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    
    pos = json.find(":", pos);
    if (pos == std::string::npos) return default_val;
    
    // Skip whitespace
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    
    return default_val;
}

// Parse a single config object from JSON
DatasetConfig parseConfig(const std::string& json_obj) {
    DatasetConfig cfg;
    
    cfg.name = extractString(json_obj, "name");
    if (cfg.name.empty()) cfg.name = "dataset";
    
    cfg.num_features = static_cast<int>(extractNumber(json_obj, "num_features", 2));
    cfg.num_labels = static_cast<int>(extractNumber(json_obj, "num_labels", 2));
    cfg.n_values = static_cast<int>(extractNumber(json_obj, "n_values", 256));
    cfg.burn_in = static_cast<int>(extractNumber(json_obj, "burn_in", 1000));
    cfg.n_instances = static_cast<int>(extractNumber(json_obj, "n_instances", 5000));
    cfg.drift_magnitude_prior = extractNumber(json_obj, "drift_magnitude_prior", 0.3);
    cfg.drift_magnitude_conditional = extractNumber(json_obj, "drift_magnitude_conditional", 0.5);
    cfg.drift_magnitude_linear = extractNumber(json_obj, "drift_magnitude_linear", 0.5);
    cfg.noise_level = extractNumber(json_obj, "noise_level", 0.02);
    cfg.drift_priors = extractBool(json_obj, "drift_priors", true);
    cfg.drift_conditional = extractBool(json_obj, "drift_conditional", true);
    cfg.seed = static_cast<int>(extractNumber(json_obj, "seed", 42));
    cfg.type = extractString(json_obj, "type");
    if (cfg.type.empty()) cfg.type = "abrupt";
    cfg.drift_width = static_cast<int>(extractNumber(json_obj, "drift_width", 500));
    cfg.boundary_type = extractString(json_obj, "boundary_type");
    if (cfg.boundary_type.empty()) cfg.boundary_type = "linear";
    
    cfg.x_spinaxis = extractNumber(json_obj, "x_spinaxis", 0.0);
    cfg.y_spinaxis = extractNumber(json_obj, "y_spinaxis", 0.0);
    cfg.num_drift_points = static_cast<int>(extractNumber(json_obj, "num_drift_points", 5));
    cfg.add_noise = extractBool(json_obj, "add_noise", true);
    cfg.add_redundant = extractBool(json_obj, "add_redundant", false);
    cfg.num_redundant_features = static_cast<int>(extractNumber(json_obj, "num_redundant_features", 3));
    
    return cfg;
}

// Parse array of configs from JSON file
std::vector<DatasetConfig> parseConfigFile(const std::string& filename) {
    std::vector<DatasetConfig> configs;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open config file: " << filename << std::endl;
        return configs;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Find array brackets
    size_t start = content.find('[');
    size_t end = content.rfind(']');
    if (start == std::string::npos || end == std::string::npos) {
        std::cerr << "Error: Invalid JSON format (missing array brackets)" << std::endl;
        return configs;
    }
    
    // Parse each object in array
    size_t pos = start + 1;
    while (pos < end) {
        // Find object start
        size_t obj_start = content.find('{', pos);
        if (obj_start == std::string::npos || obj_start >= end) break;
        
        // Find matching closing brace
        int brace_count = 1;
        size_t obj_end = obj_start + 1;
        while (obj_end < end && brace_count > 0) {
            if (content[obj_end] == '{') brace_count++;
            else if (content[obj_end] == '}') brace_count--;
            obj_end++;
        }
        
        if (brace_count == 0) {
            std::string obj = content.substr(obj_start, obj_end - obj_start);
            configs.push_back(parseConfig(obj));
        }
        
        pos = obj_end;
    }
    
    return configs;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  -c, --config <file>    Path to parameters.json (default: parameters.json)\n"
              << "  -o, --output <dir>     Output directory (default: datasets)\n"
              << "  -h, --help             Show this help message\n"
              << "\nSupported drift types:\n"
              << "  - abrupt:    Instant decision boundary change at burn_in\n"
              << "  - gradual:   Linear transition over drift_width instances\n"
              << "  - sudden:    Step-wise changes at num_drift_points intervals\n"
              << "  - recurrent: Oscillating drift pattern\n"
              << "\nSupported boundary types:\n"
              << "  - linear:    Rotating hyperplane (like THU Linear datasets)\n"
              << "  - circular:  Cake-rotation style (angle-based classification)\n"
              << "  - chocolate: Grid-based rotation (like THU ChocolateRotation)\n"
              << "  - torus:     Rolling torus (like THU RollingTorus)\n"
              << "  - hash:      Feature-hash based (for high-dimensional data)\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string config_file = "parameters.json";
    std::string output_dir = "datasets";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }
    
    // Create output directory if it doesn't exist
    fs::create_directories(output_dir);
    
    std::cout << "=== Concept Drift Dataset Generator ===" << std::endl;
    std::cout << "Config file: " << config_file << std::endl;
    std::cout << "Output directory: " << output_dir << std::endl;
    std::cout << std::endl;
    
    // Parse config file
    auto configs = parseConfigFile(config_file);
    if (configs.empty()) {
        std::cerr << "Error: No valid configurations found" << std::endl;
        return 1;
    }
    
    std::cout << "Found " << configs.size() << " dataset configuration(s)\n" << std::endl;
    
    // Generate each dataset
    for (const auto& cfg : configs) {
        std::cout << "Generating: " << cfg.name << std::endl;
        std::cout << "  Type: " << cfg.type << ", Boundary: " << cfg.boundary_type << std::endl;
        std::cout << "  Features: " << cfg.num_features << ", Labels: " << cfg.num_labels << std::endl;
        std::cout << "  Instances: " << cfg.n_instances << ", Burn-in: " << cfg.burn_in << std::endl;
        
        ConceptDriftGenerator generator(cfg);
        auto [data, labels] = generator.generate();
        
        // Construct filename
        std::string suffix = "_" + cfg.type;
        if (cfg.add_noise) suffix += "_noise";
        if (cfg.add_redundant) suffix += "_redundant";
        
        std::string filename = output_dir + "/" + cfg.name + suffix + ".csv";
        generator.saveToCSV(filename, data, labels);
        
        std::cout << std::endl;
    }
    
    std::cout << "=== Generation Complete ===" << std::endl;
    return 0;
}
