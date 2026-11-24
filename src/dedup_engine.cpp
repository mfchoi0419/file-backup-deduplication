#include "dedup_engine.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>

#include "sha256.h"

namespace dedup {
namespace {

struct Rng {
    uint64_t x;
    explicit Rng(uint64_t seed) : x(seed) {}
    uint64_t next() {
        x += 0x9E3779B97F4A7C15ull;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint8_t byte() { return static_cast<uint8_t>(next() & 0xFF); }
};

void writeFile(const std::filesystem::path& p, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("Write failed: " + p.string());
    if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

std::vector<uint8_t> structuredBlob(Rng& rng, std::size_t size) {
    std::vector<uint8_t> out;
    out.reserve(size);
    while (out.size() < size) {
        uint64_t r = rng.next();
        if (out.size() > 4096 && (r & 3) == 0) {
            std::size_t len = 64 + (rng.next() % 256);
            if (len > out.size()) len = out.size();
            std::size_t pos = rng.next() % (out.size() - len + 1);
            std::vector<uint8_t> piece(out.begin() + pos, out.begin() + pos + len);
            for (uint8_t b : piece) {
                if (out.size() >= size) break;
                out.push_back(b);
            }
        } else {
            std::size_t len = 32 + (rng.next() % 96);
            for (std::size_t i = 0; i < len && out.size() < size; ++i) {
                out.push_back(static_cast<uint8_t>(' ' + (rng.next() % 95)));
            }
            out.push_back('\n');
        }
    }
    out.resize(size);
    return out;
}

}  

std::string strategyName(Strategy s) {
    switch (s) {
        case Strategy::WholeFile: return "Whole File";
        case Strategy::FixedChunk: return "Fixed Chunk (8KiB)";
    }
    return "Unknown";
}

bool parseStrategy(const std::string& text, Strategy& out) {
    if (text == "whole") { out = Strategy::WholeFile; return true; }
    if (text == "fixed") { out = Strategy::FixedChunk; return true; }
    return false;
}

std::vector<FileData> DedupEngine::loadCorpus(const std::filesystem::path& dir) {
    std::vector<FileData> corpus;
    if (!std::filesystem::exists(dir)) 
		return corpus;

    std::vector<std::filesystem::path> paths;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.is_regular_file()) paths.push_back(e.path());
    }
    std::sort(paths.begin(), paths.end());   

    for (const auto& p : paths) {
        std::ifstream in(p, std::ios::binary);
        if (!in) 
			continue;
        FileData fd;
        fd.relPath = std::filesystem::relative(p, dir).generic_string();
        in.seekg(0, std::ios::end);
        std::streamoff len = in.tellg();
        in.seekg(0, std::ios::beg);
        fd.data.resize(static_cast<std::size_t>(len));
        if (len > 0) 
			in.read(reinterpret_cast<char*>(fd.data.data()), len);
        corpus.push_back(std::move(fd));
    }
    return corpus;
}

StrategyResult DedupEngine::run(const std::vector<FileData>& corpus, Strategy s,
                                ChunkStore& store, bool writeManifest) {
    auto t0 = std::chrono::steady_clock::now();
    StoreStats stats;

    for (const FileData& f : corpus) {
        const uint8_t* base = f.data.data();
        const uint64_t size = f.data.size();

        std::vector<ChunkSpan> spans;
        switch (s) {
            case Strategy::WholeFile: spans = wholeFileChunking(size); break;
            case Strategy::FixedChunk: spans = fixedChunking(base, size, 8192); break;
        }

        Manifest m;
        m.path = f.relPath;
        m.size = size;
        m.wholeHash = hashHex(base, static_cast<std::size_t>(size));

        for (const ChunkSpan& sp : spans) {
            HashKey key = hashHex(base + sp.offset, sp.length);
            store.put(base + sp.offset, sp.length, key, stats);   
            m.chunks.push_back(ChunkRef{key, sp.length});         
        }

        if (writeManifest) 
			store.writeManifest(m);
    }

    auto t1 = std::chrono::steady_clock::now();

    StrategyResult r;
    r.name = strategyName(s);
    r.logicalBytes = stats.bytesSeen;
    r.physicalBytes = stats.bytesNew;
    r.totalUnits = stats.chunksSeen;
    r.uniqueUnits = stats.chunksNew;
    r.bloomRejects = stats.bloomRejects;
    r.indexMemory = store.indexMemoryBytes();
    r.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

StrategyResult DedupEngine::simulate(const std::vector<FileData>& corpus, Strategy s) 
{
    ChunkStore store(std::filesystem::path("."), true);
    return run(corpus, s, store, false);
}

StrategyResult DedupEngine::backup(const std::vector<FileData>& corpus, Strategy s,
                                   const std::filesystem::path& storeDir) {
    ChunkStore store(storeDir, false);
    return run(corpus, s, store, true);
}

RestoreResult DedupEngine::restore(const std::filesystem::path& storeDir,
                                   const std::filesystem::path& destDir) {
    RestoreResult res;
    ChunkStore store(storeDir, true);          
    std::vector<Manifest> manifests = store.loadAllManifests();

    for (const Manifest& m : manifests) {
        std::filesystem::path outPath = destDir / std::filesystem::path(m.path);
        std::filesystem::create_directories(outPath.parent_path());

        std::vector<uint8_t> content;
        content.reserve(static_cast<std::size_t>(m.size));
        bool ok = true;
        for (const ChunkRef& c : m.chunks) {
            std::vector<uint8_t> block;
            if (!store.get(c.hash, block)) { ok = false; break; }   
            content.insert(content.end(), block.begin(), block.end());
        }

        if (!ok) { res.failed++; continue; }
        writeFile(outPath, content);
        res.restored++;

        if (hashHex(content.data(), content.size()) == m.wholeHash) res.verified++;
        else res.failed++;
    }
    return res;
}

void DedupEngine::generateCorpus(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    Rng rng(20261023);   

    std::vector<uint8_t> base = structuredBlob(rng, 512 * 1024);
    writeFile(dir / "report_v1.bin", base);

    writeFile(dir / "report_v1_copy.bin", base);

    std::vector<uint8_t> inserted = base;
    std::vector<uint8_t> payload;
    for (int i = 0; i < 137; ++i) payload.push_back(rng.byte());
    inserted.insert(inserted.begin() + inserted.size() / 2, payload.begin(), payload.end());
    writeFile(dir / "report_v2_inserted.bin", inserted);

    std::vector<uint8_t> patched = base;
    for (int k = 0; k < 5; ++k) {
        std::size_t pos = (base.size() / 6) * (k + 1) + 11;
        for (int i = 0; i < 40 && pos + i < patched.size(); ++i) patched[pos + i] = rng.byte();
    }
    writeFile(dir / "report_v3_patched.bin", patched);

    std::vector<uint8_t> log1 = structuredBlob(rng, 256 * 1024);
    std::vector<uint8_t> log2 = log1;
    for (std::size_t i = log2.size() / 3; i < log2.size() / 3 + 600 && i < log2.size(); ++i) {
        log2[i] = rng.byte();
    }
    writeFile(dir / "logs/day1.log", log1);
    writeFile(dir / "logs/day2.log", log2);

    std::vector<uint8_t> r1, r2;
    for (int i = 0; i < 128 * 1024; ++i) r1.push_back(rng.byte());
    for (int i = 0; i < 128 * 1024; ++i) r2.push_back(rng.byte());
    writeFile(dir / "random/a.bin", r1);
    writeFile(dir / "random/b.bin", r2);
}

}  // namespace dedup