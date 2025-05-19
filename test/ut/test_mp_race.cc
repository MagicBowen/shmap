#include <gtest/gtest.h> // 包含你刚才的 SharedBlock 版本
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

/* ---------- Hash map 型别 ---------- */
using Fix32 = std::array<char,32>;

struct Hash {
    std::size_t operator()(const Fix32&s) const noexcept {
        std::size_t h=14695981039346656037ull;
        for(char c:s){ if(!c) break;
            h^=(unsigned char)c; h*=1099511628211ull; }
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

using Map   = StaticUnifiedHashMap<Fix32,int,CAP,Hash,Eq>;
using Block = SharedBlock<Map>;

static Fix32 make_key(std::size_t id)
{
    Fix32 k{}; std::snprintf(k.data(),32,"key_%04zu",id); return k;
}

/* ---------------- 子进程逻辑 ---------------- */
void worker_process()
{
    /* 1. 打开同一块 shm 内存 */
    int fd = shm_open("/ufhm_demo", O_RDWR, 0600);
    void* mem = mmap(nullptr, Block::bytes(),
                     PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    /* 2. 线程体：随机写 / 读 */
    auto thread_fn = [mem](int seed){
        Block* blk = (seed & 1) ? Block::create(mem)
                                : Block::open(mem);
        Map* mp = &blk->map;

        std::mt19937_64 rng(seed*987654ull);
        std::uniform_int_distribution<int> kd(0, N_KEYS-1);

        for(std::size_t i=0;i<PER_PROC_OPS;++i){
            int id = kd(rng);
            Fix32 k = make_key(id);

            /* 70 % 写， 30 % 纯读 */
            if(i % 10 < 7){
                mp->visit(k, [](int&v,bool){ v+=1; }, true);
            } else {
                mp->visit(k, [](int&,bool){}, false);
            }
        }
    };

    /* 3. 启动本进程内的 4 个线程 */
    std::vector<std::thread> th;
    for(int t=0;t<N_THR;++t) th.emplace_back(thread_fn, (int)getpid()*100+t);
    for(auto& t:th) t.join();
    _exit(0);
}

/* ---------------- 父进程校验 ---------------- */

struct ShmapMpRaceTtest : public testing::Test {
protected:
};
    
TEST_F(ShmapMpRaceTtest, shm_race_test) 
{
    /* 1. 创建并 zero 共享内存 */
    shm_unlink("/ufhm_demo");
    int fd = shm_open("/ufhm_demo", O_CREAT|O_RDWR, 0600);
    ftruncate(fd, Block::bytes());
    void* mem = mmap(nullptr, Block::bytes(),
                     PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    std::memset(mem, 0, Block::bytes());

    /* 2. fork N_PROC 个子进程 */
    for(int i=0;i<N_PROC;++i){
        pid_t pid = fork();
        if(pid==0) worker_process();
    }

    /* 3. 等待全部子进程结束 */
    while(wait(nullptr)>0) {}

    /* 4. 校验结果 */
    Block* blk = Block::open(mem);
    Map*   mp  = &blk->map;

    long long total = 0;
    for(std::size_t id=0; id<N_KEYS; ++id){
        Fix32 k = make_key(id);
        mp->visit(k,[&](int&v,bool){ total+=v; }, false);
    }

    const long long expected =
        1LL * N_PROC * N_THR * PER_PROC_OPS * 7 / 10; // 70% 写操作

    std::cout << "[MP] total="<<total<<"  expect="<<expected<<"\n";
    assert(total == expected);

    munmap(mem, Block::bytes());
    shm_unlink("/ufhm_demo");
    std::cout << "✓ multi-process test passed\n";
}