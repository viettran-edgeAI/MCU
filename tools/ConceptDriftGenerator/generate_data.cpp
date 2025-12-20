#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <numeric>

namespace fs = std::filesystem;

class DriftGenerator {
public:
    enum class DriftType { ABRUPT, GRADUAL, REOCCURRING };

    struct Config {
        std::string name;
        int num_features;
        int num_labels;
        int burn_in;
        int n_instances;
        double drift_magnitude_prior;
        double drift_magnitude_conditional;
        double noise_level;
        bool drift_priors;
        bool drift_conditional;
        uint32_t seed;
        DriftType type = DriftType::ABRUPT;
        int drift_width = 100; // For gradual drift
        std::string boundary_type = "hash"; // "hash" or "linear"
        double drift_magnitude_linear = 0.5;
    };

private:
    Config config;
    std::mt19937 rng;

    std::vector<std::vector<double>> px_bd;
    std::vector<std::vector<double>> px_ad;
    
    std::vector<double> weights_bd;
    std::vector<double> weights_ad;

    std::vector<std::vector<double>> generate_random_px() {
        std::vector<std::vector<double>> px(config.num_features, std::vector<double>(config.num_labels));
        std::gamma_distribution<double> dist(1.0, 1.0);
        for (int a = 0; a < config.num_features; ++a) {
            double sum = 0;
            for (int v = 0; v < config.num_labels; ++v) {
                px[a][v] = dist(rng);
                sum += px[a][v];
            }
            for (int v = 0; v < config.num_labels; ++v) {
                px[a][v] /= sum;
            }
        }
        return px;
    }

    double compute_hellinger_px(const std::vector<std::vector<double>>& px1, const std::vector<std::vector<double>>& px2) {
        double prod_sum_sqrt = 1.0;
        for (int a = 0; a < config.num_features; ++a) {
            double sum_sqrt = 0;
            for (int v = 0; v < config.num_labels; ++v) {
                sum_sqrt += std::sqrt(px1[a][v] * px2[a][v]);
            }
            prod_sum_sqrt *= sum_sqrt;
        }
        return std::sqrt(1.0 - prod_sum_sqrt);
    }

    std::vector<std::vector<double>> generate_px_with_drift(const std::vector<std::vector<double>>& base, double target, double precision = 0.01) {
        std::vector<std::vector<double>> best_px;
        double min_diff = 1e9;
        for (int i = 0; i < 1000; ++i) {
            auto candidate = generate_random_px();
            double mag = compute_hellinger_px(base, candidate);
            double diff = std::abs(mag - target);
            if (diff < min_diff) {
                min_diff = diff;
                best_px = candidate;
            }
            if (diff < precision) break;
        }
        return best_px;
    }

