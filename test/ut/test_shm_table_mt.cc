#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

#include "shmap.h"
#include "fixed_string.h"

using namespace shmap;

using Table = ShmHashTable<int, int, 1024>;

TEST(ShmTable_Concurrent, ParallelInsertDistinct) {
    Table tbl;
    const int N = 128;
    std::vector<std::thread> ths;
    for (int t = 0; t < 8; ++t) {
        ths.emplace_back([&, t]() {
            for (int i = 0; i < N; ++i) {
                int key = t * N + i;
                bool ok = tbl.Visit(key, AccessMode::CreateIfMiss,
                    [&](size_t, int& v, bool is_new){
                        ASSERT_TRUE(is_new);
                        v = key * 2;
                    });
                ASSERT_TRUE(ok);
            }
        });
    }
    for (auto& t: ths) t.join();

    for (int t = 0; t < 8; ++t) {
        for (int i = 0; i < N; ++i) {
            int key = t * N + i;
            bool ok = tbl.Visit(key, AccessMode::AccessExist,
                [&](size_t, int& v, bool){
                    ASSERT_EQ(v, key * 2);
                });
            ASSERT_TRUE(ok);
        }
    }
}

TEST(ShmTable_Concurrent, ReadWriteSameKey) {
    Table tbl;
    const int THREADS = 4;
    std::atomic<int> writer_done{0};

    std::thread writer([&](){
        for (int i = 1; i <= 1000; ++i) {
            tbl.Visit(1, AccessMode::CreateIfMiss,
                [&](size_t, int& v, bool){
                    v = i;
                });
        }
        writer_done.store(1, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    std::atomic<bool> failed{false};
    for (int r = 0; r < THREADS; ++r) {
        readers.emplace_back([&](){
            while (!writer_done.load(std::memory_order_acquire)) {
                tbl.Visit(1, AccessMode::AccessExist,
                    [&](size_t, int& v, bool){
                        if (v < 1 || v > 1000) failed.store(true);
                    });
            }
        });
    }

    writer.join();
    for (auto& t : readers) t.join();
    ASSERT_FALSE(failed.load());
}