/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_SHM_RING_BUFFER_H
#define SHMAP_SHM_RING_BUFFER_H

#include "shmap/shmap.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace shmap {

template <typename T, std::size_t N>
struct ShmRingBuffer {
    static_assert(std::is_trivially_copyable<T>::value,  "T must be trivially copyable");
    static_assert(std::is_standard_layout<T>::value, "T should be standard layout!");

    static_assert(N > 0, "Size must be positive");

    std::size_t capacity() const noexcept {
        return N;
    }

    std::size_t size() const noexcept {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_acquire);
        return t - h;
    }

    bool empty() const noexcept {
        return size() == 0;
    }

    bool full() const noexcept {
        return size() >= N;
    }

    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    // single-producer push, returns false if buffer is full
    bool push(const T& v) noexcept {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_relaxed);
        if (t - h >= N) {
            return false; // full
        }
        data_[t % N] = v;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // multiple-consumer pop, returns nullopt if empty
    std::optional<T> pop() noexcept {
        std::size_t h, t;
        do {
            h = head_.load(std::memory_order_relaxed);
            t = tail_.load(std::memory_order_acquire);
            if (h >= t) {
                return std::nullopt; // empty
            }
        } while (!head_.compare_exchange_weak(
                     h, h + 1,
                     std::memory_order_acq_rel,
                     std::memory_order_relaxed));
        // successfully claimed slot at h
        return data_[h % N];
    }

private:
    alignas(alignof(T)) std::array<T, N> data_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};
};

}

#endif
