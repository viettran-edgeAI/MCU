# pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>


static constexpr uint8_t MAX_CAP = 255;
static constexpr uint8_t INIT_CAP = 10;


class hash_kernel{
public:
    hash_kernel() = default;
    ~hash_kernel() = default;
private:
    [[nodiscard]] inline constexpr uint8_t gcd(uint16_t a, uint8_t b) noexcept{
        while (b != 0) {
            uint8_t temp = b;
            b = a % b;
            a = temp;
        }
        return a;
    }
    [[nodiscard]] inline constexpr int djb2Hash(uint8_t key,int hash) noexcept {
        return hash*33 + key;
    }

    // Fast DJB2-style hash for null-terminated C strings
    inline size_t hash_cstr(const char* str) noexcept {
        size_t hash = 5381;
        while (*str) {
            hash = (hash * 33) ^ static_cast<uint8_t>(*str++);  // XOR for more diffusion
        }
        return hash;
    }
public:
    template<typename T>
    inline size_t preprocess_hash_input(const T& value) const noexcept {
        if constexpr (std::is_integral_v<T>) {
            // 1. Integer types: fast path
            return static_cast<size_t>(value);
        }
        else if constexpr (std::is_floating_point_v<T>) {
            // 2. Floating-point: reinterpret bits for hashing
            size_t result = 0;
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(T); ++i)
                result = result * 31 + raw[i];
            return result;
        }
        else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
            // 3. C-strings: DJB2 variant
            size_t hash = 5381;
            const char* str = value;
            while (*str)
                hash = (hash * 33) ^ static_cast<uint8_t>(*str++);
            return hash;
        }
    #if __has_include(<string>)
        else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const std::string&>) {
            // 4. std::string: hash its data
            size_t hash = 5381;
            for (char c : value)
                hash = (hash * 33) ^ static_cast<uint8_t>(c);
            return hash;
        }
    #endif
    #ifdef ARDUINO
        else if constexpr (std::is_same_v<T, String> || std::is_same_v<T, const String&>) {
            // 5. Arduino String: hash its data
            size_t hash = 5381;
            for (size_t i = 0; i < value.length(); ++i)
                hash = (hash * 33) ^ static_cast<uint8_t>(value[i]);
            return hash;
        }
    #endif
        else if constexpr (std::is_trivially_copyable_v<T>) {
            // 6. Custom struct/POD: hash raw bytes
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
            size_t result = 0;
            for (size_t i = 0; i < sizeof(T); ++i)
                result = result * 31 + raw[i];
            return result;
        }
        else {
            static_assert(std::is_trivially_copyable_v<T> ||
    #if __has_include(<string>)
                            std::is_same_v<T, std::string> ||
    #endif
    #ifdef ARDUINO
                            std::is_same_v<T, String> ||
    #endif
        std::is_same_v<T, const char*> || std::is_same_v<T, char*>,
                "Unsupported type: use integer, float/double, const char*, std::string, Arduino String, or trivially copyable PODs.");
            return 0;
        }
    }