    int get_class(const std::vector<int>& x_values, bool is_after) {
        if (config.boundary_type == "linear") {
            const auto& weights = is_after ? weights_ad : weights_bd;
            double score = 0;
            for (int i = 0; i < x_values.size(); ++i) {
                score += x_values[i] * weights[i];
            }
            
            // Normalize score to [0, 1] roughly
            // Max possible score is num_features * (num_labels - 1) * max_weight
            // Min is 0
            double max_possible = config.num_features * (config.num_labels - 1);
            int label = static_cast<int>((score / max_possible) * config.num_labels);
            return std::clamp(label, 0, config.num_labels - 1);
        }

        // Use a stable hash of x_values to determine the class deterministically for each combination
        size_t h = 0;
        for (int v : x_values) {
            h ^= std::hash<int>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        
        // Base class determination
        std::mt19937 local_rng(static_cast<uint32_t>(h ^ config.seed));
        std::uniform_int_distribution<int> class_dist(0, config.num_labels - 1);
        int base_class = class_dist(local_rng);
        
        if (!is_after || !config.drift_conditional) return base_class;
        
        // Determine if this specific combination drifts
        std::uniform_real_distribution<double> drift_dist(0.0, 1.0);
        if (drift_dist(local_rng) < config.drift_magnitude_conditional) {
            // Return a different class for the drifted state
            int new_class = class_dist(local_rng);
            if (new_class == base_class) new_class = (new_class + 1) % config.num_labels;
            return new_class;
        }
        
        return base_class;
    }

public:
    DriftGenerator(const Config& cfg) : config(cfg), rng(cfg.seed) {
        px_bd = generate_random_px();

        if (config.drift_priors) {
            px_ad = generate_px_with_drift(px_bd, config.drift_magnitude_prior);
        } else {
            px_ad = px_bd;
        }

        // Initialize linear weights
        weights_bd.resize(config.num_features);
        std::uniform_real_distribution<double> w_dist(0.0, 1.0);
        for (int i = 0; i < config.num_features; ++i) weights_bd[i] = w_dist(rng);

        weights_ad = weights_bd;
        if (config.drift_conditional && config.boundary_type == "linear") {
            for (int i = 0; i < config.num_features; ++i) {
                if (w_dist(rng) < config.drift_magnitude_linear) {
                    weights_ad[i] = w_dist(rng);
                }
            }
        }
    }

    void generate_to_file(const std::string& filepath) {
        std::ofstream out(filepath);
        if (!out.is_open()) return;

        for (int i = 0; i < config.num_features; ++i) {
            out << "x" << (i + 1) << ",";
        }
        out << "class\n";

        for (int i = 0; i < config.n_instances; ++i) {
            double drift_prob = 0.0;
            if (config.type == DriftType::ABRUPT) {
                drift_prob = (i > config.burn_in) ? 1.0 : 0.0;
            } else if (config.type == DriftType::GRADUAL) {
                // Gradual drift using sigmoid-like transition
                if (i < config.burn_in) drift_prob = 0.0;
                else if (i > config.burn_in + config.drift_width) drift_prob = 1.0;
                else {
                    drift_prob = static_cast<double>(i - config.burn_in) / config.drift_width;
                }
            } else if (config.type == DriftType::REOCCURRING) {
                // Reoccurring drift using a sine wave
                // Period is drift_width
                double period = config.drift_width > 0 ? config.drift_width : 500.0;
                drift_prob = 0.5 * (1.0 + std::sin(2.0 * M_PI * i / period));
            }

            std::uniform_real_distribution<double> d_dist(0.0, 1.0);
            bool is_after = d_dist(rng) < drift_prob;

            const auto& px = is_after ? px_ad : px_bd;

            std::vector<int> x_values(config.num_features);
            for (int a = 0; a < config.num_features; ++a) {
                std::discrete_distribution<int> d(px[a].begin(), px[a].end());
                x_values[a] = d(rng);
                out << x_values[a] << ",";
            }

            int y_value = get_class(x_values, is_after);

            std::uniform_real_distribution<double> noise_dist(0.0, 1.0);
            if (noise_dist(rng) < config.noise_level) {
                std::uniform_int_distribution<int> flip_dist(0, config.num_labels - 1);
                y_value = flip_dist(rng);
            }

            out << y_value << "\n";
        }
    }
};

std::vector<DriftGenerator::Config> parse_configs(const std::string& path) {
    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    
    std::vector<DriftGenerator::Config> configs;
    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;
        std::string block = content.substr(pos, end - pos + 1);
        pos = end + 1;

        DriftGenerator::Config cfg;
        auto extract = [&](const std::string& key, auto& val) {
            size_t kpos = block.find("\"" + key + "\"");
            if (kpos == std::string::npos) return;
            size_t colon = block.find(':', kpos);
            if (colon == std::string::npos) return;
            
            if constexpr (std::is_same_v<std::decay_t<decltype(val)>, std::string>) {
                size_t q1 = block.find('"', colon);
                size_t q2 = block.find('"', q1 + 1);
                val = block.substr(q1 + 1, q2 - q1 - 1);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(val)>, bool>) {
                val = (block.find("true", colon) != std::string::npos);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(val)>, DriftGenerator::DriftType>) {
                if (block.find("gradual", colon) != std::string::npos) val = DriftGenerator::DriftType::GRADUAL;
                else if (block.find("reoccurring", colon) != std::string::npos) val = DriftGenerator::DriftType::REOCCURRING;
                else val = DriftGenerator::DriftType::ABRUPT;
            } else {
                std::string sval = "";
                for (size_t i = colon + 1; i < block.size(); ++i) {
                    if (std::isdigit(block[i]) || block[i] == '.' || block[i] == '-') sval += block[i];
                    else if (!sval.empty() && !std::isdigit(block[i]) && block[i] != '.') break;
                }
                if (!sval.empty()) {
                    if constexpr (std::is_floating_point_v<std::decay_t<decltype(val)>>) val = std::stod(sval);
                    else val = std::stoi(sval);
                }
            }
        };

        extract("name", cfg.name);
        extract("num_features", cfg.num_features);
        extract("num_labels", cfg.num_labels);
        extract("burn_in", cfg.burn_in);
        extract("n_instances", cfg.n_instances);
        extract("drift_magnitude_prior", cfg.drift_magnitude_prior);
        extract("drift_magnitude_conditional", cfg.drift_magnitude_conditional);
        extract("noise_level", cfg.noise_level);
        extract("drift_priors", cfg.drift_priors);
        extract("drift_conditional", cfg.drift_conditional);
        extract("seed", cfg.seed);
        extract("type", cfg.type);
        extract("drift_width", cfg.drift_width);
        extract("boundary_type", cfg.boundary_type);
        extract("drift_magnitude_linear", cfg.drift_magnitude_linear);
        
        configs.push_back(cfg);
    }
    return configs;
}

int main() {
    std::string config_path = "/home/viettran/Arduino/libraries/STL_MCU/tools/ConceptDriftGenerator/parameters.json";
    std::string output_dir = "/home/viettran/Arduino/libraries/STL_MCU/tools/ConceptDriftGenerator/datasets";
    
    fs::create_directories(output_dir);
    
    auto configs = parse_configs(config_path);
    for (const auto& cfg : configs) {
        std::string type_str = "abrupt";
        if (cfg.type == DriftGenerator::DriftType::GRADUAL) type_str = "gradual";
        else if (cfg.type == DriftGenerator::DriftType::REOCCURRING) type_str = "reoccurring";

        std::cout << "Generating dataset (C++): " << cfg.name << " (" 
                  << type_str << ")..." << std::endl;
        DriftGenerator gen(cfg);
        gen.generate_to_file(output_dir + "/" + cfg.name + ".csv");
        std::cout << "Saved to " << output_dir << "/" << cfg.name << ".csv" << std::endl;
    }

    return 0;
}
