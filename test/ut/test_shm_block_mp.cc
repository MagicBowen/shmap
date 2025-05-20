#include <gtest/gtest.h>
#include <array>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <random>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "shmap.h"
#include "fixed_string.h"

using namespace shmap;

namespace {
    constexpr std::size_t CAPACITY     = 1<<14;   // 16 384 buckets
    constexpr std::size_t N_KEYS       = 1'000;
    constexpr std::size_t PER_PROC_OPS = 50'000;
    constexpr int         N_PROC       = 8;       // process count
    constexpr int         N_THR        = 4;       // thread per process

    using Map   = ShmTable<FixedString, int, CAPACITY>;
    using Block = ShmBlock<Map>;

    const char* SHM_PATH = "/shm_block_mp_test";
}

struct ShmBlockMpTest : public testing::Test {
protected:
    void SetUp() override {
        int fd = shm_open(SHM_PATH, O_CREAT|O_RDWR, 0600);
        ASSERT_NE(-1, fd);

        int ftruncate_ret = ftruncate(fd, Block::GetMemUsage());
        ASSERT_EQ(0, ftruncate_ret);

        sharedMem_ = mmap(nullptr, Block::GetMemUsage(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        ASSERT_NE(MAP_FAILED, sharedMem_);

        close(fd);
        // std::memset(sharedMem_, 0, Block::GetMemUsage());
    }

    void TearDown() override {
        munmap(sharedMem_, Block::GetMemUsage());
        shm_unlink(SHM_PATH);
    }

protected:
    void WorkerProcess() {
        int fd = shm_open(SHM_PATH, O_RDWR, 0600);
        void* mem = mmap(nullptr, Block::GetMemUsage(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);

        auto thread_fn = [this](int seed){
            Block* blk = (seed & 1) ? Block::Create(sharedMem_)
                                    : Block::Open(sharedMem_);

            std::mt19937_64 rng(seed*987654ull);
            std::uniform_int_distribution<int> kd(0, N_KEYS - 1);

            for(std::size_t i = 0; i < PER_PROC_OPS; ++i){
                int id = kd(rng);
                auto k = FixedString::FromFormat("%d", id);

                // 70 % write， 30 % read */
                if(i % 10 < 7){
                    (*blk)->Visit(k, AccessMode::CreateIfMiss, [](std::size_t id, int&v, bool) { 
                        v+=1; 
                    });

                } else {
                    (*blk)->Visit(k, AccessMode::AccessExist, [](std::size_t id, int&, bool){
                        ASSERT_TRUE(id < CAPACITY);
                    });
                }
            }
        };

        std::vector<std::thread> theadGroup;
        for(int t = 0; t < N_THR; ++t) {
            theadGroup.emplace_back(thread_fn, (int)getpid() * 100 + t);
        }
        for(auto& t:theadGroup) {
            t.join();
        }
        _exit(0);
    }

protected:
    void* sharedMem_{nullptr};
};
    
TEST_F(ShmBlockMpTest, shm_block_multi_process_race_test) 
{
    for(int i = 0; i < N_PROC; ++i){
        pid_t pid = fork();
        if(pid==0) WorkerProcess();
    }

    while(wait(nullptr) > 0) {}

    Block* blk = Block::Open(sharedMem_);

    long long total = 0;
    for(std::size_t id = 0; id < N_KEYS; ++id){
        auto k = FixedString::FromFormat("%d", id);
        (*blk)->Visit(k, AccessMode::AccessExist, [&](std::size_t id, int&v, bool){
            total+=v; 
        });
    }

    const long long expected = 1LL * N_PROC * N_THR * PER_PROC_OPS * 7 / 10;

    ASSERT_EQ(expected, total);
    
    std::cout << "✓ multi-process test passed\n";
    std::cout << "[MP] total="<<total<<"  expect="<<expected<<"\n";
}