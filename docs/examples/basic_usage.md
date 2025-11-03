# Basic Usage Examples

## Overview

This guide provides practical examples of using shmap's core components for common scenarios.

## Quick Start

### Basic Hash Table Usage

```cpp
#include "shmap/shmap.h"
#include "shmap/shm_hash_table.h"
#include "shmap/fixed_string.h"

using namespace shmap;

int main() {
    // Create a hash table with 1024 buckets
    ShmHashTable<int, FixedString, 1024> table;

    // Insert a key-value pair
    table.Visit(42, AccessMode::CreateIfMiss,
        [](size_t idx, FixedString& value, bool isNew) {
            if (isNew) value = "Hello World";
            return Status::SUCCESS;
        });

    // Access the value
    table.Visit(42, AccessMode::AccessExist,
        [](size_t idx, FixedString& value, bool isNew) {
            std::cout << "Value: " << value << std::endl;
            return Status::SUCCESS;
        });

    // Update the value
    table.Visit(42, AccessMode::CreateIfMiss,
        [](size_t idx, FixedString& value, bool isNew) {
            value = "Updated Value";
            return Status::SUCCESS;
        });

    return 0;
}
```

### Shared Memory Storage

```cpp
#include "shmap/shm_storage.h"

// Define shared memory path
struct MyShmPath {
    static constexpr const char* value = "/my_shared_table";
};

// Create shared storage
using Storage = ShmStorage<ShmHashTable<int, std::string, 1024>, MyShmPath>;

void process_one() {
    auto& storage = Storage::GetInstance();

    storage->Visit(1, AccessMode::CreateIfMiss,
        [](size_t idx, std::string& value, bool isNew) {
            if (isNew) value = "From Process One";
            return Status::SUCCESS;
        });
}

void process_two() {
    auto& storage = Storage::GetInstance();

    storage->Visit(1, AccessMode::AccessExist,
        [](size_t idx, std::string& value, bool isNew) {
            std::cout << "Process Two sees: " << value << std::endl;
            return Status::SUCCESS;
        });
}
```

## Common Patterns

### Counter Pattern

```cpp
// Shared counter across processes
using CounterStorage = ShmStorage<ShmHashTable<FixedString, int, 8>, CounterPath>;

void increment_counter(const std::string& name) {
    auto& storage = CounterStorage::GetInstance();

    storage->Visit(FixedString(name), AccessMode::CreateIfMiss,
        [](size_t idx, int& value, bool isNew) {
            if (isNew) value = 0;
            ++value;
            return Status::SUCCESS;
        });
}

int get_counter(const std::string& name) {
    auto& storage = CounterStorage::GetInstance();

    int result = 0;
    storage->Visit(FixedString(name), AccessMode::AccessExist,
        [&result](size_t idx, int& value, bool isNew) {
            result = value;
            return Status::SUCCESS;
        });

    return result;
}
```

### Configuration Storage

```cpp
#include "shmap/fixed_string.h"

struct ConfigPath { static constexpr const char* value = "/app_config"; };
using ConfigStorage = ShmStorage<ShmHashTable<FixedString, FixedString, 64>, ConfigPath>;

void set_config(const std::string& key, const std::string& value) {
    auto& storage = ConfigStorage::GetInstance();

    storage->Visit(FixedString(key), AccessMode::CreateIfMiss,
        [&value](size_t idx, FixedString& config_value, bool isNew) {
            config_value = value;
            return Status::SUCCESS;
        });
}

std::string get_config(const std::string& key, const std::string& default_val = "") {
    auto& storage = ConfigStorage::GetInstance();

    std::string result = default_val;
    storage->Visit(FixedString(key), AccessMode::AccessExist,
        [&result](size_t idx, FixedString& config_value, bool isNew) {
            result = config_value.ToString();
            return Status::SUCCESS;
        });

    return result;
}
```

### Producer-Consumer with Ring Buffer

```cpp
#include "shmap/shm_ring_buffer.h"

// SPSC ring buffer for data transfer
ShmRingBuffer<std::string, 1024> data_buffer;

void producer() {
    for (int i = 0; i < 1000; ++i) {
        std::string data = "Message " + std::to_string(i);
        while (!data_buffer.push(data)) {
            // Buffer full, wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void consumer() {
    int count = 0;
    while (count < 1000) {
        if (auto data = data_buffer.pop()) {
            std::cout << "Received: " << *data << std::endl;
            ++count;
        } else {
            // Buffer empty, wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
```

## Error Handling Examples

### Comprehensive Error Handling

```cpp
void safe_table_operation() {
    ShmHashTable<int, std::string, 1024> table;

    Status result = table.Visit(42, AccessMode::CreateIfMiss,
        [](size_t idx, std::string& value, bool isNew) {
            if (isNew) {
                value = "Initial Value";
            } else {
                value += " - Updated";
            }
            return Status::SUCCESS;
        });

    if (!result) {
        switch (result) {
            case Status::TIMEOUT:
                std::cerr << "Operation timed out" << std::endl;
                break;
            case Status::NOT_FOUND:
                std::cerr << "Element not found" << std::endl;
                break;
            default:
                std::cerr << "Error: " << result << std::endl;
                break;
        }
    }
}
```

