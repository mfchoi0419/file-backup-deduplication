#include "sha256.h"

#include <cstring>

namespace dedup {
namespace {

constexpr uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline uint32_t loadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline void storeBE32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

}  

Sha256::Sha256()
    : h_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
      bufferLen_(0),
      totalBits_(0) {
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::transform(const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = loadBE32(block + 4 * i);

    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);  
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);   
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);   
        uint32_t ch = (e & f) ^ ((~e) & g);                     
        uint32_t t1 = h + S1 + ch + K[i] + w[i];                
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);   
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);             
        uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    totalBits_ += static_cast<uint64_t>(len) * 8u;   

    if (bufferLen_ > 0) {
        std::size_t need = 64 - bufferLen_;
        std::size_t take = len < need ? len : need;
        std::memcpy(buffer_ + bufferLen_, p, take);
        bufferLen_ += take;
        p += take;
        len -= take;
        if (bufferLen_ < 64) return;   
        transform(buffer_);
        bufferLen_ = 0;
    }

    while (len >= 64) {
        transform(p);
        p += 64;
        len -= 64;
    }

    if (len > 0) {
        std::memcpy(buffer_, p, len);
        bufferLen_ = len;
    }
}

Digest Sha256::finalize() {
    uint64_t bits = totalBits_;
    uint8_t pad[72];              
    std::size_t padLen = 0;
    pad[padLen++] = 0x80;
    std::size_t remain = (bufferLen_ + 1) % 64;
    std::size_t zeros = (remain <= 56) ? (56 - remain) : (120 - remain);
    for (std::size_t i = 0; i < zeros; ++i) pad[padLen++] = 0x00;
    for (int i = 7; i >= 0; --i) pad[padLen++] = uint8_t((bits >> (i * 8)) & 0xFF);   

    uint8_t block[64];
    std::size_t bi = 0;
    for (std::size_t i = 0; i < bufferLen_; ++i) {
        block[bi++] = buffer_[i];
        if (bi == 64) { transform(block); bi = 0; }
    }
    for (std::size_t i = 0; i < padLen; ++i) {
        block[bi++] = pad[i];
        if (bi == 64) { transform(block); bi = 0; }
    }

    Digest out{};
    for (int i = 0; i < 8; ++i) storeBE32(out.data() + 4 * i, h_[i]);
    return out;
}

Digest Sha256::hash(const void* data, std::size_t len) {
    Sha256 ctx;
    ctx.update(data, len);
    return ctx.finalize();
}

HashKey Sha256::toHex(const Digest& d) {
    static const char* digits = "0123456789abcdef";
    HashKey s;
    s.resize(64);
    for (std::size_t i = 0; i < d.size(); ++i) {
        s[2 * i] = digits[d[i] >> 4];
        s[2 * i + 1] = digits[d[i] & 0x0F];
    }
    return s;
}

}  // namespace dedup
