# shmap

**shmap** is a high-performance C++17 library providing lock-free concurrent data structures designed for shared memory (shm) environments. This header-only library enables efficient, thread-safe data sharing across multiple processes.

## Overview

shmap provides sophisticated lock-free algorithms and data structures optimized for:

- **Cross-process communication** via POSIX shared memory
- **High concurrency** with lock-free algorithms
- **Low latency** through cache-aware design
- **Type safety** with template-based implementation

## Key Features

### Performance
- Lock-free algorithms minimizing contention
- Cache-line aligned data structures
- Template-based compile-time optimization
- Power-of-two capacities for efficient modulo operations

### Concurrency
- Multi-threaded and multi-process safe
- Atomic operations with proper memory ordering
- Exponential backoff for contention management
- State machines for safe concurrent access

### Data Structures
- **ShmHashTable**: Lock-free closed hashing table
- **ShmRingBuffer**: Multiple ring buffer implementations (SPSC, SPMC, Broadcast)
- **ShmVector**: Shared memory vector with atomic allocation
- **ShmStorage**: POSIX shared memory wrapper

### Utilities
- **FixedString**: Fixed-size string for shared memory
- **BitsInteger**: Bit field manipulation utilities
- **Backoff**: Exponential backoff algorithm
- **Status**: Comprehensive error handling

## Quick Start

```cpp
#include "shmap/shmap.h"
#include "shmap/shm_storage.h"
#include "shmap/shm_hash_table.h"

using namespace shmap;

// Define shared memory path
struct MyShmPath { static constexpr const char* value = "/my_shared_table"; };

// Create shared hash table storage
using Storage = ShmStorage<ShmHashTable<int, std::string, 1024>, MyShmPath>;

// Use in multiple processes
void process_worker() {
    auto& storage = Storage::GetInstance();

    storage->Visit(42, AccessMode::CreateIfMiss,
        [](size_t idx, std::string& value, bool is_new) {
            if (is_new) value = "Hello from process " + std::to_string(getpid());
            return Status::SUCCESS;
        });
}
```

## Build and Development

### Prerequisites
- C++17 compiler
- CMake 3.10+
- POSIX-compliant system (Linux, macOS, or other Unix-like)
- Docker (optional, for containerized development)

### Build Commands

```sh
$ chmod a+x ./ccup.sh

# start project from docker env
# make sure docker is pre-installed on the system.
./ccup.sh -e

# update depends and execute cmake generating
./ccup.sh -u

# build
./ccup.sh -b

# update & build
./ccup.sh -ub

# run tests
./ccup.sh -t

# build & test
./ccup.sh -bt

# update & build & test
./ccup.sh -ubt

# run executable
./ccup.sh -r

# install
./ccup.sh -i

# build & install
./ccup.sh -bi

# update & build & install
./ccup.sh -ubi

# clean build
./ccup.sh -c

# clean all
./ccup.sh -C

# help
./ccup.sh -h
```

### CMake Options

- `ENABLE_UT=ON` - Build unit tests (default: ON)
- `ENABLE_BT=OFF` - Build benchmark tests (default: OFF)
- `ENABLE_TSAN=ON` - Enable ThreadSanitizer (default: ON)
- `ENABLE_ASON=OFF` - Enable AddressSanitizer (default: OFF)

## Documentation

Comprehensive documentation is available in the `docs/` directory:

- **[Documentation README](docs/README.md)** - Project overview and quick start guide
- **[API Reference](docs/api/)** - Detailed API documentation for all components
- **[Architecture](docs/architecture/)** - System architecture and design principles
- **[Design Patterns](docs/design/)** - Key design patterns and algorithms
- **[Testing Strategy](docs/testing/)** - Testing methodology and coverage
- **[Examples](docs/examples/)** - Usage examples and best practices

## Platform Support

### Supported Platforms
- **Linux**: Full POSIX shared memory support
- **macOS**: Full POSIX shared memory support
- **Other Unix-like**: Systems with POSIX shm_open/mmap

### Dependencies
- **C++17**: Standard library features
- **POSIX**: Shared memory APIs
- **CMake**: Build system (optional for integration)

## Use Cases

shmap is ideal for:
- High-performance concurrent applications
- Cross-process communication
- Real-time data processing
- Shared configuration management
- Worker pools and task queues
- Broadcast messaging systems

## License

Copyright (c) wangbo@joycode.art 2024

## Contributing

Contributions are welcome! Please ensure:
- All tests pass (`./ccup.sh -t`)
- Code follows the project's style guidelines
- New features include appropriate tests
- Documentation is updated as needed
