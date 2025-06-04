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

#include "shmap/shm_ring_buffer.h"
#include "process_launcher.h"

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
// Cross-process single producer single consumer
// -----------------------------------------------------------------------------
TEST(ShmRingBufferTest, MultiProcessWithLauncher) {
    constexpr char   shm_name[] = "/shm_rb_test";
    constexpr size_t CAP   = 256;
    constexpr int    COUNT = 200;

    shm_unlink(shm_name);
    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, sizeof(ShmRingBuffer<int,CAP>)), 0);

    void* addr = mmap(nullptr, sizeof(ShmRingBuffer<int,CAP>), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    close(fd);

    auto* rb = new (addr) ShmRingBuffer<int,CAP>();
    rb->clear();

    ProcessLauncher launcher;

    /* ---- Launch one consumer processor ---- */
    auto consumer = launcher.Launch("rb_consumer", [rb] {
        std::vector<bool> seen(COUNT, false);
        int got = 0;
        while (got < COUNT) {
            auto o = rb->pop();
            if (!o) { usleep(50); continue; }
            int v = *o;
            if (v >= 0 && v < COUNT && !seen[v]) {
                seen[v] = true;
                ++got;
            }
        }
        for (int i = 0; i < COUNT; ++i) {
            if (!seen[i]) throw std::runtime_error("missing element");
        }
    });
    ASSERT_TRUE(consumer);

    /* ---- Parent processor produces data ---- */
    for (int i = 0; i < COUNT; ++i) {
        while (!rb->push(i)) usleep(10);
    }

    /* ---- Wait for consumer to finish ---- */
    auto res = launcher.Wait({consumer}, std::chrono::seconds(10));
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].status, Status::SUCCESS) << res[0].detail;

    launcher.Stop(consumer);
    munmap(addr, sizeof(ShmRingBuffer<int,CAP>));
    shm_unlink(shm_name);
}

// -----------------------------------------------------------------------------
// Multi-thread SPMC: 1 producer, multiple consumers share the work
// -----------------------------------------------------------------------------
TEST(ShmSpMcRingBufferTest, MultiThreadSPMC) {
    constexpr int PRODUCE   = 10000;
    constexpr int CONSUMERS = 4;

    ShmSpMcRingBuffer<int, 1024> rb;
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
    std::thread producer([&rb]() {
        for (int i = 0; i < PRODUCE; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
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
// Broadcast SPMC: 1 producer, multiple consumers share the work
// -----------------------------------------------------------------------------
TEST(BroadcastRingBuffer, MultiProcessLikeBroadcast) {
    constexpr int CAP       = 1024;
    constexpr int CONSUMERS = 3;
    constexpr int PRODUCE   = 50'000;

    BroadcastRingBuffer<int, CAP, CONSUMERS> rb;
    rb.init(CONSUMERS);

    std::array<BroadcastRingBuffer<int,CAP,CONSUMERS>::Consumer, CONSUMERS> consumers;
    for (int i = 0; i < CONSUMERS; ++i) {
        consumers[i] = rb.make_consumer();
    }

    std::vector<std::thread> consumer_ths;
    std::vector<std::vector<int>> seen(CONSUMERS);

    for (int id = 0; id < CONSUMERS; ++id) {
        consumer_ths.emplace_back([id, &consumers, &seen, PRODUCE]{
            seen[id].reserve(PRODUCE);
            while (seen[id].size() < static_cast<std::size_t>(PRODUCE)) {
                auto o = consumers[id].pop();
                if (!o) { 
                    std::this_thread::yield(); 
                    continue; 
                }
                seen[id].push_back(*o);
            }
        });
    }

    std::thread producer([&]{
        for (int i = 0; i < PRODUCE; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    for (auto& th: consumer_ths) th.join();

    for (int id = 0; id < CONSUMERS; ++id) {
        ASSERT_EQ(seen[id].size(), PRODUCE);
        for (int i = 0; i < PRODUCE; ++i) {
            EXPECT_EQ(seen[id][i], i);
        }
    }
}