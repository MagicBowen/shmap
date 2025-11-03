# Testing Strategy

## Overview

shmap employs a comprehensive testing strategy to validate the correctness, performance, and reliability of its lock-free concurrent data structures across various concurrency scenarios.

## Test Organization

### Directory Structure

```
test/
├── ut/                    # Unit tests
│   ├── test_*.cc         # Single-threaded tests
│   ├── test_*_mt.cc      # Multi-threaded tests
│   ├── test_*_mp.cc      # Multi-process tests
│   └── CMakeLists.txt
├── bt/                    # Benchmark tests
│   ├── test_shmap.cc
│   └── CMakeLists.txt
└── fixture/               # Test utilities
    ├── process_launcher.h
    └── process_launcher.cc
```

### Test Categories

| Category | Pattern | Description | Test Files |
|----------|---------|-------------|------------|
| **Single-threaded** | `test_*.cc` | Basic functionality | 8 files |
| **Multi-threaded** | `test_*_mt.cc` | Concurrent access | 4 files |
| **Multi-process** | `test_*_mp.cc` | Cross-process coordination | 2 files |
| **Benchmark** | `test_*.cc` | Performance validation | 1 file |

## Test Coverage Analysis

### Core Component Testing

#### ShmHashTable
- **Basic Operations**: Insert, access, update, enumeration
- **Bucket Operations**: Direct bucket access with rollback
- **Error Conditions**: Timeout, not found, invalid arguments
- **Concurrent Access**: Multi-threaded insert/access patterns

#### ShmRingBuffer
- **SPSC**: Single producer single consumer patterns
- **SPMC**: Single producer multiple consumers
- **Broadcast**: All consumers receive all messages
- **Edge Cases**: Full/empty conditions, wrap-around

#### ShmStorage
- **Basic Operations**: Creation, access, destruction
- **Cross-process**: Multi-process coordination
- **Memory Management**: Proper cleanup and resource handling

#### Utility Components
- **FixedString**: String operations, formatting, comparison
- **BitsInteger**: Bit field operations, validation
- **Backoff**: Exponential backoff algorithm
- **Status**: Error handling and conversion

### Concurrency Testing Strategy

#### Multi-threaded Testing

```cpp
TEST(ShmHashTableTest, ConcurrentInsertDistinctKeys) {
    ShmHashTable<int, int, 1024> table;

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&table, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = i * 100 + j;
                table.Visit(key, AccessMode::CreateIfMiss,
                    [](auto idx, auto& value, bool isNew) {
                        if (isNew) value = 1;
                        return Status::SUCCESS;
                    });
            }
        });
    }

    for (auto& t : threads) t.join();

    // Validate all inserts succeeded
    int count = 0;
    table.Travel([&count](auto idx, auto& key, auto& value) {
        ++count;
        return Status::SUCCESS;
    });

    EXPECT_EQ(count, 1000);
}
```

#### Multi-process Testing

```cpp
TEST(ShmStorageTest, MultiProcessCoordination) {
    auto& storage = Storage::GetInstance();

    // Test cross-process coordination
    ProcessLauncher launcher;
    auto child = launcher.Launch("child", [&]() {
        auto& child_storage = Storage::GetInstance();
        child_storage->Visit("shared", AccessMode::CreateIfMiss,
            [](auto idx, auto& value, bool isNew) {
                if (isNew) value = 100;
                return Status::SUCCESS;
            });
    });

    auto results = launcher.Wait({child});
    EXPECT_TRUE(results[0].status);

    // Verify data is shared
    bool found = storage->Visit("shared", AccessMode::AccessExist,
        [](auto idx, auto& value, bool) {
            EXPECT_EQ(value, 100);
            return Status::SUCCESS;
        });
    EXPECT_TRUE(found);
}
```

### Stress Testing

#### High Contention Scenarios

```cpp
TEST(ShmHashTableTest, HighContentionSameKey) {
    ShmHashTable<int, int, 8> table;  // Small table to force contention

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&table, &success_count]() {
            for (int j = 0; j < 100; ++j) {
                if (table.Visit(42, AccessMode::CreateIfMiss,
                    [](auto idx, auto& value, bool isNew) {
                        if (isNew) value = 0;
                        ++value;
                        return Status::SUCCESS;
                    })) {
                    success_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    // Validate final state
    int final_value = 0;
    table.Visit(42, AccessMode::AccessExist,
        [&final_value](auto idx, auto& value, bool) {
            final_value = value;
            return Status::SUCCESS;
        });

    EXPECT_GT(success_count, 0);
    EXPECT_LE(final_value, success_count);
}
```

## Test Infrastructure

### Process Launcher Utility

The `ProcessLauncher` class provides sophisticated multi-process testing capabilities:

