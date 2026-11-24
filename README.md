# File Backup and Deduplication Tool

**EECE 7205 – Iteration 3**

## Project Overview

A C++ application designed to identify duplicate files and estimate potential storage savings. The project explores two deduplication approaches: whole-file deduplication and chunk-based deduplication.

The current prototype implements whole-file deduplication, identifying files with identical contents and calculating estimated storage savings without modifying or deleting files.

## Requirements

- C++ compiler (GCC/G++)
- Terminal or command-line interface
- C++11 or later
- No external libraries required

## Compilation and Execution

The program prompts the user for the number of files to process and the filename or path of each file. Files in the `test_files/` directory can be entered using their paths from the project.

## Testing

Sample text files used for preliminary testing are located in the `test_files/` directory.

Tests include:
- Identical files with different filenames.
- Files with different contents.
- Multiple duplicate files.
- Empty files.
- Invalid filenames.

[View Test Files](https://github.com/mfchoi0419/file-backup-deduplication/tree/feature/whole-file-deduplication/test_files)

## Current Progress

| Feature | Status |
|---|---|
| File reading and input | Complete |
| Whole-file deduplication | Complete |
| Storage savings calculation | Complete |
| Preliminary testing | Complete |
| Chunk-based deduplication | Planned |
| Execution-time measurement | Planned |
| Memory usage evaluation | Planned |
| Algorithm comparison | Planned |

### Known Limitations

- Files must be entered individually; automatic directory scanning is not implemented.
- Only whole-file deduplication is currently supported.
- Complete file contents are stored in memory.
- Performance measurements and comparisons have not yet been implemented.
- Input validation and error handling require further refinement.
