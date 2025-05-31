/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_BACKOFF_H
#define SHMAP_BACKOFF_H

#include <chrono>
#include <thread>

namespace shmap {

struct Backoff {
    Backoff(std::chrono::nanoseconds to)
        : start_(Clock::now()), timeout_(to) {}

    // one backoff step; return false if overall timeout exceeded
    bool next() {
        if (Clock::now() - start_ > timeout_) return false;

        if (spin_ < YIELD_LIMIT) {
            std::this_thread::yield();
        } else {
            int  expected = std::min(spin_ - YIELD_LIMIT, MAX_BACKOFF_EXP);
            auto nanos = std::chrono::nanoseconds(1LL << expected);
            std::this_thread::sleep_for(nanos);
        }
        ++spin_;
        return true;
    }
private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_;
    std::chrono::nanoseconds timeout_;
    int spin_{0};

    // first N attempts use yield()
    static constexpr int  YIELD_LIMIT      = 10;
    // exponent cap: 1 << MAX_BACKOFF_EXP ns ~1ms
    static constexpr int  MAX_BACKOFF_EXP  = 20;
};

}

#endif
