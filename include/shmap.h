/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_HPP_
#define SHMAP_HPP_

#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <atomic>
#include <thread>
#include <new>

namespace shmap {

#if __cpp_lib_hardware_interference_size >= 201603
    constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

template<typename KEY, typename VALUE>
struct ShmBucket {
    static constexpr uint32_t EMPTY = 0;
    static constexpr uint32_t INSERTING = 1;
    static constexpr uint32_t READY = 2;
    static constexpr uint32_t ACCESSING = 3;

    std::atomic<uint32_t> state{EMPTY};
    KEY key;
    VALUE value;

private:
    static constexpr std::size_t PAYLOAD_SIZE = 
        sizeof(std::atomic<uint32_t>) + sizeof(KEY) + sizeof(VALUE);

    static constexpr std::size_t PADDING_SIZE = 
        (CACHE_LINE_SIZE - (PAYLOAD_SIZE % CACHE_LINE_SIZE)) % CACHE_LINE_SIZE;

    char pad_[PADDING_SIZE ? PADDING_SIZE : 1];
};

template<typename KEY, typename VALUE, std::size_t CAPACITY,
    typename HASH  = std::hash<KEY>,
    typename EQUAL = std::equal_to<KEY>
>
class ShmTable {
    static_assert(CAPACITY > 0, "CAPACITY must be > 0");
    static_assert(std::is_trivially_copyable<KEY>::value,   "KEY must be trivially copyable");
    static_assert(std::is_trivially_copyable<VALUE>::value, "VALUE must be trivially copyable");
    
    using Bucket = ShmBucket<KEY,VALUE>;
    static_assert(sizeof(Bucket) % CACHE_LINE_SIZE == 0,  "Bucket must be cache-line multiple");

public:
    static constexpr std::size_t GetMemUsage() noexcept { 
        return sizeof(ShmTable); 
    }

    template<typename Visitor>
    bool Visit(const KEY& k, Visitor&& visit, bool create_if_missing = false) noexcept {

        std::size_t idx = hasher_(k) % CAPACITY;

        for(std::size_t probe = 0; probe < CAPACITY; ++probe, idx = (idx + 1) % CAPACITY) {

            Bucket& b = buckets_[idx];

            while(true) {
                auto curState = b.state.load(std::memory_order_acquire);

                // Already has item
                if(curState == Bucket::READY) {
                    if(!key_equal_(b.key, k)) {
                        /* hash conflict, jump out to find next*/
                        break;
                    }

                    // Preempt the write permission to ensure exclusivity
                    auto expectedState = Bucket::READY;
                    if(!b.state.compare_exchange_strong(expectedState, Bucket::ACCESSING,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                       // Occupied by other writers, retry or yield in next loop
                        continue;
                    }

                    // Begin accessing
                    std::forward<Visitor>(visit)(b.value, false);
                    b.state.store(Bucket::READY, std::memory_order_release);
                    return true;
                }

                // Try inserting
                if(curState == Bucket::EMPTY && create_if_missing) {
                    auto expectedState = Bucket::EMPTY;
                    if(!b.state.compare_exchange_strong(expectedState, Bucket::INSERTING,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                        /* Race failed, retry or yield in next loop */
                        continue;
                    }

                    // Inserting key/value
                    b.key   = k;
                    b.value = VALUE{}; // Default construct
                    std::forward<Visitor>(visit)(b.value, true); // Modify content by visitor

                    b.state.store(Bucket::READY, std::memory_order_release);
                    return true;
                }

                // Only read but find none
                if(curState == Bucket::EMPTY && !create_if_missing) {
                    return false;
                }

                // Waiting for inserting or modifying to end
                std::this_thread::yield();
            }
        }
        return false; // Probe failed
    }

private:
    // Only used for placement-new
    ShmTable() = default;

    alignas(CACHE_LINE_SIZE) Bucket buckets_[CAPACITY];
    HASH   hasher_{};
    EQUAL key_equal_{};

    template<typename T> friend struct ShmBlock;
};

template<typename TABLE>
struct ShmBlock {
    std::atomic<uint32_t> state { UNINIT };
    TABLE table_;

    static constexpr std::size_t GetMemUsage() noexcept { 
        return sizeof(ShmBlock); 
    }

    static ShmBlock* Create(void* mem) noexcept {
        ShmBlock* block = static_cast<ShmBlock*>(mem);
        uint32_t expectedState = UNINIT;
        if (block->state.compare_exchange_strong(expectedState, BUILDING,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            new (&block->table_) TABLE();
            block->state.store(READY, std::memory_order_release);
            return block;
        }
        else {
            block->WaitReady();
            return block;
        }
    }

    static ShmBlock* Open(void* mem) noexcept {
        auto* block = static_cast<ShmBlock*>(mem);
        block->WaitReady();
        return block;
    }

private:
    static constexpr uint32_t UNINIT = 0;
    static constexpr uint32_t BUILDING = 1;
    static constexpr uint32_t READY = 2;

private:
    ShmBlock() = default;

    void WaitReady() noexcept{
        while (state.load(std::memory_order_acquire) != READY)
            std::this_thread::yield();
    }
};

}

#endif