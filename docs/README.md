# shmap Documentation

**shmap** is a high-performance C++17 library providing lock-free concurrent data structures designed for shared memory (shm) environments. This header-only library enables efficient, thread-safe data sharing across multiple processes.

## Overview

shmap provides sophisticated lock-free algorithms and data structures optimized for:

- **Cross-process communication** via POSIX shared memory
- **High concurrency** with lock-free algorithms
- **Low latency** through cache-aware design
- **Type safety** with template-based implementation

## Key Features

### 🚀 Performance
- Lock-free algorithms minimizing contention
- Cache-line aligned data structures
- Template-based compile-time optimization
- Power-of-two capacities for efficient modulo operations

### 🔒 Concurrency
- Multi-threaded and multi-process safe
- Atomic operations with proper memory ordering
- Exponential backoff for contention management
- State machines for safe concurrent access

### 📦 Data Structures
- **ShmHashTable**: Lock-free closed hashing table
- **ShmRingBuffer**: Multiple ring buffer implementations (SPSC, SPMC, Broadcast)
- **ShmVector**: Shared memory vector with atomic allocation
- **ShmStorage**: POSIX shared memory wrapper

### 🛠️ Utilities
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
        });
}
```

## Documentation Structure

- **[API Reference](./api/)** - Detailed API documentation for all components
- **[Architecture](./architecture/)** - System architecture and design principles
- **[Design Patterns](./design/)** - Key design patterns and algorithms
- **[Testing Strategy](./testing/)** - Testing methodology and coverage
- **[Examples](./examples/)** - Usage examples and best practices

## Build and Development

See [Development Guide](./architecture/development.md) for build instructions, testing, and development workflow.

## License

Copyright (c) wangbo@joycode.art 2024

---

Explore the documentation to understand shmap's powerful capabilities for building high-performance concurrent applications!