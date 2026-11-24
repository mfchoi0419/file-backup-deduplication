#include "chunker.h"

namespace dedup {

std::vector<ChunkSpan> wholeFileChunking(uint64_t fileSize) {
    std::vector<ChunkSpan> spans;
    if (fileSize == 0) 
		return spans;
    const uint64_t kMaxLen = 0xFFFFFFFFull;
    uint64_t off = 0;
    while (off < fileSize) {
        uint64_t len = fileSize - off;
        if (len > kMaxLen) 
			len = kMaxLen;
        spans.push_back({off, static_cast<uint32_t>(len)});
        off += len;
    }
    return spans;
}

std::vector<ChunkSpan> fixedChunking(const uint8_t* data, uint64_t size, uint32_t chunkSize) {
    (void)data;                       
    std::vector<ChunkSpan> spans;
    if (chunkSize == 0) 
		chunkSize = 8192;
    spans.reserve(static_cast<std::size_t>(size / chunkSize + 1));   
    for (uint64_t off = 0; off < size; off += chunkSize) {
        uint64_t len = size - off;
        if (len > chunkSize) 
			len = chunkSize;
        spans.push_back({off, static_cast<uint32_t>(len)});
    }
    return spans;
}

}  // namespace dedup