// shm_ring_buffer_test.cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>

#include "shm_ring_buffer.h"

using namespace shmap;

// -----------------------------------------------------------------------------
// Basic single-thread
// -----------------------------------------------------------------------------
TEST(ShmRingBufferTest, BasicSingleThread) {
    ShmRingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.capacity(), 8u);

    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(rb.push(i));
        EXPECT_EQ(rb.size(), static_cast<std::size_t>(i + 1));
    }
    EXPECT_FALSE(rb.push(100));
    EXPECT_TRUE(rb.full());

    for (int i = 0; i < 8; ++i) {
        auto o = rb.pop();
        ASSERT_TRUE(o.has_value());
        EXPECT_EQ(*o, i);
    }
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.pop().has_value());
}

// -----------------------------------------------------------------------------
// Multi-thread SPMC: 1 producer, multiple consumers share the work
// -----------------------------------------------------------------------------
TEST(ShmRingBufferTest, MultiThreadSPMC) {
    constexpr int PRODUCE   = 10000;
    constexpr int CONSUMERS = 4;

    ShmRingBuffer<int, 1024> rb;
    std::atomic<int> consumed{0};
    std::vector<std::vector<bool>> seen(CONSUMERS, std::vector<bool>(PRODUCE, false));
    std::vector<std::thread> consumers;

    // consumers
    for (int c = 0; c < CONSUMERS; ++c) {
        consumers.emplace_back([&rb, &consumed, &seen, c]() {
            while (true) {
                int done = consumed.load(std::memory_order_acquire);
                if (done >= PRODUCE) break;
                auto o = rb.pop();
                if (!o) {
                    std::this_thread::yield();
                    continue;
                }
                int v = *o;
                if (v >= 0 && v < PRODUCE) {
                    seen[c][v] = true;
                    consumed.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        });
    }

    // producer
    std::thread prod([&rb]() {
        for (int i = 0; i < PRODUCE; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    prod.join();
    for (auto& th : consumers) th.join();

    // aggregate
    std::vector<bool> agg(PRODUCE, false);
    for (int c = 0; c < CONSUMERS; ++c) {
        for (int i = 0; i < PRODUCE; ++i) {
            if (seen[c][i]) agg[i] = true;
        }
    }
    for (int i = 0; i < PRODUCE; ++i) {
        EXPECT_TRUE(agg[i]) << "value " << i << " missing";
    }
}

// -----------------------------------------------------------------------------
// Cross-process single producer single consumer
// -----------------------------------------------------------------------------
TEST(ShmRingBufferTest, MultiProcess) {
    const char* shm_name = "/shm_rb_test";
    constexpr std::size_t CAP   = 256;
    constexpr int         COUNT = 200;

    shm_unlink(shm_name);
    int fd = shm_open(shm_name, O_CREAT|O_EXCL|O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, sizeof(ShmRingBuffer<int,CAP>)), 0);

    void* addr = mmap(nullptr, sizeof(ShmRingBuffer<int,CAP>),
                      PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    close(fd);

    auto* rb = new (addr) ShmRingBuffer<int,CAP>();
    rb->clear();

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        // consumer in child
        std::vector<bool> seen(COUNT, false);
        int got = 0;
        while (got < COUNT) {
            auto o = rb->pop();
            if (!o) {
                usleep(50);
                continue;
            }
            int v = *o;
            if (v >= 0 && v < COUNT && !seen[v]) {
                seen[v] = true;
                ++got;
            }
        }
        for (int i = 0; i < COUNT; ++i) {
            if (!seen[i]) _exit(1);
        }
        _exit(0);
    } else {
        // producer in parent
        for (int i = 0; i < COUNT; ++i) {
            while (!rb->push(i)) {
                usleep(10);
            }
        }
        int status = 0;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        munmap(addr, sizeof(ShmRingBuffer<int,CAP>));
        shm_unlink(shm_name);
    }
}
