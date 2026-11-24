# File Backup and Deduplication Tool

EECE 7205 – Iteration 3

This project is a C++ command-line backup tool (dedupctl) designed to reduce redundant storage. It identifies identical files and repeated content by comparing two strategies: Whole-File Deduplication and Fixed-Size Chunking. Content-Defined Chunking (CDC) is planned for Iteration 4.

## 1. Build and Execution

Requirements:
- C++17 compatible compiler (GCC, Clang, or MSVC)
- CMake 3.15+ (optional, for CMake build)

Compilation using g++:
g++ -std=c++17 -Wall -Wextra -O2 -Iinclude src/*.cpp -o dedupctl

Alternatively, using CMake:
cmake -S . -B build
cmake --build build

Usage:
# 1. Generate a synthetic demo corpus (8 files, ~2.75 MiB)
./dedupctl gen corpus

# 2. Compare the two implemented strategies (no disk writes)
./dedupctl compare corpus

# 3. Perform real backup to Content-Addressable Storage (CAS)
./dedupctl backup corpus store

# 4. Restore from CAS and verify data integrity
./dedupctl restore store restored

## 2. Deduplication Strategies

The current prototype implements two approaches:
1. Whole-File Deduplication: Treats the entire file as a single chunk. It computes the SHA-256 hash of the whole file and checks if it already exists in the index. This is efficient for exact duplicates but fails if a file is slightly modified.
2. Fixed-Size Chunking: Divides files into 8192-byte chunks and hashes each chunk independently. This allows the system to reuse identical chunks across different files.

The Boundary Shift Problem: While fixed-size chunking is better than whole-file deduplication, it has a critical flaw. If bytes are inserted or deleted in the middle of a file, all subsequent chunk boundaries shift. This changes the hashes of all following chunks, rendering the index useless for the rest of the file. Iteration 4 will implement CDC to address this.

## 3. Architecture and Source Files

- src/main.cpp: CLI argument parsing and output formatting.
- src/sha256.cpp: Custom implementation of the SHA-256 hashing algorithm.
- src/chunker.cpp: Implementation of whole-file and fixed-size chunking.
- src/hash_index.cpp: Bloom filter, hash table indexing, and file system storage.
- src/dedup_engine.cpp: Pipeline orchestration (read, chunk, hash, store, restore).
- include/*.h: Header files for shared structures and class declarations.

Data Structures:
- Hash Table (std::unordered_map): Maps SHA-256 hashes to chunk metadata (length, refCount). Provides average O(1) lookup time.
- Bloom Filter: Operates before the hash table to quickly reject new chunks without an expensive hash table lookup. Configured for a 1% false positive rate and 0% false negative rate.
- Content-Addressable Storage (CAS): Chunks are stored on disk with their SHA-256 hash as the filename. Manifests record the sequence of chunk hashes for each file to enable reconstruction.

## 4. Complexity Analysis

Let N be total file size, m be the number of chunks, and B be the average chunk size.
- Chunking: Time O(N), Space O(m)
- Hashing: Time O(N) total, Space O(1)
- Index Lookup: Time O(1) average per chunk, Space O(m)
- Backup/Restore: Time O(N), Space O(N) (files loaded in memory)

## 5. Experimental Results

Tested on 8 generated files (2.75 MiB total), including exact copies, insertions, modifications, and random data.

=== Corpus: corpus (8 files) ===

  Strategy                    Logical     Physical    Savings   Total   Unique  AvgSize    Time
  --------------------------------------------------------------------------------------------------
  Whole File                  2.75 MiB    2.25 MiB    18.18%        8        7   360465     18.5ms
  Fixed Chunk (8KiB)          2.75 MiB    1.30 MiB    52.84%      353      167     8169     32.0ms

  Physical usage: WholeFile 2.25 MiB -> Fixed 1.30 MiB

Results:
- Whole-file deduplication saved 18.18%, only catching exact duplicates.
- Fixed-size chunking saved 52.84% by detecting repeated chunks within modified files.

Data Integrity Check:
Restored Files   : 8
SHA-256 Verified : 8
Failed           : 0
[OK] All files are identical to the source (byte-for-byte)

## 6. Iteration 4 Plan

- Implement Content-Defined Chunking (CDC) using a rolling hash (e.g., Gear Hash) to solve the boundary shift problem.
- Evaluate and compare all three strategies based on execution time, storage savings, and index memory.
- Expand test cases to cover more edge cases (e.g., corrupted files, large datasets).
