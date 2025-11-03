# Development Guide

## Build System

shmap uses CMake with a custom build script (`ccup.sh`) for development tasks.

### Prerequisites

- **C++17 Compiler** (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.10+**
- **Docker** (for development environment)
- **POSIX Shared Memory** support

### Build Commands

```bash
# Make script executable
chmod a+x ./ccup.sh

# Common development workflows
./ccup.sh -ubt    # Update, build, and test
./ccup.sh -bt     # Build and test
./ccup.sh -t      # Run tests only
```

### Complete Command Reference

```bash
# Start project in Docker environment
./ccup.sh -e

# Update dependencies and generate makefile
./ccup.sh -u

# Format code
./ccup.sh -f

# Build project
./ccup.sh -b

# Run tests
./ccup.sh -t

# Install project
./ccup.sh -i

# Run executable
./ccup.sh -r

# Generate documentation
./ccup.sh -d

# Clean build
./ccup.sh -c

# Clean all (build + dependencies)
./ccup.sh -C

# Show help
./ccup.sh -h
```

## CMake Configuration

### Key Options

```cmake
# Enable/disable unit tests (default: ON)
option(ENABLE_UT "Build unit tests" ON)

# Enable/disable benchmark tests (default: OFF)
option(ENABLE_BT "Build bench tests" OFF)

# Enable ThreadSanitizer (default: ON)
option(ENABLE_TSAN "Enable ThreadSanitizer" ON)

# Enable AddressSanitizer (default: OFF)
option(ENABLE_ASON "Enable AddressSanitizer" OFF)
```

### Build Types

- **Debug**: `-g -fno-inline` with debug logging enabled
- **Release**: `-O2` optimizations for benchmarks
- **Sanitized**: ThreadSanitizer and AddressSanitizer support

## Testing Strategy

### Test Organization

```
test/
├── ut/           # Unit tests
│   ├── test_*.cc        # Single-threaded tests
│   ├── test_*_mt.cc     # Multi-threaded tests
│   └── test_*_mp.cc     # Multi-process tests
├── bt/           # Benchmark tests
└── fixture/      # Test utilities
```

### Running Tests

```bash
# Run all tests
./ccup.sh -t

# Run specific test
./build/test/ut/test_shm_table

# Run with verbose output
./build/test/ut/test_shm_table --gtest_verbose
```

### Test Categories

1. **Single-threaded**: Basic functionality validation
2. **Multi-threaded**: Concurrency and lock-free algorithm validation
3. **Multi-process**: Cross-process shared memory testing
4. **Benchmark**: Performance and scalability testing

## Code Style

### Formatting

```bash
# Format all code
./ccup.sh -f
```

Formats files in `include/`, `src/`, `test/` directories with:
- **File types**: `*.h`, `*.hpp`, `*.c`, `*.cc`, `*.cpp`
- **Style**: Clang-format with project configuration

### Naming Conventions

- **Classes**: `PascalCase` (e.g., `ShmHashTable`)
- **Functions**: `camelCase` (e.g., `visitBucket`)
- **Variables**: `snake_case` (e.g., `cache_line_size`)
- **Constants**: `UPPER_SNAKE_CASE` (e.g., `CACHE_LINE_SIZE`)

### Code Organization

```cpp
// File header with copyright
/**
* Copyright (c) wangbo@joycode.art 2024
*/

// Include guards
#ifndef SHMAP_COMPONENT_H
#define SHMAP_COMPONENT_H

// Standard includes first
#include <atomic>
#include <cstddef>

// Project includes next
#include "shmap/shmap.h"

// Namespace
namespace shmap {

// Class declaration with alignment
struct alignas(CACHE_LINE_SIZE) Component {
    // Static assertions for type safety
    static_assert(std::is_trivially_copyable<T>::value, "Message");

    // Public interface
    Status operation() noexcept;

private:
    // Private implementation
    std::atomic<uint32_t> state_;
};

} // namespace shmap

#endif // SHMAP_COMPONENT_H
```

## Debugging

### Debug Logging

Enable debug logging with compile-time flag:

```cpp
#define SHMAP_DEBUG_ENABLE 1
#include "shmap/shmap.h"
```

Debug logs show file, line, and process ID:
```
[shm_hash_table.h:92:12345] ShmHashTable[42] from READY to ACCESSING!
```

### Sanitizers

Enable sanitizers for concurrency debugging:

```bash
# Build with ThreadSanitizer
cmake -DENABLE_TSAN=ON -B build

# Build with AddressSanitizer
cmake -DENABLE_ASON=ON -B build
```

### Core Dump Analysis

```bash
# Generate core dump
ulimit -c unlimited

# Analyze core dump
gdb ./build/test/ut/test_shm_table core
```

## Performance Optimization

### Compile-Time Optimizations

- **Template Specialization**: Custom hash functions and comparators
- **Constexpr**: Compile-time calculations where possible
- **Inline Functions**: Performance-critical operations

### Runtime Optimizations

- **Cache Alignment**: All shared data structures are cache-line aligned
- **Memory Ordering**: Appropriate `std::memory_order` for access patterns
- **Backoff Strategy**: Exponential backoff for contention

## Integration Guide

### Using shmap in Your Project

1. **Header-Only Integration**:
   ```cmake
   target_include_directories(your_target PRIVATE path/to/shmap/include)
   ```

2. **CMake Integration**:
   ```cmake
   add_subdirectory(path/to/shmap)
   target_link_libraries(your_target PRIVATE shmap)
   ```

### Dependencies

- **Required**: C++17 standard library
- **Optional**: Google Test (for testing), Google Benchmark (for benchmarks)
- **Platform**: POSIX shared memory support

## Contributing

### Development Workflow

1. **Setup**: Use Docker environment (`./ccup.sh -e`)
2. **Develop**: Implement features with tests
3. **Test**: Run comprehensive test suite
4. **Format**: Apply code formatting
5. **Validate**: Ensure all tests pass

### Code Review Checklist

- [ ] Lock-free algorithm correctness
- [ ] Memory ordering correctness
- [ ] Cross-process compatibility
- [ ] Performance characteristics
- [ ] Test coverage
- [ ] Documentation updates

This development guide ensures consistent coding practices and efficient development workflows for the shmap project.