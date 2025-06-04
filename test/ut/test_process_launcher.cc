#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <random>
#include "process_launcher.h"

using namespace shmap;

TEST(ProcessLauncher, Basic) {
    ProcessLauncher launcher;

    Processor p1 = launcher.Launch("worker1", []{
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[child1] first task\n";
    });

    Processor p2 = launcher.Launch("worker2", []{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << "[child2] first task\n";
    });

    auto res1 = launcher.Wait({p1, p2}, std::chrono::milliseconds(1000));
    for (const auto& r : res1) {
        std::cout << r.name << " -> " << r.status << " (" << r.detail << ")\n";
    }

    launcher.Dispatch(p1, []{
        std::cout << "[child1] second task\n";
    });
    launcher.Dispatch(p2, []{
        throw std::runtime_error("boom");
    });

    auto res2 = launcher.Wait({p1, p2}, std::chrono::milliseconds(1000));
    for (const auto& r : res2) {
        std::cout << r.name << " -> " << r.status << " (" << r.detail << ")\n";
    }

    launcher.Stop(p1, p2);
}

TEST(ProcessLauncher, NormalAndException) {
    ProcessLauncher launcher;
    auto p1 = launcher.Launch("ok", []{});
    auto p2 = launcher.Launch("exp", []{ throw std::runtime_error("err"); });
    auto p3 = launcher.Launch("crash", [](){ std::raise(SIGSEGV); });
    auto p4 = launcher.Launch("crash", [](){
        *((volatile int*)nullptr) = 1;
    });

    auto r = launcher.Wait({p1, p2, p3, p4}, std::chrono::milliseconds(500));
    ASSERT_EQ(r.size(), 4u);

    EXPECT_EQ(r[0].status, shmap::Status::SUCCESS);
    EXPECT_EQ(r[1].status, shmap::Status::EXCEPTION);
    EXPECT_EQ(r[2].status, shmap::Status::CRASH);
    EXPECT_EQ(r[3].status, shmap::Status::CRASH);

    launcher.Stop(p1, p2);
}

TEST(ProcessLauncher, Timeout) {
    ProcessLauncher launcher;

    auto p = launcher.Launch("sleep", []{
        std::this_thread::sleep_for(std::chrono::seconds(2));
    });

    auto r = launcher.Wait({p}, std::chrono::milliseconds(300));

    ASSERT_EQ(r[0].status, shmap::Status::TIMEOUT);
    launcher.Stop(p);
}

TEST(ProcessLauncher, MonteCarloPiConcurrentHeavy) {
    constexpr int kWorkers  = 4;          // 子进程个数
    constexpr int kThreads  = 4;          // 每个子进程里的线程数
    constexpr size_t kTotal = 2'000'000;  // 每个子进程要抛的随机点

    ProcessLauncher launcher;

    /* ---- 准备一块父子共享的结果缓存 ---- */
    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    void* mem = ::mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    ASSERT_NE(mem, MAP_FAILED);

    auto* results = static_cast<double*>(mem);
    std::fill(results, results + kWorkers, 0.0);

    /* ---- 启动子进程 ---- */
    std::vector<Processor> ps;
    ps.reserve(kWorkers);

    for (int wi = 0; wi < kWorkers; ++wi) {
        ps.push_back(launcher.Launch("pi_" + std::to_string(wi),
            [wi, results] {
                std::atomic<size_t> hit{0};

                auto oneThread = [&](size_t begin, size_t end) {
                    std::mt19937_64 gen(static_cast<uint64_t>(begin) ^ static_cast<uint64_t>(getpid()));
                    std::uniform_real_distribution<double> dist(0.0, 1.0);

                    size_t localHit = 0;
                    for (size_t i = begin; i < end; ++i) {
                        double x = dist(gen);
                        double y = dist(gen);
                        if (x * x + y * y <= 1.0) ++localHit;
                    }
                    hit.fetch_add(localHit, std::memory_order_relaxed);
                };

                size_t chunk = kTotal / kThreads;
                std::vector<std::thread> threads;
                threads.reserve(kThreads);

                for (int t = 0; t < kThreads; ++t) {
                    size_t begin = static_cast<size_t>(t) * chunk;
                    size_t end   = (t == kThreads - 1) ? kTotal : begin + chunk;
                    threads.emplace_back(oneThread, begin, end);
                }
                for (auto& th : threads) th.join();

                double pi = 4.0 * static_cast<double>(hit.load()) /
                            static_cast<double>(kTotal);

                /* 将结果写回共享区，之后父进程可直接读取 */
                results[wi] = pi;
                /* 内存屏障，确保结果对其他进程可见 */
                __sync_synchronize();
            }));
        ASSERT_TRUE(ps.back()) << "launch worker " << wi << " failed";
    }

    /* ---- 等待所有子进程任务结束（10s 超时） ---- */
    auto res = launcher.Wait(ps, std::chrono::seconds(10));

    /* ---- 断言执行状态 & 结果正确性 ---- */
    for (const auto& r : res) {
        EXPECT_EQ(r.status, Status::SUCCESS) << r.name << " detail=" << r.detail;
    }

    for (int i = 0; i < kWorkers; ++i) {
        double pi = results[i];
        EXPECT_GT(pi, 3.0)  << "worker " << i;
        EXPECT_LT(pi, 3.3)  << "worker " << i;   // 允许 ±0.15 误差
    }

    /* ---- 关闭子进程 ---- */
    launcher.Stop(ps[0], ps[1], ps[2], ps[3]);
    ::munmap(mem, page);
}