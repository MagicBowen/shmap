#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include <new>

#include "shmap.h"
#include "fixed_string.h"

using namespace shmap;

namespace {
    static constexpr std::size_t CAPACITY = 1 << 14;     // 16 384 buckets
    static constexpr std::size_t N_KEYS   = 1'000;
    static constexpr std::size_t OPS      = 100'000;     // operator count per thread

    using Map   = ShmTable<FixedString, int, CAPACITY>;
    using Block = ShmBlock<Map>;
}

struct ShmapMtRaceTtest : public testing::Test {
protected:
    void SetUp() override {
        const std::size_t BYTES = Block::GetMemUsage();

        std::size_t align = std::max<std::size_t>(64, ::getpagesize());
        ASSERT_TRUE(posix_memalign(&sharedMem_, align, BYTES) == 0 && sharedMem_ != nullptr);

        std::memset(sharedMem_, 0, BYTES);
    }

    void TearDown() override {
        free(sharedMem_);
        sharedMem_ = nullptr;
    }

protected:
    void* sharedMem_{nullptr};
};
    
TEST_F(ShmapMtRaceTtest, shmap_multi_threads_race_test) 
{
    std::vector<std::atomic<int>> expected(N_KEYS);
    for(auto& a : expected) {
        a.store(0, std::memory_order_relaxed);
    }

    auto worker = [&](int tid) {
        // 50 % try create，50 % try open
        Block* blk = (tid & 1) ? Block::Create(sharedMem_)
                               : Block::Open(sharedMem_);
        Map& table_ = blk->table_;

        std::mt19937_64 rng(tid * 1234567ull);
        std::uniform_int_distribution<int> key_dist(0, N_KEYS - 1);
        std::uniform_int_distribution<int> op_dist(0, 99);

        for(std::size_t i = 0; i < OPS; ++i) {
            int id  = key_dist(rng);
            auto k = FixedString::FromFormat("key_%04zu", id);

            int op  = op_dist(rng);

            if(op < 70) {
                // 70 % insert / modify
                table_.Visit(k,
                    [&](int& v, bool created){
                        (void)created;
                        v += 1;
                    },
                    /* create_if_missing = */ true);
                expected[id].fetch_add(1, std::memory_order_relaxed);
            }
            else {
                // 30 % only read
                int gotValue = 0;
                bool seen = table_.Visit(k,
                    [&gotValue](int& value, bool neu){ 
                        gotValue = value; 
                        ASSERT_FALSE(neu);
                    },
                    /* create_if_missing = */ false);

                if(seen) {
                    // Read for racing
                    table_.Visit(k, [&gotValue](int& value, bool neu){
                        ASSERT_TRUE(value >= gotValue);
                        ASSERT_TRUE(value > 0 && value < OPS);
                        ASSERT_FALSE(neu);
                    }, false);
                }
            }
        }
    };

    const std::size_t NUM_THREADS =
    std::max(8u, std::thread::hardware_concurrency() * 2u);

    std::vector<std::thread> threadGroup;
    for(std::size_t t = 0; t < NUM_THREADS; ++t) {
        threadGroup.emplace_back(worker, static_cast<int>(t));
    }

    for(auto& th : threadGroup) {
        th.join();
    }

    Block* blk = Block::Open(sharedMem_);
    Map&   table_ = blk->table_;

    for(std::size_t id = 0; id < N_KEYS; ++id) {
        auto k = FixedString::FromFormat("key_%04zu", id);
        int got = 0;

        bool have = table_.Visit(k,
            [&](int& v, bool){ got = v; },
            false);

        int expect = expected[id].load(std::memory_order_relaxed);

        if(expect == 0) {
            ASSERT_FALSE(have);
            std::cout << "[MT] key["<< id <<"] has not been inserted\n";
        } else {
            ASSERT_TRUE(have);
            ASSERT_EQ(expect, got);
        }
    }
}