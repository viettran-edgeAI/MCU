#include <iostream>
#include <iomanip>
#include <cstring>
#include "../../src/STL_MCU.h"

using mcu::packed_vector;

// ============================================================================
// [TIGHT PACKING] Tree_node: No manual trait needed - bitfield layout is auto-packed
// ============================================================================
struct Tree_node {
    uint32_t packed_data = 0;

    inline uint16_t getFeatureID() const { return getBits(0, 8); }
    inline uint8_t getLabel() const { return static_cast<uint8_t>(getBits(8, 5)); }
    inline uint8_t getThresholdSlot() const { return static_cast<uint8_t>(getBits(13, 2)); }
    inline bool getIsLeaf() const { return getBits(15, 1) != 0; }
    inline uint16_t getLeftChildIndex() const { return getBits(16, 8); }

    inline void setFeatureID(uint16_t v) { setBits(0, 8, v); }
    inline void setLabel(uint8_t v) { setBits(8, 5, v); }
    inline void setThresholdSlot(uint8_t v) { setBits(13, 2, v); }
    inline void setIsLeaf(bool v) { setBits(15, 1, v ? 1u : 0u); }
    inline void setLeftChildIndex(uint16_t v) { setBits(16, 8, v); }

private:
    inline uint32_t getBits(uint8_t pos, uint8_t len) const {
        return (packed_data >> pos) & ((1u << len) - 1u);
    }

    inline void setBits(uint8_t pos, uint8_t len, uint32_t val) {
        const uint32_t mask = ((1u << len) - 1u) << pos;
        packed_data = (packed_data & ~mask) | ((val << pos) & mask);
    }
};

// ============================================================================
// [LOOSE PACKING] SensorReading: Explicit trait specialization needed
// This struct has padding/unused bits, so we define the packing scheme manually
// ============================================================================
struct SensorReading {
    uint8_t channel;    // 0-15
    uint8_t level;      // 0-255
};

namespace mcu {
    template<>
    struct packed_value_traits<SensorReading> {
        static uint32_t to_bits(const SensorReading& reading) noexcept {
            const uint32_t ch = static_cast<uint32_t>(reading.channel) & 0x0Fu;
            const uint32_t lv = static_cast<uint32_t>(reading.level) & 0xFFu;
            return ((lv << 4) | ch) & 0xFFFu;
        }

        static SensorReading from_bits(uint32_t bits) noexcept {
            SensorReading reading{};
            reading.channel = static_cast<uint8_t>(bits & 0x0Fu);
            reading.level = static_cast<uint8_t>((bits >> 4) & 0xFFu);
            return reading;
        }
    };
}

void demoBitFlags() {
    std::cout << "\n[1] Tracking button states with packed_vector<1>\n";

    packed_vector<1> buttonStates(8, 0);
    buttonStates.set(0, 1);
    buttonStates.set(3, 1);
    buttonStates.set(5, 1);

    std::cout << "  Button states (bpv=" << static_cast<int>(buttonStates.get_bits_per_value())
              << ", size=" << buttonStates.size() << ")\n";
    std::cout << "  values: ";
    for (size_t i = 0; i < buttonStates.size(); ++i) {
        std::cout << static_cast<int>(buttonStates[i]);
        if (i + 1 < buttonStates.size()) std::cout << ", ";
    }
    std::cout << "\n  bytes used: " << buttonStates.memory_usage() << "\n";
}

void demoQuantizedLevels() {
    std::cout << "\n[2] Storing 4-bit brightness levels\n";

    packed_vector<4, uint8_t> brightness;
    const uint8_t samples[] = {3, 7, 12, 15, 9, 1};
    for (uint8_t value : samples) {
        brightness.push_back(value);
    }

    std::cout << "  Brightness (bpv=" << static_cast<int>(brightness.get_bits_per_value())
              << ", size=" << brightness.size() << ")\n";
    std::cout << "  values: ";
    for (size_t i = 0; i < brightness.size(); ++i) {
        std::cout << static_cast<int>(brightness[i]);
        if (i + 1 < brightness.size()) std::cout << ", ";
    }
    std::cout << "\n  max storable value: " << static_cast<int>(brightness.max_value()) << "\n";
    std::cout << "  bytes used: " << brightness.memory_usage() << "\n";
}

void demoRuntimeBitWidth() {
    std::cout << "\n[3] Switching runtime bit width\n";

    packed_vector<12, uint16_t> adcSamples;
    const uint16_t initialSamples[] = {120, 256, 512, 1000};
    for (uint16_t value : initialSamples) {
        adcSamples.push_back(value);
    }

    std::cout << "  ADC samples initial (bpv=" << static_cast<int>(adcSamples.get_bits_per_value())
              << ", size=" << adcSamples.size() << ")\n";
    std::cout << "  values: ";
    for (size_t i = 0; i < adcSamples.size(); ++i) {
        std::cout << adcSamples[i];
        if (i + 1 < adcSamples.size()) std::cout << ", ";
    }
    std::cout << "\n  bytes used: " << adcSamples.memory_usage() << "\n";

    std::cout << "  set_bits_per_value(6) clears and reconfigures\n";
    adcSamples.set_bits_per_value(6);

    const uint8_t compacted[] = {32, 48, 51, 63, 12};
    for (uint8_t value : compacted) {
        adcSamples.push_back(value);
    }

    std::cout << "  ADC samples after reconfig (bpv=" << static_cast<int>(adcSamples.get_bits_per_value())
              << ", size=" << adcSamples.size() << ")\n";
    std::cout << "  values: ";
    for (size_t i = 0; i < adcSamples.size(); ++i) {
        std::cout << static_cast<int>(adcSamples[i]);
        if (i + 1 < adcSamples.size()) std::cout << ", ";
    }
    std::cout << "\n  bytes used: " << adcSamples.memory_usage() << "\n";
}

