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

using namespace shmap;

namespace {
    using Fix32 = std::array<char,32>;

    struct Hash {
        std::size_t operator()(const Fix32&s) const noexcept {
            std::size_t h=14695981039346656037ull;
            for(char c:s){ 
                if(!c) break;
                h^=(unsigned char)c; 
                h*=1099511628211ull; 
            }
            return h;
        }
    };
    struct Eq {
        bool operator()(const Fix32&a,const Fix32&b) const noexcept {
            return std::memcmp(a.data(),b.data(),32)==0;
        }
    };

    constexpr std::size_t CAP     = 1<<14;   // 16 384 buckets
    constexpr std::size_t N_KEYS  = 1'000;
    constexpr std::size_t PER_PROC_OPS = 50'000;
    constexpr int         N_PROC  = 8;       // 并发进程数
    constexpr int         N_THR   = 4;       // 每进程再起 4 线程

    using Map   = ShmTable<Fix32, int, CAP, Hash, Eq>;
    using Block = ShmBlock<Map>;

    static Fix32 make_key(std::size_t id) {
        Fix32 k{}; 
        std::snprintf(k.data(),32,"key_%04zu",id); 
        return k;
    } 

    const char* SHM_PATH = "/shmap_mp_test";
}

struct ShmapMpRaceTtest : public testing::Test {
protected:
    void SetUp() override {
        int fd = shm_open(SHM_PATH, O_CREAT|O_RDWR, 0600);
        ASSERT_NE(-1, fd);

        int ftruncate_ret = ftruncate(fd, Block::GetMemUsage());
        ASSERT_EQ(0, ftruncate_ret);

        sharedMem_ = mmap(nullptr, Block::GetMemUsage(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        ASSERT_NE(MAP_FAILED, sharedMem_);

        close(fd);
        std::memset(sharedMem_, 0, Block::GetMemUsage());
    }

    void TearDown() override {
        munmap(sharedMem_, Block::GetMemUsage());
        shm_unlink(SHM_PATH);
    }

protected:
    void worker_process() {
        int fd = shm_open(SHM_PATH, O_RDWR, 0600);
        void* mem = mmap(nullptr, Block::GetMemUsage(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);

        auto thread_fn = [this](int seed){
            Block* blk = (seed & 1) ? Block::Create(sharedMem_)
                                    : Block::Open(sharedMem_);
            Map& mp = blk->table_;

            std::mt19937_64 rng(seed*987654ull);
            std::uniform_int_distribution<int> kd(0, N_KEYS - 1);

            for(std::size_t i = 0; i < PER_PROC_OPS; ++i){
                int id = kd(rng);
                Fix32 k = make_key(id);

                /* 70 % 写， 30 % 纯读 */
                if(i % 10 < 7){
                    mp.Visit(k, [](int&v,bool){ v+=1; }, true);
                } else {
                    mp.Visit(k, [](int&,bool){}, false);
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
    
TEST_F(ShmapMpRaceTtest, shm_race_test) 
{
    for(int i = 0; i < N_PROC; ++i){
        pid_t pid = fork();
        if(pid==0) worker_process();
    }

    while(wait(nullptr) > 0) {}

    Block* blk = Block::Open(sharedMem_);
    Map& mp = blk->table_;

    long long total = 0;
    for(std::size_t id = 0; id < N_KEYS; ++id){
        Fix32 k = make_key(id);
        mp.Visit(k,
            [&](int&v,bool){
                 total+=v; 
            }, 
            false
        );
    }

    const long long expected = 1LL * N_PROC * N_THR * PER_PROC_OPS * 7 / 10;

    ASSERT_EQ(expected, total);
    
    std::cout << "✓ multi-process test passed\n";
    std::cout << "[MP] total="<<total<<"  expect="<<expected<<"\n";
}