#include <Arduino.h>
#include <STL_MCU.h>
#include <type_traits>
#include <cstring>

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

template<uint8_t BitsPerValue, typename ValueType>
void printPackedVector(const char* label, const packed_vector<BitsPerValue, ValueType>& vec) {
    Serial.print(label);
    Serial.print(F(" (bpv="));
    Serial.print(vec.get_bits_per_value());
    Serial.print(F(", size="));
    Serial.print(vec.size());
    Serial.print(F(")\n  values: "));

    if constexpr (std::is_arithmetic_v<ValueType>) {
        for (size_t i = 0; i < vec.size(); ++i) {
            Serial.print(static_cast<uint32_t>(vec[i]));
            if (i + 1 < vec.size()) {
                Serial.print(F(", "));
            }
        }
    } else {
        Serial.print(F("<custom type>"));
    }

    Serial.print(F("\n  bytes used: "));
    Serial.println(vec.memory_usage());
}

void demoBitFlags() {
    Serial.println(F("\n[1] Tracking button states with packed_vector<1>"));

    packed_vector<1> buttonStates(8, 0);
    buttonStates.set(0, 1);
    buttonStates.set(3, 1);
    buttonStates.set(5, 1);

    printPackedVector("Button states", buttonStates);
}

void demoQuantizedLevels() {
    Serial.println(F("\n[2] Storing 4-bit brightness levels"));

    packed_vector<4, uint8_t> brightness;
    const uint8_t samples[] = {3, 7, 12, 15, 9, 1};
    for (uint8_t value : samples) {
        brightness.push_back(value);
    }

    printPackedVector("Brightness", brightness);
    Serial.print(F("  max storable value: "));
    Serial.println(static_cast<uint32_t>(brightness.max_value()));
}

void demoRuntimeBitWidth() {
    Serial.println(F("\n[3] Switching runtime bit width"));

    packed_vector<12, uint16_t> adcSamples;
    const uint16_t initialSamples[] = {120, 256, 512, 1000};
    for (uint16_t value : initialSamples) {
        adcSamples.push_back(value);
    }

    printPackedVector("ADC samples", adcSamples);

    Serial.println(F("  set_bits_per_value(6) compacts future values"));
    adcSamples.set_bits_per_value(6);

    const uint8_t compacted[] = {32, 48, 51, 63, 12};
    for (uint8_t value : compacted) {
        adcSamples.push_back(value);
    }

    printPackedVector("Compacted samples", adcSamples);
}

void demoTightPacking() {
    Serial.println(F("\n[4] Tight packing: Tree_node (no packed_value_traits needed)"));
    Serial.println(F("  Tree_node uses fixed bit layout → auto-packs via raw struct layout"));

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

    const size_t naive_bytes = sizeof(Tree_node) * nodes.size();
    const size_t packed_before = nodes.memory_usage();
    const long naive_long = static_cast<long>(naive_bytes);
    const long packed_before_long = static_cast<long>(packed_before);
    const long compression_before = (naive_long == 0)
        ? 0
        : ((naive_long - packed_before_long) * 100) / naive_long;

    Serial.print(F("  Stored "));
    Serial.print(nodes.size());
    Serial.println(F(" Tree_node entries (bpv=24)"));
    Serial.print(F("    struct Tree_node size: "));
    Serial.print(sizeof(Tree_node));
    Serial.print(F(" bytes × "));
    Serial.print(nodes.size());
    Serial.print(F(" = "));
    Serial.print(naive_bytes);
    Serial.println(F(" bytes (naive storage)"));
    Serial.print(F("    packed_vector memory (capacity "));
    Serial.print(nodes.capacity());
    Serial.print(F("): "));
    Serial.print(packed_before);
    Serial.println(F(" bytes (packed)"));
    Serial.print(F("    compression before fit: "));
    Serial.print(compression_before);
    Serial.println(F("%"));

    nodes.fit();
    const size_t packed_after = nodes.memory_usage();
    const long packed_after_long = static_cast<long>(packed_after);
    const long compression_after = (naive_long == 0)
        ? 0
        : ((naive_long - packed_after_long) * 100) / naive_long;

    Serial.print(F("    packed_vector memory after fit: "));
    Serial.print(packed_after);
    Serial.println(F(" bytes"));
    Serial.print(F("    compression after fit: "));
    Serial.print(compression_after);
    Serial.println(F("%"));

    for (size_t i = 0; i < nodes.size(); ++i) {
        const Tree_node retrieved = nodes[i];
        Serial.print(F("    ["));
        Serial.print(i);
        Serial.print(F("] feature="));
        Serial.print(retrieved.getFeatureID());
        Serial.print(F(" label="));
        Serial.print(retrieved.getLabel());
        Serial.print(F(" isLeaf="));
        Serial.print(retrieved.getIsLeaf() ? 1 : 0);
        Serial.println();
    }
}

void demoLoosePacking() {
    Serial.println(F("\n[5] Loose packing: SensorReading (explicit packed_value_traits)"));
    Serial.println(F("  SensorReading has unused bits → needs custom trait for efficient packing"));

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

    Serial.print(F("  Stored 3 SensorReading entries (bpv=12)\n"));
    Serial.print(F("    struct SensorReading size: "));
    Serial.print(sizeof(SensorReading));
    Serial.print(F(" bytes × 3 = "));
    Serial.print(sizeof(SensorReading) * 3);
    Serial.println(F(" bytes (naive storage)"));
    Serial.print(F("    packed_vector memory: "));
    Serial.print(readings.memory_usage());
    Serial.println(F(" bytes (packed)"));
    Serial.print(F("    compression: "));
    Serial.print(100 - (readings.memory_usage() * 100) / (sizeof(SensorReading) * 3));
    Serial.println(F("%"));

    for (size_t i = 0; i < readings.size(); ++i) {
        const SensorReading reading = readings[i];
        Serial.print(F("    ["));
        Serial.print(i);
        Serial.print(F("] ch="));
        Serial.print(reading.channel);
        Serial.print(F(" level="));
        Serial.print(reading.level);
        Serial.println();
    }
}

void setup() {
    Serial.begin(115200);

    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 2000) {
        ;
    }

    Serial.println(F("\n=== packed_vector showcase ==="));

    demoBitFlags();
    demoQuantizedLevels();
    demoRuntimeBitWidth();
    demoTightPacking();
    demoLoosePacking();
}

void loop() {
    // No repeated work required.
}
