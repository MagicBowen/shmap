/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_SHM_HASH_TABLE_H_
#define SHMAP_SHM_HASH_TABLE_H_

#include "shmap/shmap.h"
#include "shmap/backoff.h"
#include "shmap/status.h"

#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <atomic>

namespace shmap {

/* -------------------------------------------------------------------------- */
/*                     ShmBucket – bucket for closed hashing table            */
/* -------------------------------------------------------------------------- */    
template<typename KEY, typename VALUE>
struct alignas(CACHE_LINE_SIZE) ShmBucket {
    static constexpr uint32_t EMPTY = 0;
    static constexpr uint32_t INSERTING = 1;
    static constexpr uint32_t READY = 2;
    static constexpr uint32_t ACCESSING = 3;

    std::atomic<uint32_t> state{EMPTY};
    KEY key;
    VALUE value;
};

/* -------------------------------------------------------------------------- */
/*                     AccessMode – how to access the table                   */
/* -------------------------------------------------------------------------- */
enum class AccessMode : uint8_t {
    AccessExist,
    CreateIfMiss,
};

/* -------------------------------------------------------------------------- */
/*                     ShmHashTable  (lock-free closed hashing table)         */
/* -------------------------------------------------------------------------- */
template<typename KEY, typename VALUE, std::size_t CAPACITY,
    typename HASH  = std::hash<KEY>,
    typename EQUAL = std::equal_to<KEY>,
    bool ROLLBACK_ENABLE = false
>
struct ShmHashTable {
    static_assert(CAPACITY > 0, "CAPACITY must be > 0");
    static_assert(std::is_trivial_v<KEY>,   "KEY must be trivial type");
    static_assert(std::is_trivial_v<VALUE>, "VALUE must be trivial type");

    using Bucket = ShmBucket<KEY,VALUE>;
    static_assert(sizeof(Bucket) % CACHE_LINE_SIZE == 0,  "Bucket must be cache-line multiple");

    ShmHashTable() = default; // Only used for placement-new

