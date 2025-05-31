/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_SHM_VECTOR_H
#define SHMAP_SHM_VECTOR_H

#include "shmap/shmap.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace shmap {

template <typename T, std::size_t N>
struct ShmVector {
    static_assert(std::is_trivial_v<T>, "T must be trivial type");
    static_assert(N > 0, "Size must be positive");  
    
    std::size_t capacity() const noexcept {
         return N; 
    }

    std::size_t size() const noexcept {
        return size_.load(std::memory_order_acquire);
    }

    bool empty() const noexcept { 
        return size() == 0; 
    }

    void clear() noexcept {
        size_.store(0, std::memory_order_release);
    }

    // Reserve `n` slots atomically. Returns starting index or nullopt on overflow.
    std::optional<std::size_t> allocate(std::size_t n) noexcept {
        std::size_t old = size_.load(std::memory_order_relaxed);
        do {
            if (old + n > N) {
                return std::nullopt;
            }
        } while (!size_.compare_exchange_weak(
            old, old + n, 
            std::memory_order_acq_rel, 
            std::memory_order_relaxed
        ));
        return old;        
    }

    // Convenience single‐element push_back. Returns index or nullopt on overflow.
    std::optional<std::size_t> push_back(const T& v) noexcept {
        auto idx = allocate(1);
        if (!idx) {
            return std::nullopt;
        }
        data_[*idx] = v;
        return idx;
    }

    // Element access
    T& operator[](std::size_t i) noexcept {
        assert(i < N && "Index out of bounds");
        assert(i < size_.load(std::memory_order_acquire));
        return data_[i]; 
    }

    const T& operator[](std::size_t i) const noexcept { 
        assert(i < N && "Index out of bounds");
        assert(i < size_.load(std::memory_order_acquire));
        return data_[i]; 
    }

    // Iterators
    T* begin() noexcept { return data_.begin(); }
    T* end() noexcept { return data_.begin() + size(); }
    const T* begin() const noexcept { return data_.begin(); }
    const T* end() const noexcept { return data_.begin() + size(); }

private:
    alignas(alignof(T)) std::array<T, N> data_;
    std::atomic<std::size_t> size_{0};
};

}

#endif
