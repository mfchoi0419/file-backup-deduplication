cmake_minimum_required(VERSION 3.15)
project(file_dedup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(dedupctl
    src/main.cpp
    src/sha256.cpp
    src/chunker.cpp
    src/hash_index.cpp
    src/dedup_engine.cpp
)

# Older versions of GCC / Clang require explicit linking of stdc++fs
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(dedupctl PRIVATE stdc++fs)
endif()

if (MSVC)
    target_compile_options(dedupctl PRIVATE /W4 /utf-8)
else()
    target_compile_options(dedupctl PRIVATE -Wall -Wextra -O2)
endif()
