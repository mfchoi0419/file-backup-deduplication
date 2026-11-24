#pragma once

#include <cstdint>
#include <vector>

namespace dedup {

struct ChunkSpan {
    uint64_t offset;   
    uint32_t length;   
};

std::vector<ChunkSpan> wholeFileChunking(uint64_t fileSize);

std::vector<ChunkSpan> fixedChunking(const uint8_t* data, uint64_t size, uint32_t chunkSize = 8192);

}  // namespace dedup