protected:
    // total collisions: 33342   . 510 bytes
    static constexpr uint16_t best_hashers_16[255] = {
        1,3,1,2,12,34,49,127,981,594,2052,1044,49375,53321,10649,380,17924,4814,21417,27973,2711,25859,19375,30550,46560,
        27453,40930,18546,22584,6562,23268,53300,5169,40037,41846,33642,27539,20618,64175,59684,19330,42712,1875,43525,
        64229,36685,20704,31013,9442,25741,38699,30829,1037,43586,12733,27755,61573,48797,42204,31935,63893,11520,24363,
        22963,48454,27302,4153,51261,31542,19673,20041,41237,5395,45652,65105,42390,32730,58752,23485,22238,45897,30628,
        18218,56135,64169,23873,33359,41164,30553,2477,26146,25258,38555,36956,55323,36955,28145,34934,24128,44346,57422,
        17639,10847,14692,58631,62805,44332,23472,30505,42232,45541,28020,27608,47457,7888,22815,33549,56415,36346,1458,
        24626,39447,35548,23130,30783,58784,9345,3842,59278,15268,9092,37766,62289,49252,39060,6744,6888,35294,61301,8810,
        35659,54890,27484,15082,41652,55021,24111,2335,8341,24842,22493,7374,8563,24125,14717,49767,39395,44696,18306,6331,
        60974,28892,34381,22501,47759,10173,19659,58273,56330,31516,39378,4702,55814,58567,26173,4818,19669,63836,59751,
        30066,1339,38164,11732,7403,39225,5556,44476,33594,2491,63186,58885,50149,51242,19350,18232,10553,65382,61292,25227,
        14925,29984,55349,36245,10413,37264,43980,6598,38559,21451,18880,54303,48748,48658,34723,36902,39886,52936,28903,
        13346,6541,14553,59345,4998,45510,62008,16457,47400,9316,21719,13975,36364,17815,4488,40578,7847,14591,1443,35610,
        8353,23187,41174,31424,24346,35663,45976,26208,20988,39438,52284,7982,58000,5705,16935,5340,7};
    // total collisions: 51119    . 255 bytes
    static constexpr uint8_t best_hashers_8[255] = {
        0,0,1,11,58,14,29,239,19,35,233,4,75,31,189,112,193,181,35,4,34,64,183,70,111,124,163,156,230,124,10,199,105,213,
        15,153,125,249,173,42,242,7,25,142,111,19,124,125,243,159,251,76,43,181,114,61,192,214,0,94,182,21,92,221,204,138,
        75,164,162,67,198,72,209,40,223,146,238,27,96,22,207,17,75,234,253,113,145,47,25,79,36,18,108,123,58,34,247,101,
        148,100,179,246,195,8,167,147,127,117,29,191,62,162,80,166,24,190,154,156,42,95,45,66,108,169,197,96,25,241,108,
        54,197,3,98,155,39,24,50,181,3,135,187,59,119,123,164,87,191,151,86,80,122,136,147,39,253,90,223,103,68,10,44,250,
        38,138,173,138,94,231,70,133,37,8,180,32,221,146,126,223,217,150,53,22,49,234,32,132,105,211,9,239,8,197,115,65,
        148,183,186,99,150,13,81,46,218,176,204,228,238,42,157,180,157,43,141,232,140,170,136,109,111,243,45,165,225,222,
        2,42,63,214,146,164,63,162,84,241,222,79,144,42,99,162,131,128,19,166,23,190,16,19,90,161,112,178,58,223,115
    };
    template<typename T>
    [[nodiscard]] inline uint8_t hashFunction(uint8_t TABLE_SIZE, T key, int hash) noexcept {
        size_t tranform_key = preprocess_hash_input(key);

        // return (uint8_t)(djb2Hash(key, hash) % TABLE_SIZE);  // normal hash func
        return static_cast<uint8_t>((hash + tranform_key) % TABLE_SIZE);              // minimal 
        // return (key * 157) % TABLE_SIZE;           // golden ration 
    }   
    [[nodiscard]] inline uint8_t linearProbe(uint8_t TABLE_SIZE,uint8_t index, uint8_t step) noexcept{
        uint16_t sum = index + step;
        if ((TABLE_SIZE & (TABLE_SIZE - 1)) == 0) {
            return sum & (TABLE_SIZE - 1);
        }
        return sum % TABLE_SIZE;
        // return (index + step) % TABLE_SIZE;
    }
    [[nodiscard]] inline uint8_t linearProbe(uint8_t TABLE_SIZE,uint8_t index, uint8_t step) const noexcept{
        uint16_t sum = index + step;
        if ((TABLE_SIZE & (TABLE_SIZE - 1)) == 0) {
            return sum & (TABLE_SIZE - 1);
        }
        return sum % TABLE_SIZE;
        // return (index + step) % TABLE_SIZE;
    }
    
    [[nodiscard]] inline constexpr uint8_t calStep(uint16_t a) noexcept{
        if(a<=10) return 1;
        if(a>10 && a<=20) {
          if(a==14 || a==18) return 5;
          return a/2 + a%2 -1;
        }
        uint8_t b = a /10 - 1;   
        while (b % 10 == 0 || gcd(a, b) > 1) {
            b = b - 1;
        }
        return b;
    }
    [[nodiscard]] inline uint8_t square(uint8_t s) noexcept {
        if(s <= 0) return 0;
        uint8_t x = 1;
        while(x*x <= s){
          x++;
        }
        return x;
    }
    [[nodiscard]] constexpr inline uint8_t  getHasher_8(uint8_t TABLE_SIZE) noexcept{
        return best_hashers_8[TABLE_SIZE-1];
    }
    [[nodiscard]] constexpr inline uint16_t getHasher_16(uint8_t TABLE_SIZE) noexcept{
        return best_hashers_16[TABLE_SIZE-1];
    }
};

