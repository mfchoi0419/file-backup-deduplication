#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sha256.h"

namespace dedup {

class BloomFilter {
public:
    explicit BloomFilter(std::size_t expectedItems, double falsePositiveRate = 0.01);

    void add(const HashKey& key);
    bool maybeContains(const HashKey& key) const;

    std::size_t bitCount() const { return bitCount_; }
    std::size_t hashCount() const { return k_; }
    std::size_t memoryBytes() const { return bits_.size(); }

private:
    std::pair<uint64_t, uint64_t> probe(const HashKey& key) const;

    std::vector<uint8_t> bits_;   
    std::size_t bitCount_;
    std::size_t k_;               
};

struct ChunkMeta {
    uint32_t length = 0;      
    uint32_t refCount = 0;    
};

struct StoreStats {
    uint64_t chunksSeen = 0;    
    uint64_t chunksNew = 0;     
    uint64_t bytesSeen = 0;     
    uint64_t bytesNew = 0;      
    uint64_t bloomRejects = 0;  
    uint64_t mapProbes = 0;     

    double dedupRatio() const { return bytesSeen ? double(bytesNew) / double(bytesSeen) : 1.0; }
};

struct ChunkRef {
    HashKey hash;
    uint32_t length = 0;
};

struct Manifest {
    std::string path;        
    uint64_t size = 0;       
    HashKey wholeHash;       
    std::vector<ChunkRef> chunks;
};

class ChunkStore {
public:
    explicit ChunkStore(std::filesystem::path root, bool dryRun = false);

    bool put(const uint8_t* data, uint32_t length, const HashKey& key, StoreStats& stats);
    bool get(const HashKey& key, std::vector<uint8_t>& out) const;
    bool hasChunk(const HashKey& key) const;

    void writeManifest(const Manifest& m);
    std::vector<Manifest> loadAllManifests() const;

    std::size_t uniqueChunks() const { return index_.size(); }
    uint64_t uniqueBytes() const { return uniqueBytes_; }
    const StoreStats& stats() const { return stats_; }
    std::filesystem::path root() const { return root_; }

    std::size_t indexMemoryBytes() const;

private:
    std::filesystem::path objectPath(const HashKey& key) const;

    std::filesystem::path manifestPath(const std::string& relPath) const;

    void loadExistingObjects();

    std::filesystem::path root_;
    bool dryRun_;
    std::unordered_map<HashKey, ChunkMeta> index_;  
    BloomFilter bloom_;
    uint64_t uniqueBytes_ = 0;
    StoreStats stats_;
};

}  // namespace dedup