#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <iostream>

#include "shmap/shm_storage.h"
#include "shmap/shm_hash_table.h"
#include "shmap/fixed_string.h"

using namespace shmap;

namespace {
    struct ShmPath {
        static constexpr const char* value = "/shm_storage_mt_test";
    };

    using Table = ShmHashTable<int, int, 128>;
    using Storage = ShmStorage<Table, ShmPath>;
}

struct ShmStorgeMtTest : public testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
        Storage::GetInstance().Destroy();
    }
};

TEST_F(ShmStorgeMtTest, ConcurrentInsertAndRead) {
    auto& storage = Storage::GetInstance();

    constexpr int keyCount = 10;
    constexpr int threadCount = 8;
    std::vector<std::thread> threads;

    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = j % keyCount;
                storage->Visit(key, AccessMode::CreateIfMiss, 
                [i](auto, auto& value, bool created) {
                    if (created) {
                        value = i;
                    } else {
                        value += i;
                    }
                });
            }
        });
    }

    std::atomic<bool> done{false};
    std::thread reader([&]() {
        while (!done) {
            for (int key = 0; key < keyCount; ++key) {
                storage->Visit(key, AccessMode::AccessExist, [](auto, auto&, bool) {});
            }
        }
    });

    for (auto& t : threads) t.join();
    done = true;
    reader.join();

    // Verify all keys are present
    for (int key = 0; key < keyCount; ++key) {
        bool found = storage->Visit(key, AccessMode::AccessExist, [](auto, auto&, bool) {});
        ASSERT_TRUE(found);
    }
}