enum class slotState : uint8_t {
    Empty    = 0b00,
    Deleted  = 0b01,
    Used     = 0b10
};

class slot_handler {
public:
// private:
    uint8_t* flags = nullptr;
    uint8_t cap_ = 0;

    inline slotState getState(uint8_t index) const noexcept {
        if (index >= cap_) return slotState::Empty;
        uint16_t bitPos = index * 2;
        uint16_t byteIdx = bitPos / 8;
        uint16_t bitOff = bitPos % 8;
        uint8_t mask = 0b11u << bitOff;
        return static_cast<slotState>((flags[byteIdx] & mask) >> bitOff);
    }

    inline void setState(uint8_t index, slotState st) noexcept {
        if (index >= cap_) return;
        uint16_t bitPos = index * 2;
        uint16_t byteIdx = bitPos / 8;
        uint16_t bitOff = bitPos % 8;
        uint8_t clearMask = ~(0b11u << bitOff);
        flags[byteIdx] &= clearMask;
        flags[byteIdx] |= (static_cast<uint8_t>(st) << bitOff);
    }
    
    inline void setState(uint8_t index, slotState st, uint8_t* other_flags){
        if (index >= cap_) return;
        uint16_t bitPos  = index * 2;
        uint16_t byteIdx = bitPos / 8;
        uint16_t bitOff  = bitPos % 8;
        uint8_t clearMask = ~(0b11u << bitOff);
        other_flags[byteIdx] = (other_flags[byteIdx] & clearMask) | (static_cast<uint8_t>(st) << bitOff);
    }

    inline slotState getStateFrom(const uint8_t* f, uint8_t index) const noexcept {
        uint16_t bitPos = index * 2;
        uint16_t byteIdx = bitPos / 8;
        uint16_t bitOff = bitPos % 8;
        uint8_t mask = 0b11u << bitOff;
        return static_cast<slotState>((f[byteIdx] & mask) >> bitOff);
    }

public:
    slot_handler() = default;

    slot_handler(const slot_handler& o) noexcept {
        cap_ = o.cap_;
        size_t bytes = (static_cast<size_t>(cap_) * 2 + 7) / 8;
        flags = new uint8_t[bytes];
        std::memcpy(flags, o.flags, bytes);
    }
    slot_handler& operator=(const slot_handler& o) noexcept {
        if (this != &o) {
            delete[] flags;
            cap_ = o.cap_;
            size_t bytes = (static_cast<size_t>(cap_) * 2 + 7) / 8;
            flags = new uint8_t[bytes];
            std::memcpy(flags, o.flags, bytes);
        }
        return *this;
    }

    // move
    slot_handler(slot_handler&& o) noexcept
      : flags(o.flags), cap_(o.cap_)
    {
        o.flags = nullptr;
        o.cap_  = 0;
    }
    slot_handler& operator=(slot_handler&& o) noexcept {
        if (this != &o) {
            delete[] flags;
            flags    = o.flags;
            cap_     = o.cap_;
            o.flags  = nullptr;
            o.cap_   = 0;
        }
        return *this;
    }
    //destructor 
    ~slot_handler() {
        delete[] flags;
    }

    // Initialize the slot handler with a given capacity and mark all slots as empty
    void slots_init(uint8_t capacity) noexcept {
        // slots_release();
        cap_ = capacity;
        uint16_t byteCount = (static_cast<uint16_t>(cap_) * 2 + 7) / 8;
        flags = new uint8_t[byteCount];
        memset(flags, 0, byteCount);
    }
    // Release resources
    void slots_release() noexcept {
        if(flags != nullptr) {
            delete[] flags;
            flags = nullptr;
        }
        cap_ = 0;
    }
};