void demoTightPacking() {
    std::cout << "\n[4] Tight packing: Tree_node (no packed_value_traits needed)\n";
    std::cout << "  Tree_node uses fixed bit layout → auto-packs via raw struct layout\n";

    packed_vector<24, Tree_node> nodes;

    constexpr size_t sampleCount = 20;
    for (size_t i = 0; i < sampleCount; ++i) {
        Tree_node node;
        node.setFeatureID(static_cast<uint16_t>(i * 3));
        node.setLabel(static_cast<uint8_t>(i & 0x1Fu));
        node.setThresholdSlot(static_cast<uint8_t>(i & 0x3u));
        node.setIsLeaf((i & 1u) == 0u);
        node.setLeftChildIndex(static_cast<uint16_t>((i + 1u) & 0xFFu));
        nodes.push_back(node);
    }

    const size_t naive_size = sizeof(Tree_node) * nodes.size();
    const size_t packed_before = nodes.memory_usage();
    const long long naive_ll = static_cast<long long>(naive_size);
    const long long packed_before_ll = static_cast<long long>(packed_before);
    const long long compression_before = (naive_ll == 0)
        ? 0
        : ((naive_ll - packed_before_ll) * 100) / naive_ll;

    std::cout << "  Stored " << nodes.size() << " Tree_node entries (bpv=24)\n";
    std::cout << "    struct Tree_node size: " << sizeof(Tree_node) << " bytes × "
              << nodes.size() << " = " << naive_size << " bytes (naive storage)\n";
    std::cout << "    packed_vector memory (capacity " << nodes.capacity() << "): "
              << packed_before << " bytes (packed)\n";
    std::cout << "    compression before fit: " << compression_before << "%\n";

    nodes.fit();
    const size_t packed_after = nodes.memory_usage();
    const long long packed_after_ll = static_cast<long long>(packed_after);
    const long long compression_after = (naive_ll == 0)
        ? 0
        : ((naive_ll - packed_after_ll) * 100) / naive_ll;

    std::cout << "    packed_vector memory after fit: " << packed_after << " bytes\n";
    std::cout << "    compression after fit: " << compression_after << "%\n";

    for (size_t i = 0; i < nodes.size(); ++i) {
        const Tree_node retrieved = nodes[i];
        std::cout << "    [" << i << "] feature=" << retrieved.getFeatureID()
                  << " label=" << static_cast<int>(retrieved.getLabel())
                  << " isLeaf=" << (retrieved.getIsLeaf() ? 1 : 0) << "\n";
    }
}

void demoLoosePacking() {
    std::cout << "\n[5] Loose packing: SensorReading (explicit packed_value_traits)\n";
    std::cout << "  SensorReading has unused bits → needs custom trait for efficient packing\n";

    packed_vector<12, SensorReading> readings;

    SensorReading r1;
    r1.channel = 3;
    r1.level = 200;
    readings.push_back(r1);

    SensorReading r2;
    r2.channel = 7;
    r2.level = 150;
    readings.push_back(r2);

    SensorReading r3;
    r3.channel = 1;
    r3.level = 50;
    readings.push_back(r3);

    std::cout << "  Stored 3 SensorReading entries (bpv=12)\n";
    std::cout << "    struct SensorReading size: " << sizeof(SensorReading) << " bytes × 3 = "
              << (sizeof(SensorReading) * 3) << " bytes (naive storage)\n";
    std::cout << "    packed_vector memory: " << readings.memory_usage() << " bytes (packed)\n";

    size_t naive_size = sizeof(SensorReading) * 3;
    size_t packed_size = readings.memory_usage();
    int compression = 100 - (packed_size * 100) / naive_size;
    std::cout << "    compression: " << compression << "%\n";

    for (size_t i = 0; i < readings.size(); ++i) {
        const SensorReading reading = readings[i];
        std::cout << "    [" << i << "] ch=" << static_cast<int>(reading.channel)
                  << " level=" << static_cast<int>(reading.level) << "\n";
    }
}

int main() {
    std::cout << "\n=== packed_vector showcase (PC version) ===\n";

    demoBitFlags();
    demoQuantizedLevels();
    demoRuntimeBitWidth();
    demoTightPacking();
    demoLoosePacking();

    std::cout << "\n=== All demos completed successfully ===\n";
    return 0;
}
