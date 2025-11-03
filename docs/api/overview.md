# API Reference Overview

## Core Data Structures

shmap provides several lock-free concurrent data structures designed for shared memory environments:

### Primary Components

| Component | Description | Key Features |
|-----------|-------------|--------------|
| **ShmHashTable** | Lock-free closed hashing table | Visitor pattern, atomic state transitions |
| **ShmRingBuffer** | Multiple ring buffer implementations | SPSC, SPMC, Broadcast variants |
| **ShmVector** | Shared memory vector | Atomic allocation, fixed capacity |
| **ShmStorage** | POSIX shared memory wrapper | Singleton pattern, automatic cleanup |

### Utility Components

| Component | Description | Usage |
|-----------|-------------|--------|
| **FixedString** | Fixed-size string | Shared memory string operations |
| **BitsInteger** | Bit field manipulation | Compact data storage |
| **Backoff** | Exponential backoff | Contention management |
| **Status** | Error handling | Comprehensive status codes |

## Common Patterns

### Template Parameters

Most shmap components use template parameters for type safety and customization:

```cpp
template<typename KEY, typename VALUE, std::size_t CAPACITY,
         typename HASH = std::hash<KEY>,
         typename EQUAL = std::equal_to<KEY>,
         bool ROLLBACK_ENABLE = false>
struct ShmHashTable;
```

### Memory Requirements

All stored types must satisfy:
- `std::is_trivially_copyable<T>::value`
- `std::is_standard_layout<T>::value`

### Error Handling

Use `Status` type for error handling:

```cpp
Status result = table.Visit(key, mode, visitor);
if (!result) {
    // Handle error
    std::cout << "Error: " << result << std::endl;
}
```

## Quick API Reference

### Status Codes

| Code | Value | Description |
|------|-------|-------------|
| `SUCCESS` | 0 | Operation completed successfully |
| `ERROR` | 1 | General error |
| `NOT_FOUND` | 3 | Element not found |
| `TIMEOUT` | 5 | Operation timeout |
| `INVALID_ARGUMENT` | 8 | Invalid parameter |

### Access Modes

| Mode | Description |
|------|-------------|
| `AccessExist` | Access existing elements only |
| `CreateIfMiss` | Create element if it doesn't exist |

## Header Files

- `shmap/shmap.h` - Core definitions and debug logging
- `shmap/shm_hash_table.h` - Hash table implementation
- `shmap/shm_ring_buffer.h` - Ring buffer implementations
- `shmap/shm_vector.h` - Vector implementation
- `shmap/shm_storage.h` - Shared memory storage
- `shmap/status.h` - Error handling
- `shmap/backoff.h` - Backoff algorithm
- `shmap/fixed_string.h` - Fixed string utilities
- `shmap/bits_integer.h` - Bit field utilities

## Namespace

All components are in the `shmap` namespace:

```cpp
using namespace shmap;
// or
shmap::ShmHashTable<int, std::string, 1024> table;
```

## Thread Safety

All data structures are:
- **Thread-safe**: Can be used concurrently from multiple threads
- **Process-safe**: Can be used across multiple processes via shared memory
- **Lock-free**: Use atomic operations without mutexes

## Memory Ordering

shmap uses appropriate memory ordering:
- `std::memory_order_acquire` for loads
- `std::memory_order_release` for stores
- `std::memory_order_acq_rel` for read-modify-write operations

Refer to individual component documentation for detailed API usage and examples.