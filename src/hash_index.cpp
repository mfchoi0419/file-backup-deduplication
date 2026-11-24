#include "hash_index.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dedup {
namespace {

inline uint64_t fnv1a(const std::string& s, uint64_t seed) {
    uint64_t h = seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001B3ull;   
    }
    return h ^ (h >> 33);        
}

}  

BloomFilter::BloomFilter(std::size_t expectedItems, double falsePositiveRate) {
    if (expectedItems < 16) expectedItems = 16;      
    double ln2 = std::log(2.0);
    double m = -double(expectedItems) * std::log(falsePositiveRate) / (ln2 * ln2);
    bitCount_ = static_cast<std::size_t>(m) + 1;     
    k_ = static_cast<std::size_t>(std::llround(double(bitCount_) / double(expectedItems) * ln2));
    if (k_ < 1) k_ = 1;
    if (k_ > 16) k_ = 16;                            
    bits_.assign((bitCount_ + 7) / 8, 0);            
}

std::pair<uint64_t, uint64_t> BloomFilter::probe(const HashKey& key) const {
    uint64_t a = fnv1a(key, 0xcbf29ce484222325ull);
    uint64_t b = fnv1a(key, 0x84222325cbf29ce4ull) | 1ull;  
    return {a, b};
}

void BloomFilter::add(const HashKey& key) {
    auto [a, b] = probe(key);
    for (std::size_t i = 0; i < k_; ++i) {
        std::size_t bit = static_cast<std::size_t>((a + i * b) % bitCount_);
        bits_[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));   
    }
}

bool BloomFilter::maybeContains(const HashKey& key) const {
    auto [a, b] = probe(key);
    for (std::size_t i = 0; i < k_; ++i) {
        std::size_t bit = static_cast<std::size_t>((a + i * b) % bitCount_);
        if ((bits_[bit >> 3] & static_cast<uint8_t>(1u << (bit & 7))) == 0) return false;
    }
    return true;   
}

ChunkStore::ChunkStore(std::filesystem::path root, bool dryRun)
    : root_(std::move(root)), dryRun_(dryRun), bloom_(1u << 17, 0.01) {
    if (!dryRun_) {
        std::filesystem::create_directories(root_ / "objects");
        std::filesystem::create_directories(root_ / "manifests");
        loadExistingObjects();   
    }
}

void ChunkStore::loadExistingObjects() {
    std::filesystem::path dir = root_ / "objects";
    if (!std::filesystem::exists(dir)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() != 64) continue;   

        uint64_t len = static_cast<uint64_t>(entry.file_size());
        index_.emplace(name, ChunkMeta{static_cast<uint32_t>(len), 0});
        bloom_.add(name);                  
        uniqueBytes_ += len;
    }
}

std::filesystem::path ChunkStore::objectPath(const HashKey& key) const {
    return root_ / "objects" / key.substr(0, 2) / key;
}

std::filesystem::path ChunkStore::manifestPath(const std::string& relPath) const {
    HashKey h = hashHex(relPath.data(), relPath.size());
    return root_ / "manifests" / (h.substr(0, 32) + ".mf");
}

bool ChunkStore::hasChunk(const HashKey& key) const {
    return index_.find(key) != index_.end();
}

bool ChunkStore::put(const uint8_t* data, uint32_t length, const HashKey& key, StoreStats& stats) {
    stats.chunksSeen++;
    stats.bytesSeen += length;

    if (!bloom_.maybeContains(key)) {
        stats.bloomRejects++;      
    } else {
        stats.mapProbes++;
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second.refCount++;
            return false;   
        }
    }

    index_.emplace(key, ChunkMeta{length, 1});   
    bloom_.add(key);                             
    uniqueBytes_ += length;
    stats.chunksNew++;
    stats.bytesNew += length;

    if (!dryRun_) {
        std::filesystem::path p = objectPath(key);
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary);
        if (!out) throw std::runtime_error("Failed to write object file: " + p.string());
        out.write(reinterpret_cast<const char*>(data), length);
    }
    return true;
}

bool ChunkStore::get(const HashKey& key, std::vector<uint8_t>& out) const {
    std::ifstream in(objectPath(key), std::ios::binary);
    if (!in) return false;                          
    in.seekg(0, std::ios::end);
    std::streamoff len = in.tellg();
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(len));
    if (len > 0) in.read(reinterpret_cast<char*>(out.data()), len);
    return static_cast<bool>(in);
}

void ChunkStore::writeManifest(const Manifest& m) {
    if (dryRun_) return;
    std::filesystem::path p = manifestPath(m.path);
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("无法写入清单: " + p.string());
    out << "MANIFEST v1\n";          
    out << "path=" << m.path << "\n";
    out << "size=" << m.size << "\n";
    out << "hash=" << m.wholeHash << "\n";
    for (const ChunkRef& c : m.chunks) {
        out << "chunk " << c.hash << ' ' << c.length << "\n";   
    }
}

std::vector<Manifest> ChunkStore::loadAllManifests() const {
    std::vector<Manifest> result;
    std::filesystem::path dir = root_ / "manifests";
    if (!std::filesystem::exists(dir)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream in(entry.path());
        if (!in) continue;

        Manifest m;
        std::string line;
        std::getline(in, line);            
        while (std::getline(in, line)) {
            if (line.rfind("path=", 0) == 0) {
                m.path = line.substr(5);
            } else if (line.rfind("size=", 0) == 0) {
                m.size = std::stoull(line.substr(5));
            } else if (line.rfind("hash=", 0) == 0) {
                m.wholeHash = line.substr(5);
            } else if (line.rfind("chunk ", 0) == 0) {
                std::istringstream ss(line.substr(6));
                ChunkRef c;
                ss >> c.hash >> c.length;
                m.chunks.push_back(c);
            }
        }
        if (entry.path().filename() != manifestPath(m.path).filename()) continue;

        result.push_back(std::move(m));
    }
    return result;
}

std::size_t ChunkStore::indexMemoryBytes() const {
    const std::size_t perEntry = sizeof(std::pair<const HashKey, ChunkMeta>) + 64 + 2 * sizeof(void*);
    return index_.size() * perEntry + bloom_.memoryBytes();
}

}  // namespace dedup