    template<typename Visitor /* Status (idx, Value&, bool isNew) */>
    Status Visit(const KEY& key, AccessMode mode, Visitor&& visitor,
        std::chrono::nanoseconds timeout = std::chrono::seconds(5)) noexcept {
            
        Backoff backoff(timeout);
        const std::size_t idx = hasher_(key) % CAPACITY;

        for (std::size_t probe = 0; probe < CAPACITY; ++probe) {
            Bucket& b = buckets_[(idx + probe) % CAPACITY];

            while (true) {
                auto state = b.state.load(std::memory_order_acquire);

                // Existing READY slot
                if (state == Bucket::READY) {
                    if (!keyEq_(b.key, key)) break; // collision

                    auto expectState = Bucket::READY;
                    if (!b.state.compare_exchange_strong(expectState, Bucket::ACCESSING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        if (!backoff.next()) {
                            SHMAP_LOG("ShmHashTable[%zd] backoff timeout!", idx);
                            return Status::TIMEOUT;
                        }
                        continue;
                    }

                    SHMAP_LOG("ShmHashTable[%zd] from READY to ACCESSING!", idx);

                    // Maybe save old value
                    VALUE old{};
                    VALUE* old_ptr = nullptr;
                    if constexpr (ROLLBACK_ENABLE) {
                        old = b.value;
                        old_ptr = &old;
                    }

                    Status status = ApplyVisitor(std::forward<Visitor>(visitor), old_ptr, (idx + probe) % CAPACITY, b.value, false);

                    SHMAP_LOG("ShmHashTable[%zd] from ACCESSING to READY!", idx);
                    b.state.store(Bucket::READY, std::memory_order_release);
                    return status;
                }

                // Empty && Create
                if (state == Bucket::EMPTY && mode == AccessMode::CreateIfMiss) {
                    uint32_t expectState = Bucket::EMPTY;
                    if (!b.state.compare_exchange_strong(expectState, Bucket::INSERTING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        if (!backoff.next()) {
                            SHMAP_LOG("ShmHashTable[%zd] backoff timeout!", idx);
                            return Status::TIMEOUT;
                        }
                        continue;
                    }

                    SHMAP_LOG("ShmHashTable[%zd] from EMPTY to INSERTING!", idx);

                    b.value = VALUE{}; // default construct value
                    
                    VALUE old_dummy{};
                    VALUE* old_ptr = ROLLBACK_ENABLE ? &old_dummy : nullptr;
                    Status status = ApplyVisitor(std::forward<Visitor>(visitor), old_ptr, (idx + probe) % CAPACITY, b.value, true);
                    
                    if (!status) {
                        SHMAP_LOG("ShmHashTable[%zd] from INSERTING to EMPTY!", idx);
                        b.state.store(Bucket::EMPTY, std::memory_order_release);
                        return status;
                    }

                    b.key = key;

                    SHMAP_LOG("ShmHashTable[%zd] from INSERTING to READY!", idx);
                    b.state.store(Bucket::READY, std::memory_order_release);
                    return Status::SUCCESS;
                }

                // Empty && Read-only miss
                if (state == Bucket::EMPTY && mode == AccessMode::AccessExist) {
                    return Status::NOT_FOUND;
                }

                // Otherwise waiting
                if (!backoff.next()) {
                    SHMAP_LOG("ShmHashTable[%zd] backoff timeout!", idx);
                    return Status::TIMEOUT;
                }
            }
        }
        return Status::NOT_FOUND;
    }

    template<typename Visitor /* Status (idx, const Key&, Value&) */>
    Status Travel(Visitor&& visit,
        std::chrono::nanoseconds timeout = std::chrono::seconds(5)) noexcept {

        Backoff backoff(timeout);
        for (std::size_t idx = 0; idx < CAPACITY; ++idx) {
            Bucket& b = buckets_[idx];
            while (true) {
                uint32_t curState = b.state.load(std::memory_order_acquire);

                if (curState == Bucket::EMPTY) break;

                if (curState == Bucket::READY) {
                    uint32_t expected = Bucket::READY;
                    if (!b.state.compare_exchange_strong(expected, Bucket::ACCESSING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        if (!backoff.next()) {
                            return Status::TIMEOUT;
                        }
                        continue;
                    }

                    Status status = ApplyVisitor(std::forward<Visitor>(visit), idx, b.key, b.value);
                    b.state.store(Bucket::READY, std::memory_order_release);
                    if (!status) return status;

                    break;
                }
                if (!backoff.next()) {
                    return Status::TIMEOUT;
                }
            }
        }
        return Status::SUCCESS;
    }

private:
    template<typename Visitor, typename ...Args>
    Status ApplyVisitor(Visitor&& visit, Args&&... args) noexcept {
        Status result = Status::SUCCESS;
        try {
            if constexpr (std::is_same_v<std::invoke_result_t<Visitor, Args...>, void>) {
                // void visitor -> always success
                std::forward<Visitor>(visit)(std::forward<Args>(args)...);
            } else {
                result = std::forward<Visitor>(visit)(std::forward<Args>(args)...);
            }
        } catch (...) {
            result = Status::ERROR;
        }
        return result;
    }

    template<typename Visitor>
    Status ApplyVisitor(Visitor&& visit, VALUE* oldValue /* non-null only if rollback enabled */,
        std::size_t idx, VALUE& value, bool isNew) noexcept {

        Status result = ApplyVisitor(std::forward<Visitor>(visit), idx, value, isNew);
        if (ROLLBACK_ENABLE) {
            if (!result && oldValue) {
                // roll back
                value = *oldValue;
            }
        }
        return result;
    }

private:
    alignas(CACHE_LINE_SIZE) Bucket buckets_[CAPACITY];
    HASH  hasher_{};
    EQUAL keyEq_{};
};

} // namespace shmap

#endif // SHMAP_SHM_HASH_TABLE_H_
