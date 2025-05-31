/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_SHM_STORAGE_H_
#define SHMAP_SHM_STORAGE_H_

#include "shmap/shmap.h"

#include <stdexcept>
#include <string>
#include <atomic>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <cerrno>
#else
    #error "POSIX shared memory is required."
#endif

namespace shmap {

/* -------------------------------------------------------------------------- */
/*                         ShmBlock – memory block for table                  */
/* -------------------------------------------------------------------------- */
template<typename TABLE>
struct ShmBlock {
    static_assert(std::is_trivially_copyable<TABLE>::value, "TABLE must be trivially copyable");
    static_assert(std::is_standard_layout<TABLE>::value, "TABLE must have standard layout");

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
            SHMAP_LOG("ShmBlock create and new block!");
            return block;
        }
        else {
            block->WaitReady();
            SHMAP_LOG("ShmBlock create and wait block!");
            return block;
        }
    }

    static ShmBlock* Open(void* mem) noexcept {
        auto* block = static_cast<ShmBlock*>(mem);
        block->WaitReady();
        SHMAP_LOG("ShmBlock open and wait block!");
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
        while (state.load(std::memory_order_acquire) != READY) {
            std::this_thread::yield();
        }
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
            SHMAP_LOG("ShmStorage construct %s!", SHM_PATH::value);
        }
        else if (errno == EEXIST) {
            fd_ = ::shm_open(path, O_RDWR, 0666);
            if (fd_ < 0) {
                int e = errno;
                throw std::runtime_error("shm_open O_RDWR failed: " + std::to_string(e));
            }
            SHMAP_LOG("ShmStorage open %s!", SHM_PATH::value);
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
        SHMAP_LOG("ShmStorage close %s!", SHM_PATH::value);
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