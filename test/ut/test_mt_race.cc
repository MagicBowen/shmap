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

using namespace shmap;

namespace {
    using Fix32 = std::array<char, 32>;

    static Fix32 make_key(std::size_t id) {
        Fix32 k{};
        std::snprintf(k.data(), 32, "key_%04zu", id);
        return k;
    }

    struct FixHash {
        std::size_t operator()(const Fix32& s) const noexcept {
            std::size_t h = 14695981039346656037ull;
            for(char c : s) {
                if(c == 0) break;
                h ^= static_cast<unsigned char>(c);
                h *= 1099511628211ull;
            }
            return h;
        }
    };

    struct FixEq {
        bool operator()(const Fix32& a, const Fix32& b) const noexcept {
            return std::memcmp(a.data(), b.data(), 32) == 0;
        }
    };

    static constexpr std::size_t CAP      = 1 << 14;     // 16 384 buckets
    static constexpr std::size_t N_KEYS   = 1'000;
    static constexpr std::size_t OPS      = 100'000;     // 每线程操作数

    using Map   = ShmTable<Fix32, int, CAP, FixHash, FixEq>;
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
        /* 50 % 概率尝试 create，50 % 概率 open */
        Block* blk = (tid & 1) ? Block::Create(sharedMem_)
                               : Block::Open(sharedMem_);
        Map& table_ = blk->table_;

        std::mt19937_64 rng(tid * 1234567ull);
        std::uniform_int_distribution<int> key_dist(0, N_KEYS - 1);
        std::uniform_int_distribution<int> op_dist(0, 99);

        for(std::size_t i = 0; i < OPS; ++i) {
            int id  = key_dist(rng);
            Fix32 k = make_key(id);

            int op  = op_dist(rng);

            if(op < 70) {
                /* 70 % 概率：插入 / 修改（带写）*/
                table_.Visit(k,
                    [&](int& v, bool created){
                        (void)created;
                        v += 1;
                    },
                    /* create_if_missing = */ true);
                expected[id].fetch_add(1, std::memory_order_relaxed);
            }
            else {
                /* 30 % 纯读 */
                int gotValue = 0;
                bool seen = table_.Visit(k,
                    [&gotValue](int& value, bool neu){ 
                        gotValue = value; 
                        ASSERT_FALSE(neu);
                    },
                    /* create_if_missing = */ false);

                if(seen) {
                    /* 随机读取几次以增强并发读比例 */
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
        Fix32 k = make_key(id);
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