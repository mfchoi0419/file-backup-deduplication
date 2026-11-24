#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "dedup_engine.h"
#include "sha256.h"

using namespace dedup;

namespace {

std::size_t displayWidth(const std::string& s) {
    std::size_t w = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { w += 1; i += 1; }                 
        else if ((c >> 5) == 0x6) { w += 1; i += 2; }     
        else if ((c >> 4) == 0xE) { w += 2; i += 3; }     
        else { w += 2; i += 4; }                          
    }
    return w;
}

std::string padR(const std::string& s, std::size_t width) {
    std::size_t w = displayWidth(s);
    return w >= width ? s : s + std::string(width - w, ' ');
}

std::string padL(const std::string& s, std::size_t width) {
    std::size_t w = displayWidth(s);
    return w >= width ? s : std::string(width - w, ' ') + s;
}

std::string human(uint64_t bytes) {
    char buf[64];
    const char* unit = "B";
    double v = double(bytes);
    if (v >= 1024) { v /= 1024; unit = "KiB"; }
    if (v >= 1024) { v /= 1024; unit = "MiB"; }
    std::snprintf(buf, sizeof buf, "%.2f %s", v, unit);
    return buf;
}

std::string fixed(double v, int digits = 2) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(digits) << v;
    return os.str();
}

void printUsage() {
    std::cout <<
        "File Backup and Deduplication Tool (dedupctl)\n"
        "\n"
        "Usage:\n"
        "  dedupctl gen <dir>                              Generate demo corpus\n"
        "  dedupctl compare <dir>                          Compare two chunking/dedup strategies\n"
        "  dedupctl backup <src_dir> <store_dir> [--strategy fixed|whole]\n"
        "                                                    Execute backup and write to CAS\n"
        "  dedupctl restore <store_dir> <dest_dir>         Restore and verify SHA-256\n"
        "  dedupctl info <file>                            Print SHA-256 of the file\n";
}

void printResultRow(const StrategyResult& r) {
    std::cout << "  " << padR(r.name, 22)
              << padL(human(r.logicalBytes), 12)
              << padL(human(r.physicalBytes), 12)
              << padL(fixed(r.savedPercent()) + "%", 10)
              << padL(std::to_string(r.totalUnits), 9)
              << padL(std::to_string(r.uniqueUnits), 9)
              << padL(fixed(r.avgChunk(), 0), 9)
              << padL(fixed(r.elapsedMs, 1) + "ms", 11)
              << "\n";
}

void printTableHeader() {
    std::cout << "  " << padR("Strategy", 22) << padL("Logical", 12) << padL("Physical", 12)
              << padL("Savings", 10) << padL("Total", 9) << padL("Unique", 9)
              << padL("AvgSize", 9) << padL("Time", 11) << "\n";
    std::cout << "  " << std::string(98, '-') << "\n";
}

int cmdGen(const std::string& dir) {
    std::filesystem::path p(dir);
    DedupEngine::generateCorpus(p);
    std::size_t count = 0;
    uint64_t total = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(p)) {
        if (e.is_regular_file()) { count++; total += e.file_size(); }
    }
    std::cout << "[OK] Generated " << count << " files in " << p.string() 
              << ", total " << human(total) << "\n";
    std::cout << "     Includes: exact copies / insertions / partial modifications / log increments / random files\n";
    return 0;
}

int cmdCompare(const std::string& dir) {
    auto corpus = DedupEngine::loadCorpus(dir);
    if (corpus.empty()) {
        std::cout << "[Error] Directory is empty or does not exist: " << dir << "\n";
        return 1;
    }

    std::cout << "\n=== Corpus: " << dir << " (" << corpus.size() << " files) ===\n\n";
    printTableHeader();

    std::vector<StrategyResult> results;
    results.push_back(DedupEngine::simulate(corpus, Strategy::WholeFile));
    printResultRow(results.back());
    results.push_back(DedupEngine::simulate(corpus, Strategy::FixedChunk));
    printResultRow(results.back());

    const StrategyResult& whole = results[0];
    const StrategyResult& fixedR = results[1];

    std::cout << "\n  ---- Conclusion ----\n";
    std::cout << "  1) Whole-file dedup only detects exact file matches and fails after a 137-byte insertion.\n";
    std::cout << "  2) Fixed-size chunking is finer than whole-file, but insertions cause all subsequent boundaries to shift,\n"
                 "     changing the hashes of all following chunks (boundary shift problem).\n";
    std::cout << "  Physical usage: WholeFile " << human(whole.physicalBytes)
              << " -> Fixed " << human(fixedR.physicalBytes) << "\n\n";
    return 0;
}

