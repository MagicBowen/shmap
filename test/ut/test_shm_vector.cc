#include <gtest/gtest.h>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <fcntl.h>

#include "shmap/shm_vector.h"
#include "process_launcher.h"

using namespace shmap;

TEST(ShmVectorTest, BasicSingleThread) {
    ShmVector<int, 16> v{};
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0u);
    EXPECT_EQ(v.capacity(), 16u);

    // push_back
    for (int i = 0; i < 10; ++i) {
        auto idx = v.push_back(i * 2);
        ASSERT_TRUE(idx.has_value());
        EXPECT_EQ(*idx, (std::size_t)i);
    }
    EXPECT_EQ(v.size(), 10u);

    // direct operator[]
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(v[i], i * 2);
    }

    // range‐for
    int sum = 0;
    for (auto x : v) {
        sum += x;
    }
    // sum of 0,2,4,...,18 = 90
    EXPECT_EQ(sum, 90);

    // find_if
    auto it = std::find_if(v.begin(), v.end(), [](int x) { return x == 6; });
    EXPECT_NE(it, v.end());
    EXPECT_EQ(*it, 6);
}

TEST(ShmVectorTest, AllocateBlocks) {
    ShmVector<long, 128> v{};
    // Reserve 5 slots
    auto off1 = v.allocate(5);
    ASSERT_TRUE(off1.has_value());
    for (int i = 0; i < 5; ++i) {
        v[*off1 + i] = 128 + i;
    }
    // Reserve another 10
    auto off2 = v.allocate(10);
    ASSERT_TRUE(off2.has_value());
    for (int i = 0; i < 10; ++i) {
        v[*off2 + i] = 256 + i;
    }
    EXPECT_EQ(v.size(), 15u);

    // Check contents
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(v[*off1 + i], 128 + i);
    }
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(v[*off2 + i], 256 + i);
    }
}

TEST(ShmVectorTest, MultiThreadedPushBack) {
    ShmVector<int, 8192> v{};
    const int nthreads = 8;
    const int per_thread = 1024;
    std::vector<std::thread> threads;
    threads.reserve(nthreads);
    for (int t = 0; t < nthreads; ++t) {
        threads.emplace_back([t,&v](){
        for (int i = 0; i < per_thread; ++i) {
            int val = t * per_thread + i;
            auto idx = v.push_back(val);
            // must succeed
            if (!idx) {
                std::abort();
            }
        }
        });
    }
    for (auto &th : threads) th.join();
    EXPECT_EQ(v.size(), nthreads * per_thread);

    // Check all values appear exactly once
    std::vector<bool> seen(nthreads * per_thread, false);
    for (auto x : v) {
        ASSERT_GE(x, 0);
        ASSERT_LT(x, nthreads * per_thread);
        seen[x] = true;
    }
    for (bool b : seen) {
        EXPECT_TRUE(b);
    }
}

// =============================================================================
// Multi-Process Test (POSIX shared memory + fork)
// =============================================================================
TEST(ShmVectorTest, MultiProcessSharedMemoryWithLauncher) {
    constexpr char  shm_name[] = "/shm_vector_test";
    constexpr size_t CAP       = 1024;
    constexpr int    NPROC     = 4;
    constexpr int    PER_PROC  = 100;

    shm_unlink(shm_name);
    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, sizeof(ShmVector<int,CAP>)), 0);

    void* addr = mmap(nullptr, sizeof(ShmVector<int,CAP>), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    close(fd);

    auto* vec = new (addr) ShmVector<int,CAP>{};

    ProcessLauncher launcher;
    std::vector<Processor> procs;
    procs.reserve(NPROC);

    /* ----- Launch NPROC 子进程，每个进程写 PER_PROC 个元素 ----- */
    for (int p = 0; p < NPROC; ++p) {
        procs.emplace_back(
            launcher.Launch("vec_writer_" + std::to_string(p),
                [p, vec] {
                    auto off = vec->allocate(PER_PROC);
                    if (!off) throw std::runtime_error("allocate failed");

                    for (int i = 0; i < PER_PROC; ++i) {
                        (*vec)[*off + i] = p * PER_PROC + i;
                    }
                }));
        ASSERT_TRUE(procs.back());
    }

    auto results = launcher.wait(procs, std::chrono::seconds(5));

    for (auto& r : results) {
        EXPECT_EQ(r.status, Status::SUCCESS) << r.detail;
    }

    launcher.Stop(procs);

    /* ----- 校验向量内容完整性 ----- */
    EXPECT_EQ(vec->size(), static_cast<size_t>(NPROC * PER_PROC));

    std::vector<bool> seen(NPROC * PER_PROC, false);
    for (std::size_t i = 0; i < vec->size(); ++i) {
        int v = (*vec)[i];
        ASSERT_GE(v, 0);
        ASSERT_LT(v, NPROC * PER_PROC);
        seen[v] = true;
    }
    for (bool b : seen) {
        EXPECT_TRUE(b);
    }

    munmap(addr, sizeof(ShmVector<int,CAP>));
    shm_unlink(shm_name);
}