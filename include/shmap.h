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
#include <string>
#include <new>

#if defined(__unix__) || defined(__APPLE__)
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <cerrno>
#else
    #error "POSIX shared memory is required."
#endif

namespace shmap {

#if __cpp_lib_hardware_interference_size >= 201603
    constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

/* -------------------------------------------------------------------------- */
/*                              Bucket definition                             */
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

enum class AccessMode : uint8_t {
    AccessExist,
    CreateIfMiss,
};

/* -------------------------------------------------------------------------- */
/*                     ShmHashTable  (lock-free closed hashing table)             */
/* -------------------------------------------------------------------------- */
template<typename KEY, typename VALUE, std::size_t CAPACITY,
    typename HASH  = std::hash<KEY>,
    typename EQUAL = std::equal_to<KEY>
>
struct ShmHashTable {
    static_assert(CAPACITY > 0, "CAPACITY must be > 0");
    static_assert(std::is_trivially_copyable<KEY>::value,   "KEY must be trivially copyable");
    static_assert(std::is_trivially_copyable<VALUE>::value, "VALUE must be trivially copyable");
    
    using Bucket = ShmBucket<KEY,VALUE>;
    static_assert(sizeof(Bucket) % CACHE_LINE_SIZE == 0,  "Bucket must be cache-line multiple");

    ShmHashTable() = default; // Only used for placement-new

