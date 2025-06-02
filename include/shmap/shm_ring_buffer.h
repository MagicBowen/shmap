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

/* -------------------------------------------------------------------------- */
/*                              ShmRingBugger - SPSC                          */
/* -------------------------------------------------------------------------- */
template <typename T, std::size_t N>
struct ShmRingBuffer {
    static_assert(std::is_trivially_copyable<T>::value,  "T must be trivially copyable");
    static_assert(std::is_standard_layout<T>::value, "T should be standard layout!");
    static_assert(N && (N & (N - 1)) == 0,  "Capacity must be power of two for cheap modulo");

    std::size_t capacity() const noexcept {
        return N;
    }

    std::size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
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


/* -------------------------------------------------------------------------- */
/*       ShmRingBugger - SPMC （only one consumer fetches data success）       */
/* -------------------------------------------------------------------------- */
template <typename T, std::size_t N>
struct ShmSpMcRingBuffer {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_standard_layout<T>::value, "T should be standard layout!");
    static_assert(N && (N & (N - 1)) == 0,  "Capacity must be power of two for cheap modulo");

    ShmSpMcRingBuffer() noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            buf_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    bool push(const T& v) noexcept {
        std::size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            Cell& c = buf_[pos & (N - 1)];
            std::size_t seq = c.seq.load(std::memory_order_acquire);
            intptr_t diff   = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {// empty cell
                if (tail_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    c.data = v;
                    c.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    std::optional<T> pop() noexcept {
        std::size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            Cell& c = buf_[pos & (N - 1)];
            std::size_t seq = c.seq.load(std::memory_order_acquire);
            intptr_t diff   = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) { // has data to be read
                if (head_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    T v = c.data;
                    c.seq.store(pos + N, std::memory_order_release); // mark empty
                    return v;
                }
            } else if (diff < 0) {
                return std::nullopt; // empty
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }

    std::size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        for (std::size_t i = 0; i < N; ++i)
            buf_[i].seq.store(i, std::memory_order_relaxed);
    }

private:
    struct Cell {
        std::atomic<std::size_t> seq;
        T data;
    };

private:
    alignas(CACHE_LINE_SIZE) std::array<Cell, N> buf_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};
};

/* -------------------------------------------------------------------------- */
/*           ShmRingBugger - SPMC (all consumers fetch data success）         */
/* -------------------------------------------------------------------------- */
template<typename T, std::size_t N, std::size_t MAX_CONCUMER = 8>
struct BroadcastRingBuffer {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_standard_layout<T>::value, "T should be standard layout!");
    static_assert((N & (N - 1)) == 0, "N must be power-of-two");

    // parent processor invoke once
    void init(std::uint32_t consumers) noexcept {
        consumer_cnt_.store(consumers, std::memory_order_relaxed);
        for (std::size_t i = 0; i < N; ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed); 
        }
    }

    bool push(const T& v) noexcept {
        const std::size_t pos = tail_.fetch_add(1, std::memory_order_relaxed);
        Slot& s = slots_[pos & (N - 1)];

        while (s.remain.load(std::memory_order_acquire) != 0) {
            std::this_thread::yield();
        }

        s.data = v;

        s.seq.store(pos, std::memory_order_release);
        s.remain.store(consumer_cnt_.load(std::memory_order_relaxed), std::memory_order_release);
        return true;
    }

    struct Consumer {
        BroadcastRingBuffer* rb{};
        std::size_t cursor{0};

        std::optional<T> pop() noexcept {
            Slot& slot = rb->slots_[cursor & (N - 1)];
            uint64_t seq = slot.seq.load(std::memory_order_acquire);
            if (seq != cursor) {
                return std::nullopt;
            }

            uint32_t r = slot.remain.load(std::memory_order_acquire);
            if (r == 0) {
                return std::nullopt;
            }

            T v = slot.data;

            slot.remain.fetch_sub(1, std::memory_order_acq_rel);
            ++cursor;
            return v;
        }
    };

    Consumer make_consumer() noexcept { 
        return Consumer{this, 0}; 
    }

private:
    struct Slot {
        alignas(64) std::atomic<uint64_t> seq{0};
        T data;
        std::atomic<uint32_t> remain{0};
    };

private:
    alignas(64) std::array<Slot, N> slots_{};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::atomic<uint32_t> consumer_cnt_{0};
};

}

#endif
