#include <gtest/gtest.h>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <fcntl.h>

#include "shm_vector.h"

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
    ShmVector<long, 100> v{};
    // Reserve 5 slots
    auto off1 = v.allocate(5);
    ASSERT_TRUE(off1.has_value());
    for (int i = 0; i < 5; ++i) {
        v[*off1 + i] = 100 + i;
    }
    // Reserve another 10
    auto off2 = v.allocate(10);
    ASSERT_TRUE(off2.has_value());
    for (int i = 0; i < 10; ++i) {
        v[*off2 + i] = 200 + i;
    }
    EXPECT_EQ(v.size(), 15u);

    // Check contents
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(v[*off1 + i], 100 + i);
    }
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(v[*off2 + i], 200 + i);
    }
}

TEST(ShmVectorTest, MultiThreadedPushBack) {
    ShmVector<int, 10000> v{};
    const int nthreads = 8;
    const int per_thread = 1000;
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

TEST(ShmVectorTest, MultiProcessSharedMemory) {
    const char* shm_name = "/shm_vector_test";
    const std::size_t CAP = 1024;
    shm_unlink(shm_name);

    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0) << "shm_open failed";

    ASSERT_EQ(ftruncate(fd, sizeof(ShmVector<int, CAP>)), 0);

    void* addr = mmap(nullptr, sizeof(ShmVector<int, CAP>), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    close(fd);

    auto* v = new (addr) ShmVector<int, CAP>{};

    const int nproc = 4;
    const int per_proc = 100;
    std::vector<pid_t> pids;

    for (int p = 0; p < nproc; ++p) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);

        if (pid == 0) {
            // child
            auto off = v->allocate(per_proc);
            if (!off) _exit(1);
            for (int i = 0; i < per_proc; ++i) {
                (*v)[*off + i] = p * per_proc + i;
            }
            _exit(0);
        } else {
            pids.push_back(pid);
        }
    }
    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    EXPECT_EQ(v->size(), nproc * per_proc);
    std::vector<bool> seen(nproc * per_proc, false);
    for (std::size_t i = 0; i < v->size(); ++i) {
        int x = (*v)[i];
        ASSERT_GE(x, 0);
        ASSERT_LT(x, nproc * per_proc);
        seen[x] = true;
    }
    for (bool b : seen) {
        EXPECT_TRUE(b);
    }

    munmap(addr, sizeof(ShmVector<int, CAP>));
    shm_unlink(shm_name);
}