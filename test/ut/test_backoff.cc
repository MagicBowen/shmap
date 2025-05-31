#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <vector>
#include <chrono>
#include <numeric>

#include "backoff.h"
#include "shm_hash_table.h"

using namespace shmap;
using namespace std::chrono_literals;

namespace {
    // Hash table disabled rollback
    using Table = ShmHashTable<int,int,16>;

    // Hash table enabled rollback
    using RbTable = ShmHashTable<int, int, 16, std::hash<int>, std::equal_to<int>, true>;

    template<typename TABLE>
    static std::pair<bool,int> peek(TABLE& table, int key) {
        bool found = false;
        int  val   = -1;
        table.Visit(key, AccessMode::AccessExist,
            [&](std::size_t, int& v, bool){
                found = true;
                val   = v;
            });
        return {found, val};
    }
}

TEST(RollbackTest, InsertFailLeavesEmpty) {
    RbTable table;

    // If the visitor returns false
    // the insertion should be rolled back and the subsequent access won't find it
    bool ok = table.Visit(42, AccessMode::CreateIfMiss,
        [](auto, int&, bool){ return false; }
    );
    ASSERT_FALSE(ok);

    auto [found, val] = peek(table, 42);
    ASSERT_FALSE(found);
}

TEST(RollbackTest, UpdateFailRestoresOldValue) {
    RbTable table;
    // First, successfully insert value = 1
    ASSERT_TRUE(table.Visit(7, AccessMode::CreateIfMiss,
        [](auto, int& v, bool){ v = 1; return true; }
    ));
    // Visit again, try to set the value to 2, 
    // but return false -> it should be rolled back
    ASSERT_FALSE(table.Visit(7, AccessMode::AccessExist,
        [](auto, int& v, bool){ v = 2; return false; }
    ));
    // The final value should still be 1
    auto [found,val] = peek(table, 7);
    ASSERT_TRUE(found);
    ASSERT_EQ(val,1);
}

TEST(ConcurrencyTest, MultiThreadIncrement) {
    Table table;
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 1000;

    auto worker = [&](int /*id*/) {
        for(int i = 0; i < PER_THREAD; ++i) {
            table.Visit(1, AccessMode::CreateIfMiss,
                [](auto, int& v, bool is_new){
                    ++v;
                }
            );
        }
    };

    std::vector<std::thread> thr;
    for(int i = 0; i < THREADS; ++i) {
        thr.emplace_back(worker, i);
    }
    for(auto& th : thr) {
        th.join();
    }

    auto [found, val] = peek(table,1);
    ASSERT_TRUE(found);
    ASSERT_EQ(val, THREADS * PER_THREAD);
}

TEST(TimeoutTest, VisitorHoldLockCausesTimeout) {
    Table table;
    int key = 99;

    // Thread A: Create for the first time and sleep for 200ms in the Visitor
    std::thread blocker([&](){
        table.Visit(key, AccessMode::CreateIfMiss,
            [&](auto, int& v, bool){
                std::this_thread::sleep_for(200ms);
                v = 123;
            }
        );
    });

    // Sleep a little to ensure that A has entered the Visitor and holds the INSERTING lock
    std::this_thread::sleep_for(20ms);

    // Thread B: Read-only mode, with a 100ms timeout => should return false in advance
    auto start = std::chrono::steady_clock::now();
    bool ok = table.Visit(key, AccessMode::AccessExist,
        [&](auto, int& v, bool){ v = 128; },
        100ms  // timeout
    );
    auto dur = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(ok);
    // The time spent should be approximately >= 100ms (allow a little error)
    ASSERT_GE(dur, 90ms);

    blocker.join();
    // The final insertion should succeed and the value should be 123
    auto [found,val] = peek(table,key);
    ASSERT_TRUE(found);
    ASSERT_EQ(val,123);
}

TEST(BackoffTest, LateAverageGreaterThanEarly) {
    constexpr int YIELD_LIMIT = 10;
    constexpr int LATE_STEPS  = 10;

    Backoff bf(std::chrono::seconds(1));

    std::vector<long long> early, late;
    early.reserve(YIELD_LIMIT);
    late .reserve(LATE_STEPS);

    for(int i = 0; i < YIELD_LIMIT; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        EXPECT_TRUE(bf.next());

        auto t1 = std::chrono::high_resolution_clock::now();
        early.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }
    for(int i = 0; i < LATE_STEPS; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        EXPECT_TRUE(bf.next());
        auto t1 = std::chrono::high_resolution_clock::now();
        late.push_back( std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }

    double avg_early = std::accumulate(early.begin(),  early.end(), 0.0) / YIELD_LIMIT;
    double avg_late  = std::accumulate(late.begin(),   late.end(),  0.0) / LATE_STEPS;

    EXPECT_GT(avg_late, avg_early * 1.5) << "avg_early=" << avg_early << "us, avg_late=" << avg_late << "us";
}