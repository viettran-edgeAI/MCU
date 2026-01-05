#pragma once
/**
 * @file eml_random.h
 * @brief EML Random Number Generator
 * 
 * Platform-agnostic random number generator using PCG32 algorithm.
 * Uses PAL for hardware entropy on each platform.
 */

#include "../pal/eml_time.h"
#include <cstdint>

namespace eml {

/**
 * @brief High-quality random number generator using PCG32 algorithm
 * 
 * Features:
 * - Deterministic sequences with seed control
 * - Global seed support for reproducibility
 * - Hardware entropy integration via PAL
 * - Bounded random generation without bias
 */
class Random {
private:
    struct PCG32 {
        uint64_t state = 0x853c49e6748fea9bULL;
        uint64_t inc   = 0xda3e39cb94b95bdbULL;

        inline void seed(uint64_t initstate, uint64_t initseq) {
            state = 0U;
            inc = (initseq << 1u) | 1u;
            next();
            state += initstate;
            next();
        }

        inline uint32_t next() {
            uint64_t oldstate = state;
            state = oldstate * 6364136223846793005ULL + inc;
            uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
            uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
            return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
        }

        inline uint32_t bounded(uint32_t bound) {
            if (bound == 0) return 0;
            uint32_t threshold = -bound % bound;
            while (true) {
                uint32_t r = next();
                if (r >= threshold) return r % bound;
            }
        }
    };

    // Constants for mixing
    static constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
    static constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
    static constexpr uint64_t SMIX_C1 = 0x9e3779b97f4a7c15ULL;
    static constexpr uint64_t SMIX_C2 = 0xbf58476d1ce4e5b9ULL;
    static constexpr uint64_t SMIX_C3 = 0x94d049bb133111ebULL;

    // Global seed state (function-local statics to avoid ODR issues)
    static uint64_t& global_seed() { static uint64_t v = 0ULL; return v; }
    static bool& has_global() { static bool v = false; return v; }

    uint64_t base_seed_ = 0;
    PCG32 engine_;

    inline uint64_t splitmix64(uint64_t x) const {
        x += SMIX_C1;
        x = (x ^ (x >> 30)) * SMIX_C2;
        x = (x ^ (x >> 27)) * SMIX_C3;
        return x ^ (x >> 31);
    }

public:
    /**
     * @brief Default constructor - uses hardware entropy or global seed
     */
    Random() {
        if (has_global()) {
            base_seed_ = global_seed();
        } else {
            // Use PAL for platform-specific entropy
            uint64_t entropy = pal::eml_random_entropy();
            uint64_t cycles = pal::eml_cpu_cycles();
            base_seed_ = splitmix64(entropy ^ cycles);
        }
        engine_.seed(base_seed_, base_seed_ ^ 0xda3e39cb94b95bdbULL);
    }

    /**
     * @brief Constructor with explicit seed
     * @param seed Initial seed value
     */
    explicit Random(uint64_t seed) {
        init(seed, true);
    }

    /**
     * @brief Initialize with optional seed
     * @param seed Seed value
     * @param use_provided_seed If true, use the provided seed; otherwise mix with entropy
     */
    void init(uint64_t seed, bool use_provided_seed = true) {
        if (use_provided_seed) {
            base_seed_ = seed;
        } else if (has_global()) {
            base_seed_ = global_seed();
        } else {
            uint64_t entropy = pal::eml_random_entropy();
            uint64_t cycles = pal::eml_cpu_cycles();
            base_seed_ = splitmix64(entropy ^ cycles ^ seed);
        }
        engine_.seed(base_seed_, base_seed_ ^ 0xda3e39cb94b95bdbULL);
    }

    // Global seed control
    static void setGlobalSeed(uint64_t seed) { global_seed() = seed; has_global() = true; }
    static void clearGlobalSeed() { has_global() = false; }
    static bool hasGlobalSeed() { return has_global(); }

    // Basic generation
    inline uint32_t next() { return engine_.next(); }
    inline uint32_t bounded(uint32_t bound) { return engine_.bounded(bound); }
    inline float nextFloat() { return static_cast<float>(next()) / static_cast<float>(UINT32_MAX); }
    inline double nextDouble() { return static_cast<double>(next()) / static_cast<double>(UINT32_MAX); }

    void seed(uint64_t new_seed) {
        base_seed_ = new_seed;
        engine_.seed(base_seed_, base_seed_ ^ 0xda3e39cb94b95bdbULL);
    }
    
    inline uint64_t getBaseSeed() const { return base_seed_; }

    /**
     * @brief Create a derived RNG for deterministic substreams
     * @param stream Stream identifier
     * @param nonce Optional nonce value
     * @return New Random instance with derived state
     */
    Random deriveRNG(uint64_t stream, uint64_t nonce = 0) const {
        uint64_t s = splitmix64(base_seed_ ^ (stream * SMIX_C1 + nonce));
        uint64_t inc = splitmix64(base_seed_ + (stream << 1) + 0x632be59bd9b4e019ULL);
        Random r;
        r.base_seed_ = s;
        r.engine_.seed(s, inc);
        return r;
    }

    // Hash helpers (FNV-1a)
    static inline uint64_t hashString(const char* data) {
        uint64_t h = FNV_OFFSET;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(data); *p; ++p) {
            h ^= *p;
            h *= FNV_PRIME;
        }
        return h;
    }

    static inline uint64_t hashBytes(const uint8_t* data, size_t len) {
        uint64_t h = FNV_OFFSET;
        for (size_t i = 0; i < len; ++i) {
            h ^= data[i];
            h *= FNV_PRIME;
        }
        return h;
    }

    template <class IdVec>
    static uint64_t hashIDVector(const IdVec& ids) {
        uint64_t h = FNV_OFFSET;
        for (size_t i = 0; i < ids.size(); ++i) {
            auto v = ids[i];
            for (size_t byte = 0; byte < sizeof(v); ++byte) {
                h ^= static_cast<uint64_t>((static_cast<uint64_t>(v) >> (byte * 8)) & 0xFFULL);
                h *= FNV_PRIME;
            }
        }
        size_t sz = ids.size();
        for (size_t byte = 0; byte < sizeof(sz); ++byte) {
            h ^= static_cast<uint64_t>((sz >> (byte * 8)) & 0xFFULL);
            h *= FNV_PRIME;
        }
        return h;
    }

    /**
     * @brief Get memory footprint
     */
    size_t memoryUsage() const {
        return sizeof(Random);
    }
};

} // namespace eml

// =============================================================================
// BACKWARD COMPATIBILITY - Alias to old name
// =============================================================================

namespace mcu {
    using Rf_random = eml::Random;
}
