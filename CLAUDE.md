# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**shmap** is a C++17 library providing lock-free concurrent data structures designed for shared memory (shm) environments. The project focuses on high-performance, thread-safe data structures that can be used across multiple processes.

## Build System and Development Workflow

### Build Commands

The project uses CMake with a custom build script `ccup.sh` for development tasks:

```bash
# Make script executable
chmod a+x ./ccup.sh

# Start project in Docker environment
./ccup.sh -e

# Update dependencies and generate makefile
./ccup.sh -u

# Build project
./ccup.sh -b

# Run tests
./ccup.sh -t

# Update & build & test
./ccup.sh -ubt

# Clean build
./ccup.sh -c

# Clean all (build + dependencies)
./ccup.sh -C
```

### CMake Configuration

Key CMake options:
- `ENABLE_UT=ON` - Build unit tests (default: ON)
- `ENABLE_BT=OFF` - Build benchmark tests (default: OFF)
- `ENABLE_TSAN=ON` - Enable ThreadSanitizer (default: ON)
- `ENABLE_ASON=OFF` - Enable AddressSanitizer (default: OFF)

### Development Environment

- **Language**: C++17
- **Build System**: CMake 3.10+
- **Testing**: Google Test (gtest)
- **Benchmarking**: Google Benchmark
- **Container**: Docker-based development environment using `magicbowen/ubuntu-cc-dev:v2`

## Architecture and Key Components

### Core Data Structures

1. **ShmHashTable** (`include/shmap/shm_hash_table.h`)
   - Lock-free closed hashing table
   - Template-based with configurable capacity, hash, and equality functions
   - Supports concurrent access with state machine (EMPTY, INSERTING, READY, ACCESSING)
   - Visitor pattern for safe concurrent modifications

2. **ShmRingBuffer** (`include/shmap/shm_ring_buffer.h`)
   - Multiple ring buffer implementations:
     - **SPSC** (Single Producer Single Consumer)
     - **SPMC** (Single Producer Multiple Consumers)
     - **BroadcastRingBuffer** (All consumers fetch data successfully)
   - Lock-free implementations using atomic operations
   - Power-of-two capacity for efficient modulo operations

3. **ShmStorage** (`include/shmap/shm_storage.h`)
   - POSIX shared memory wrapper for data structures
   - Singleton pattern for shared memory access
   - Automatic memory management and cleanup

### Supporting Components

- **Status** (`include/shmap/status.h`) - Error handling with status codes
- **Backoff** (`include/shmap/backoff.h`) - Exponential backoff for lock-free algorithms
- **ShmVector** (`include/shmap/shm_vector.h`) - Shared memory vector
- **FixedString** (`include/shmap/fixed_string.h`) - Fixed-size string for shared memory
- **BitsInteger** (`include/shmap/bits_integer.h`) - Bit manipulation utilities

### Design Patterns

- **Visitor Pattern**: Used extensively in `ShmHashTable` for safe concurrent access
- **Singleton Pattern**: Used in `ShmStorage` for shared memory management
- **Template-based Design**: All data structures are template-based for type safety

## Testing Structure

### Unit Tests (`test/ut/`)
- `test_shm_table.cc` - Hash table functionality
- `test_shm_storage.cc` - Shared memory storage
- `test_shm_ring_buffer.cc` - Ring buffer implementations
- `test_backoff.cc` - Backoff algorithm
- Multi-threaded (`_mt.cc`) and multi-process (`_mp.cc`) test variants

### Benchmark Tests (`test/bt/`)
- Performance testing using Google Benchmark

## Key Implementation Details

### Memory Alignment
- All data structures use `alignas(CACHE_LINE_SIZE)` to prevent false sharing
- Cache line size detection using `std::hardware_destructive_interference_size`

### Lock-Free Algorithms
- Atomic operations with appropriate memory ordering
- State machines for safe concurrent access
- Exponential backoff for contention management

### Shared Memory Requirements
- All stored types must be `trivially_copyable` and `standard_layout`
- POSIX shared memory API for cross-process communication

## Development Guidelines

### Code Style
- Header-only library design
- Extensive use of templates and constexpr
- Clear namespace organization in `shmap::`
- Comprehensive error handling with `Status` type

### Testing Strategy
- Unit tests for single-threaded functionality
- Multi-threaded tests for concurrent access patterns
- Multi-process tests for shared memory scenarios
- ThreadSanitizer enabled by default for concurrency testing

### Performance Considerations
- Cache-line alignment for all shared data
- Lock-free algorithms to minimize contention
- Template-based design for compile-time optimization
- Power-of-two capacities for efficient modulo operations