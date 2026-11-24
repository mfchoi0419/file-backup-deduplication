#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dedup {

using Digest = std::array<uint8_t, 32>;
using HashKey = std::string;

class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t len);

    Digest finalize();

    static Digest hash(const void* data, std::size_t len);
    static HashKey toHex(const Digest& d);

private:
    void transform(const uint8_t* block);

    uint32_t h_[8];        
    uint8_t buffer_[64];   
    std::size_t bufferLen_;
    uint64_t totalBits_;   
};

inline HashKey hashHex(const void* data, std::size_t len) {
    return Sha256::toHex(Sha256::hash(data, len));
}

}  // namespace dedup