    template<typename Visitor>
    bool Visit(const KEY& k, AccessMode mode, Visitor&& visit) noexcept {

        std::size_t idx = hasher_(k) % CAPACITY;

        for(std::size_t probe = 0; probe < CAPACITY; ++probe, idx = (idx + 1) % CAPACITY) {

            Bucket& b = buckets_[idx];

            while(true) {
                auto curState = b.state.load(std::memory_order_acquire);

                // Already has item
                if(curState == Bucket::READY) {
                    if(!keyEq_(b.key, k)) {
                        /* hash conflict, jump out to find next*/
                        break;
                    }

                    // Preempt the write permission to ensure exclusivity
                    auto expectedState = Bucket::READY;
                    if(!b.state.compare_exchange_strong(expectedState, Bucket::ACCESSING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                       // Occupied by other writers, retry or yield in next loop
                        continue;
                    }

                    // Begin accessing
                    std::forward<Visitor>(visit)(idx, b.value, false);
                    b.state.store(Bucket::READY, std::memory_order_release);
                    return true;
                }

                // Try inserting
                if(curState == Bucket::EMPTY && mode == AccessMode::CreateIfMiss) {
                    auto expectedState = Bucket::EMPTY;
                    if(!b.state.compare_exchange_strong(expectedState, Bucket::INSERTING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        /* Race failed, retry or yield in next loop */
                        continue;
                    }

                    // Inserting key/value
                    b.key   = k;
                    b.value = VALUE{}; // Default construct before modify
                    std::forward<Visitor>(visit)(idx, b.value, true); // Modify content by visitor

                    b.state.store(Bucket::READY, std::memory_order_release);
                    return true;
                }

                // Only read but find none
                if(curState == Bucket::EMPTY && mode == AccessMode::AccessExist) {
                    return false;
                }

                // Waiting for inserting or modifying to end
                std::this_thread::yield();
            }
        }
        return false; // Probe failed
    }

    template<typename Visitor>
    void Travel(Visitor&& visit) noexcept {
        for(std::size_t idx = 0; idx < CAPACITY; ++idx) {
            Bucket& b = buckets_[idx];
            while(true) {
                auto curState = b.state.load(std::memory_order_acquire);
                if(curState == Bucket::READY) {
                    auto expected = Bucket::READY;
                    if(!b.state.compare_exchange_strong(expected, Bucket::ACCESSING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        continue;
                    }

                    std::forward<Visitor>(visit)(idx, b.key, b.value);
                    b.state.store(Bucket::READY, std::memory_order_release);
                    break;
                } else if(curState == Bucket::EMPTY) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
        }
    }

private:
    alignas(CACHE_LINE_SIZE) Bucket buckets_[CAPACITY];
    HASH  hasher_{};
    EQUAL keyEq_{};
};

/* -------------------------------------------------------------------------- */
/*                         ShmBlock – memory block for table                  */
/* -------------------------------------------------------------------------- */
template<typename TABLE>
struct ShmBlock {
    static constexpr std::size_t GetMemUsage() noexcept { 
        return sizeof(ShmBlock); 
    }

    static ShmBlock* Create(void* mem) noexcept {
        auto* block = static_cast<ShmBlock*>(mem);
        uint32_t expectedState = UNINIT;
        if (block->state.compare_exchange_strong(expectedState, BUILDING,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
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

    TABLE* operator->() noexcept  { return &table_; }
    TABLE& operator* () noexcept  { return  table_; }
    const TABLE* operator->() const noexcept { return &table_; }
    const TABLE& operator* () const noexcept { return  table_; }    

private:
    static constexpr uint32_t UNINIT = 0;
    static constexpr uint32_t BUILDING = 1;
    static constexpr uint32_t READY = 2;
    
private:
    ShmBlock() : state {UNINIT} {};
    
    void WaitReady() noexcept{
        while (state.load(std::memory_order_acquire) != READY)
        std::this_thread::yield();
    }

private:
    std::atomic<uint32_t> state { UNINIT };
    TABLE table_;
};

/* -------------------------------------------------------------------------- */
/*                   ShmStorage – POSIX shared memory singleton               */
/* -------------------------------------------------------------------------- */
template<typename TABLE, typename SHM_PATH /* SHM_PATH::value is shm path str */>
struct ShmStorage {
    using Block = ShmBlock<TABLE>;

    static ShmStorage& GetInstance() {
        static ShmStorage instance;
        return instance;
    }

    ShmStorage(const ShmStorage&)            = delete;
    ShmStorage& operator=(const ShmStorage&) = delete;

    ~ShmStorage() {
        Close();
    }

    void Destroy() {
        Close();
        shm_unlink(SHM_PATH::value);
    }


    TABLE* operator->() noexcept  { return &(**block_); }
    TABLE& operator* () noexcept  { return  **block_; }
    const TABLE* operator->() const noexcept { return &(**block_); }
    const TABLE& operator* () const noexcept { return  **block_; }

private:
    ShmStorage() {
        constexpr const char* path = SHM_PATH::value;

        fd_ = ::shm_open(path, O_RDWR | O_CREAT | O_EXCL, 0666);

        if (fd_ >= 0) {
            owner_ = true;
            if (::ftruncate(fd_, static_cast<off_t>(memBytes_)) != 0) {
                int e = errno;
                ::close(fd_);
                ::shm_unlink(path);
                throw std::runtime_error("ftruncate failed: " + std::to_string(e));
            }
        }
        else if (errno == EEXIST) {
            fd_ = ::shm_open(path, O_RDWR, 0666);
            if (fd_ < 0) {
                int e = errno;
                throw std::runtime_error("shm_open O_RDWR failed: " + std::to_string(e));
            }
        }
        else {
            int e = errno;
            throw std::runtime_error("shm_open failed: " + std::to_string(e));
        }

        void* addr_ = ::mmap(nullptr, memBytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (addr_ == MAP_FAILED) {
            int e = errno;
            ::close(fd_);
            if (owner_) ::shm_unlink(path);
            throw std::runtime_error("mmap failed: " + std::to_string(e));
        }

        if (owner_) {
            block_ = Block::Create(addr_);
        } else {
            block_ = Block::Open(addr_);
        }
    }

    void Close() {
        if (block_) {
            // block_->~ShmBlock<TABLE>();
            block_ = nullptr;
        }
        if (addr_) {
            ::munmap(addr_, memBytes_);
            addr_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }     
    }

private:
    int     fd_{-1};
    void*   addr_{nullptr};
    size_t  memBytes_{Block::GetMemUsage()};
    bool    owner_{false};
    Block*  block_{nullptr};
};

}

#endif