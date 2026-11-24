#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "chunker.h"
#include "hash_index.h"

namespace dedup {

enum class Strategy { WholeFile, FixedChunk };

std::string strategyName(Strategy s);                    
bool parseStrategy(const std::string& text, Strategy& out);   

struct FileData {
    std::string relPath;             
    std::vector<uint8_t> data;
};

struct StrategyResult {
    std::string name;
    uint64_t logicalBytes = 0;   
    uint64_t physicalBytes = 0;  
    uint64_t totalUnits = 0;     
    uint64_t uniqueUnits = 0;    
    uint64_t bloomRejects = 0;   
    std::size_t indexMemory = 0; 
    double elapsedMs = 0.0;

    double savedPercent() const {
        return logicalBytes ? 100.0 * (1.0 - double(physicalBytes) / double(logicalBytes)) : 0.0;
    }
    double dedupFactor() const {
        return physicalBytes ? double(logicalBytes) / double(physicalBytes) : 1.0;
    }
    double avgChunk() const { return totalUnits ? double(logicalBytes) / double(totalUnits) : 0.0; }
};

struct RestoreResult {
    std::size_t restored = 0;
    std::size_t verified = 0;
    std::size_t failed = 0;
};

class DedupEngine {
public:
    static std::vector<FileData> loadCorpus(const std::filesystem::path& dir);

    static StrategyResult simulate(const std::vector<FileData>& corpus, Strategy s);

    static StrategyResult backup(const std::vector<FileData>& corpus, Strategy s,
                                 const std::filesystem::path& storeDir);

    static RestoreResult restore(const std::filesystem::path& storeDir,
                                 const std::filesystem::path& destDir);

    static void generateCorpus(const std::filesystem::path& dir);

private:
    static StrategyResult run(const std::vector<FileData>& corpus, Strategy s,
                              ChunkStore& store, bool writeManifest);
};

}  // namespace dedup