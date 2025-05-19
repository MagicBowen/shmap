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

/* -----------  1. 键类型、Hasher / Equal ---------------- */
using Fix32 = std::array<char, 32>;

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

/* -----------  2. HashMap/Block alias ------------------- */
static constexpr std::size_t CAP      = 1 << 14;     // 16 384 bucket
static constexpr std::size_t N_KEYS   = 1'000;
static constexpr std::size_t OPS      = 100'000;     // 每线程操作数

using Map   = StaticUnifiedHashMap<Fix32, int, CAP, FixHash, FixEq>;
using Block = SharedBlock<Map>;

/* -----------  3. 生成第 i 个 key ----------------------- */
static Fix32 make_key(std::size_t id)
{
    Fix32 k{};
    std::snprintf(k.data(), 32, "key_%04zu", id);
    return k;
}

struct ShmapRaceTtest : public testing::Test {
protected:
};
    
TEST_F(ShmapRaceTtest, shm_race_test) 
{
    const std::size_t NUM_THREADS =
        std::max(4u, std::thread::hardware_concurrency() * 2u);

    /* 3.1 共享内存块（进程内用 new[] 模拟） */
    const std::size_t BYTES = Block::bytes();
    // void* mem = ::operator new[](BYTES, std::align_val_t{64});
    
    /* cache-line 对齐即可；高位对齐用 pagesize 更保险 */
    void* mem = nullptr;
    std::size_t align = std::max<std::size_t>(64, ::getpagesize());
    if (posix_memalign(&mem, align, BYTES) != 0 || mem == nullptr)
        std::abort();          // guaranteed enough & aligned

    std::memset(mem, 0, BYTES);    // optional：保证 UNINIT

    /* 3.2 统计预期值 */
    std::vector<std::atomic<int>> expected(N_KEYS);
    for(auto& a : expected) a.store(0, std::memory_order_relaxed);

    /* 3.3 线程函数 */
    auto worker = [&](int tid)
    {
        /* 50 % 概率尝试 create，50 % 概率 open */
        Block* blk = (tid & 1) ? Block::create(mem)
                               : Block::open(mem);
        Map* map = &blk->map;

        std::mt19937_64 rng(tid * 1234567ull);
        std::uniform_int_distribution<int> key_dist(0, N_KEYS - 1);
        std::uniform_int_distribution<int> op_dist(0, 99);

        for(std::size_t i = 0; i < OPS; ++i)
        {
            int id  = key_dist(rng);
            Fix32 k = make_key(id);

            int op  = op_dist(rng);

            if(op < 70) {
                /* 70 % 概率：插入 / 修改（带写）*/
                map->visit(k,
                    [&](int& v, bool created){
                        (void)created;
                        v += 1;
                    },
                    /* create_if_missing = */ true);
                expected[id].fetch_add(1, std::memory_order_relaxed);
            }
            else {
                /* 30 % 纯读 */
                bool seen = map->visit(k,
                    [](int&, bool){ /* no-op */ },
                    /* create_if_missing = */ false);

                if(seen) {
                    /* 随机读取几次以增强并发读比例 */
                    map->visit(k, [](int&, bool){}, false);
                }
            }
        }
    };

    /* 3.4 启动线程 */
    std::vector<std::thread> tg;
    for(std::size_t t = 0; t < NUM_THREADS; ++t)
        tg.emplace_back(worker, static_cast<int>(t));
    for(auto& th : tg) th.join();

    /* 3.5 校验： value == expected[id]，且只插入一次 */
    Block* blk = Block::open(mem);
    Map*   map = &blk->map;

    for(std::size_t id = 0; id < N_KEYS; ++id)
    {
        Fix32 k = make_key(id);
        int got = 0;

        bool have = map->visit(k,
            [&](int& v, bool){ got = v; },
            false);

        int expect = expected[id].load(std::memory_order_relaxed);

        /* 如果 never touched，则 visit 应返回 false */
        if(expect == 0) {
            assert(!have && "key should not exist");
        } else {
            assert(have);
            assert(got == expect && "value mismatch");
        }
    }

    std::cout << "All assertions passed with "
              << NUM_THREADS << " threads × "
              << OPS << " ops.\n";

    // ::operator delete[](mem, std::align_val_t{64});
    free(mem);
}