```cpp
class ProcessLauncher {
public:
    Processor Launch(const std::string& name, ProcessTask task = nullptr);
    std::vector<TaskResult> Wait(const std::vector<Processor>& ps,
                                std::chrono::milliseconds timeout);
    bool Stop(const Processor& p);
};
```

**Features:**
- Process lifecycle management
- Inter-process communication
- Timeout handling
- Result collection and validation

### Test Fixtures

Proper resource management for shared memory tests:

```cpp
struct ShmStorageTest : public testing::Test {
protected:
    void SetUp() override {
        // Optional setup
    }

    void TearDown() override {
        Storage::GetInstance().Destroy();  // Cleanup shared memory
    }
};
```

## Performance Testing

### Benchmark Tests

Using Google Benchmark for performance validation:

```cpp
static void BM_HashTableInsert(benchmark::State& state) {
    ShmHashTable<int, int, 1024> table;

    for (auto _ : state) {
        int key = state.iterations() % 1000;
        table.Visit(key, AccessMode::CreateIfMiss,
            [](auto idx, auto& value, bool isNew) {
                if (isNew) value = 1;
                return Status::SUCCESS;
            });
    }
}

BENCHMARK(BM_HashTableInsert);
```

### Concurrency Scaling Tests

Measure performance under increasing thread counts:

```cpp
TEST(ShmHashTableTest, ConcurrencyScaling) {
    constexpr int OPERATIONS_PER_THREAD = 1000;

    for (int thread_count : {1, 2, 4, 8, 16}) {
        ShmHashTable<int, int, 1024> table;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        for (int i = 0; i < thread_count; ++i) {
            threads.emplace_back([&table, i]() {
                for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
                    int key = i * OPERATIONS_PER_THREAD + j;
                    table.Visit(key, AccessMode::CreateIfMiss,
                        [](auto idx, auto& value, bool isNew) {
                            if (isNew) value = 1;
                            return Status::SUCCESS;
                        });
                }
            });
        }

        for (auto& t : threads) t.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        // Log performance metrics
        std::cout << "Threads: " << thread_count
                  << ", Time: " << duration.count() << "μs" << std::endl;
    }
}
```

## Error Condition Testing

### Timeout Handling

```cpp
TEST(ShmHashTableTest, OperationTimeout) {
    ShmHashTable<int, int, 2> table;  // Very small table

    // Fill table to force contention
    table.Visit(1, AccessMode::CreateIfMiss, [](...) { return Status::SUCCESS; });
    table.Visit(2, AccessMode::CreateIfMiss, [](...) { return Status::SUCCESS; });

    // This should timeout
    auto start = std::chrono::steady_clock::now();
    auto status = table.Visit(3, AccessMode::CreateIfMiss, [](...) {
        return Status::SUCCESS;
    }, std::chrono::milliseconds(100));
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(status, Status::TIMEOUT);
    EXPECT_GE(end - start, std::chrono::milliseconds(100));
}
```

### Rollback Validation

```cpp
TEST(ShmHashTableTest, RollbackMechanism) {
    using Table = ShmHashTable<int, int, 8, std::hash<int>, std::equal_to<int>, true>;
    Table table;

    // Insert initial value
    table.Visit(42, AccessMode::CreateIfMiss,
        [](auto idx, auto& value, bool isNew) {
            if (isNew) value = 100;
            return Status::SUCCESS;
        });

    // Attempt modification that fails
    table.Visit(42, AccessMode::CreateIfMiss,
        [](auto idx, auto& value, bool isNew) {
            value = 200;  // Modify
            return Status::ERROR;  // Force rollback
        });

    // Verify rollback occurred
    int final_value = 0;
    table.Visit(42, AccessMode::AccessExist,
        [&final_value](auto idx, auto& value, bool) {
            final_value = value;
            return Status::SUCCESS;
        });

    EXPECT_EQ(final_value, 100);  // Should be original value
}
```

## Continuous Integration

### Automated Testing

The test suite is designed for CI/CD pipelines:

```bash
# Run all tests
./ccup.sh -t

# Run with sanitizers
cmake -DENABLE_TSAN=ON -B build && cmake --build build && ./ccup.sh -t

# Run benchmarks
cmake -DENABLE_BT=ON -B build && cmake --build build && ./build/test/bt/test_shmap
```

### Test Result Validation

- **All tests must pass** in single-threaded, multi-threaded, and multi-process scenarios
- **No memory leaks** detected by sanitizers
- **Performance benchmarks** within expected ranges
- **Cross-process coordination** validated across different test scenarios

## Coverage Goals

- **100% code coverage** for core algorithms
- **Comprehensive concurrency testing** for all data structures
- **Cross-platform validation** on supported platforms
- **Performance regression detection** through benchmark tests

This testing strategy ensures that shmap's lock-free algorithms are correct, performant, and reliable under all expected usage scenarios.