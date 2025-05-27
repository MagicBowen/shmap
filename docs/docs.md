## shmap

下面是 C++ 17 实现的一个支持多进程/多线程的共享内存哈希表。支持多进程和多线程的并行访问；
这个哈希表支持通过 Visit 方法进行插入、修改和读取，但是不支持删除元素。实现上采用 Lock-free 的方式实现，支持混合回退的自旋策略，以及指定超时时间；
Visit 方法通过回调 Visitor 来处理元素的访问，支持返回 void 或者 bool 类型的 Visitor；返回 void 类型按照和返回 true 一样的逻辑进行处理。

```cpp
namespace shmap {

#if __cpp_lib_hardware_interference_size >= 201603
    constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

// ----------------------------------------
// Backoff wait + timeout helper
// ----------------------------------------
struct Backoff {
    Backoff(std::chrono::nanoseconds to)
        : start_(Clock::now()), timeout_(to) {}

    // one backoff step; return false if overall timeout exceeded
    bool next() {
        if (Clock::now() - start_ > timeout_) return false;

        if (spin_ < YIELD_LIMIT) {
            std::this_thread::yield();
        } else {
            int  exp = std::min(spin_ - YIELD_LIMIT, MAX_BACKOFF_EXP);
            auto nanos = std::chrono::nanoseconds(1LL << exp);
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
    typename EQUAL = std::equal_to<KEY>,
    bool ROLLBACK_ENABLE = false
>
struct ShmHashTable {
    static_assert(CAPACITY > 0, "CAPACITY must be > 0");
    static_assert(std::is_trivially_copyable<KEY>::value,   "KEY must be trivially copyable");
    static_assert(std::is_trivially_copyable<VALUE>::value, "VALUE must be trivially copyable");
    
    using Bucket = ShmBucket<KEY,VALUE>;
    static_assert(sizeof(Bucket) % CACHE_LINE_SIZE == 0,  "Bucket must be cache-line multiple");

    ShmHashTable() = default; // Only used for placement-new

    template<typename Visitor>
    bool Visit(const KEY& key, AccessMode mode, Visitor&& visitor,
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
                        if (!backoff.next()) return false;
                        continue;
                    }

                    // Maybe save old value
                    VALUE old{};
                    VALUE* old_ptr = nullptr;
                    if constexpr (ROLLBACK_ENABLE) {
                        old = b.value;
                        old_ptr = &old;
                    }

                    bool ok = ApplyVisitor(std::forward<Visitor>(visitor), (idx + probe) % CAPACITY, b.value, false,old_ptr);

                    b.state.store(Bucket::READY, std::memory_order_release);
                    return ok;
                }

                // Empty && Create
                if (state == Bucket::EMPTY && mode == AccessMode::CreateIfMiss) {
                    uint32_t expectState = Bucket::EMPTY;
                    if (!b.state.compare_exchange_strong(expectState, Bucket::INSERTING,
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        if (!backoff.next()) return false;
                        continue;
                    }

                    b.key   = key;
                    b.value = VALUE{};

                    VALUE old_dummy{};
                    VALUE* old_ptr = ROLLBACK_ENABLE ? &old_dummy : nullptr;
                    bool ok = ApplyVisitor(std::forward<Visitor>(visitor), (idx + probe) % CAPACITY, b.value, true, old_ptr);

                    if (!ok && ROLLBACK_ENABLE) {
                        b.state.store(Bucket::EMPTY, std::memory_order_release);
                        return false;
                    }
                    b.state.store(Bucket::READY, std::memory_order_release);
                    return ok;
                }

                // Empty && Read-only miss
                if (state == Bucket::EMPTY && mode == AccessMode::AccessExist) {
                    return false;
                }

                // Otherwise waiting
                if (!backoff.next()) return false;
            }
        }
        return false; // table full or timeout
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
    template<typename Visitor>
    bool ApplyVisitor(Visitor&& visit, std::size_t idx, VALUE& value, bool isNew,
        VALUE* oldValue /* non-null only if rollback enabled */) noexcept {
        bool result = true;
        try {
            if constexpr (std::is_same_v<std::invoke_result_t<Visitor, std::size_t, VALUE&, bool>, void>) {
                // void visitor -> always success
                std::forward<Visitor>(visit)(idx, value, isNew);
            } else {
                result = std::forward<Visitor>(visit)(idx, value, isNew);
            }
        } catch (...) {
            result = false;
        }
        if (!result && oldValue) {
            // roll back
            value = *oldValue;
        }
        return result;
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

        SHMAP_LOG("ShmStorage construct %s!", SHM_PATH::value);

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
```

我需要你仔细的帮我 review 代码，实现以下功能：
- Travel 也能支持自旋的回退策略，支持超时退出，用户可以使用返回值来判断是否超时退出；
- Travel 的 Visitor 参数可以有返回值，如果返回值为 false，则表示用户主动中断遍历；
- 如果 Travel 的 Visitor 返回值为 true 或者为 void，则表示继续遍历；
- 实现一个接口，用于监控进程进行哈希表检查，通过遍历判断某个元素长期处于修改状态，来判断是否有死锁，通过返回对应的 bucket id 、 key 和 value 值来告诉用户死锁的元素；
- 给我一些如果检测到死锁，监控进程有哪些可行的处理或者恢复手段？
- 此外，如果多个进程竞争打开并初始化相同 ShmStorage，当前的实现有没有问题？有的话如何解决？
- 最后帮我仔细的 review 代码，看看是否有可以优化的地方，或者实现上考虑不周全的地方，尤其是一些并发竞争以及 lock-free 编程考虑不严谨的地方；