int cmdBackup(const std::string& src, const std::string& store, Strategy s) {
    auto corpus = DedupEngine::loadCorpus(src);
    if (corpus.empty()) {
        std::cout << "[Error] Source directory is empty or does not exist: " << src << "\n";
        return 1;
    }
    StrategyResult r = DedupEngine::backup(corpus, s, store);

    std::cout << "\n=== Backup Completed ===\n\n";
    printTableHeader();
    printResultRow(r);
    std::cout << "\n  Source Dir    : " << src << "\n";
    std::cout << "  Store Dir     : " << store << "\n";
    std::cout << "  Manifest Files: " << corpus.size() << "\n";
    std::cout << "  Logical Total : " << human(r.logicalBytes) << " ("
              << r.logicalBytes << " bytes)\n";
    if (r.physicalBytes == 0) {
        std::cout << "  Physical Write: 0 B (" << r.totalUnits
                  << " chunks all hit existing storage, no disk writes were generated)\n";
        std::cout << "  Savings       : " << human(r.logicalBytes) << ", which is 100%\n";
    } else {
        std::cout << "  Physical Write: " << human(r.physicalBytes) << " ("
                  << r.physicalBytes << " bytes)\n";
        std::cout << "  Savings       : " << human(r.logicalBytes - r.physicalBytes) << ", savings rate "
                  << fixed(r.savedPercent()) << "%, dedup factor " << fixed(r.dedupFactor()) << "x\n";
    }
    std::cout << "  Bloom Filter  : Rejected " << r.bloomRejects << " hash map probes early\n\n";
    return 0;
}

int cmdRestore(const std::string& store, const std::string& dest) {
    RestoreResult rr = DedupEngine::restore(store, dest);
    std::cout << "\n=== Restore Completed ===\n";
    std::cout << "  Restored Files   : " << rr.restored << "\n";
    std::cout << "  SHA-256 Verified : " << rr.verified << "\n";
    std::cout << "  Failed           : " << rr.failed << "\n";
    std::cout << "  Output Dir       : " << dest << "\n";
    std::cout << (rr.failed == 0 && rr.verified == rr.restored
                      ? "  [OK] All files are identical to the source (byte-for-byte)\n"
                      : "  [Warning] Some files failed verification\n");
    return rr.failed == 0 ? 0 : 1;
}

}  

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) { printUsage(); return 0; }

    const std::string cmd = argv[1];

    if (cmd == "gen" && argc >= 3) 
		return cmdGen(argv[2]);

    if (cmd == "compare" && argc >= 3) 
		return cmdCompare(argv[2]);

    if (cmd == "backup" && argc >= 4) {
        Strategy s = Strategy::FixedChunk;   
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
                if (!parseStrategy(argv[i + 1], s)) {
                    std::cout << "[Error] Unknown strategy, available: fixed / whole\n";
                    return 1;
                }
                ++i;   
            }
        }
        return cmdBackup(argv[2], argv[3], s);
    }

    if (cmd == "restore" && argc >= 4) return cmdRestore(argv[2], argv[3]);

    if (cmd == "info" && argc >= 3) {
        auto corpus = DedupEngine::loadCorpus(std::filesystem::path(argv[2]).parent_path());
        for (const auto& f : corpus) {
            if (f.relPath == std::filesystem::path(argv[2]).filename().generic_string()) {
                std::cout << "SHA-256: " << hashHex(f.data.data(), f.data.size()) << "\n";
                std::cout << "Size   : " << human(f.data.size()) << "\n";
                return 0;
            }
        }
        std::cout << "[Error] Failed to read file\n";
        return 1;
    }

    printUsage();
    return 1;
}