### Timeout with Backoff

```cpp
Status robust_operation() {
    ShmHashTable<int, int, 8> table;  // Small table for demonstration

    Backoff backoff(std::chrono::seconds(5));

    while (true) {
        Status result = table.Visit(42, AccessMode::CreateIfMiss,
            [](size_t idx, int& value, bool isNew) {
                if (isNew) value = 0;
                ++value;
                return Status::SUCCESS;
            });

        if (result) {
            return result;  // Success
        }

        if (result == Status::TIMEOUT || !backoff.next()) {
            return Status::TIMEOUT;  // Give up after timeout
        }

        // Continue retrying with backoff
    }
}
```

## Multi-threaded Examples

### Worker Pool with Shared Queue

```cpp
#include <vector>
#include <thread>
#include <atomic>

ShmSpMcRingBuffer<std::string, 1024> work_queue;
std::atomic<int> completed_tasks{0};

void worker(int id) {
    while (true) {
        if (auto task = work_queue.pop()) {
            std::cout << "Worker " << id << " processing: " << *task << std::endl;
            // Simulate work
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            completed_tasks.fetch_add(1);
        } else {
            // No work available, check if we should exit
            if (completed_tasks >= 100) break;
            std::this_thread::yield();
        }
    }
}

void producer() {
    for (int i = 0; i < 100; ++i) {
        std::string task = "Task " + std::to_string(i);
        while (!work_queue.push(task)) {
            std::this_thread::yield();
        }
    }
}

int main() {
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back(worker, i);
    }

    std::thread producer_thread(producer);

    producer_thread.join();
    for (auto& w : workers) w.join();

    return 0;
}
```

### Concurrent Counter with Rollback

```cpp
using RollbackTable = ShmHashTable<FixedString, int, 64,
                                   std::hash<FixedString>,
                                   std::equal_to<FixedString>,
                                   true>;  // Enable rollback

void concurrent_counter_updates() {
    RollbackTable table;

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&table, i]() {
            for (int j = 0; j < 100; ++j) {
                table.Visit(FixedString("counter"), AccessMode::CreateIfMiss,
                    [i, j](size_t idx, int& value, bool isNew) {
                        if (isNew) value = 0;

                        // Simulate occasional failures
                        if ((i + j) % 23 == 0) {
                            return Status::ERROR;  // This will rollback
                        }

                        value += 1;
                        return Status::SUCCESS;
                    });
            }
        });
    }

    for (auto& t : threads) t.join();

    // Check final counter value
    int final_count = 0;
    table.Visit(FixedString("counter"), AccessMode::AccessExist,
        [&final_count](size_t idx, int& value, bool isNew) {
            final_count = value;
            return Status::SUCCESS;
        });

    std::cout << "Final counter: " << final_count << std::endl;
}
```

## Advanced Patterns

### Broadcast Messaging

```cpp
BroadcastRingBuffer<std::string, 256> message_bus;

void setup_broadcast() {
    message_bus.init(3);  // Expect 3 consumers
}

void broadcaster() {
    for (int i = 0; i < 10; ++i) {
        std::string msg = "Broadcast Message " + std::to_string(i);
        message_bus.push(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void listener(int id) {
    auto consumer = message_bus.make_consumer();

    while (true) {
        if (auto msg = consumer.pop()) {
            std::cout << "Listener " << id << " received: " << *msg << std::endl;
        } else {
            // No messages, check for termination condition
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
```

### Bit Field Configuration

```cpp
#include "shmap/bits_integer.h"

enum class ConfigFields { CacheSize, CompressionLevel, ChecksumEnabled };

using ConfigBits = BitsInteger<uint32_t,
    BitField<ConfigFields::CacheSize, 0, 12>,        // 12 bits for cache size
    BitField<ConfigFields::CompressionLevel, 12, 4>, // 4 bits for compression
    BitField<ConfigFields::ChecksumEnabled, 16, 1>   // 1 bit for flag
>;

void configure_system() {
    ConfigBits config;

    // Set configuration values
    config.Set<ConfigFields::CacheSize>(1024);
    config.Set<ConfigFields::CompressionLevel>(3);
    config.Set<ConfigFields::ChecksumEnabled>(1);

    // Store in shared configuration
    ShmHashTable<FixedString, ConfigBits, 8> config_table;
    config_table.Visit(FixedString("system_config"), AccessMode::CreateIfMiss,
        [&config](size_t idx, ConfigBits& stored_config, bool isNew) {
            stored_config = config;
            return Status::SUCCESS;
        });
}
```

These examples demonstrate the versatility and power of shmap for building high-performance concurrent applications with shared memory data structures.