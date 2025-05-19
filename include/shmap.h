/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_HPP_
#define SHMAP_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

/* --------------------  1. 自旋辅助  -------------------- */
inline void cpu_relax() noexcept
{
// #if defined(__x86_64__) || defined(_M_X64)
//     _mm_pause();
// #elif defined(__aarch64__)
//     asm volatile("yield");
// #else
    std::this_thread::yield();
// #endif
}

/* --------------------  2. 桶状态  ----------------------- */
enum : uint32_t { EMPTY = 0, INSERTING = 1, READY = 2, MODIFYING = 3 };

/* --------------------  3. 缓存行尺寸 -------------------- */
#if __cpp_lib_hardware_interference_size >= 201603
constexpr std::size_t kLine =
    std::hardware_destructive_interference_size;
#else
constexpr std::size_t kLine = 64;
#endif

/* --------------------  4. Bucket ----------------------- */
template<typename K, typename V>
struct Bucket
{
    std::atomic<uint32_t> flag { EMPTY };
    K  key {};
    V  value {};

private:
    static constexpr std::size_t body =
        sizeof(std::atomic<uint32_t>) + sizeof(K) + sizeof(V);
    static constexpr std::size_t pad_sz =
        (kLine - (body % kLine)) % kLine;
    char pad_[pad_sz ? pad_sz : 1];
};
static_assert(sizeof(Bucket<int,int>) % kLine == 0,
              "Bucket must be cache-line multiple");

/* ====================================================== */
/*  5. StaticUnifiedHashMap                                */
/* ====================================================== */
template<
    typename   Key,
    typename   Value,
    std::size_t Capacity,
    typename   Hasher   = std::hash<Key>,
    typename   KeyEqual = std::equal_to<Key>
>
class StaticUnifiedHashMap
{
    static_assert(Capacity > 0,      "Capacity must be > 0");
    static_assert(std::is_trivially_copyable<Key>::value,
                  "Key must be trivially copyable");
    static_assert(std::is_trivially_copyable<Value>::value,
                  "Value must be trivially copyable");

    using BucketT = Bucket<Key,Value>;

public:
    /* 供 shm_open+ftruncate 使用的字节总量 */
    static constexpr std::size_t bytes() noexcept
    { return sizeof(StaticUnifiedHashMap); }

    /* -------------------------------------------------- */
    /*  visit()  ―― 统一的 读 / 写 / 插入 接口            */
    /* -------------------------------------------------- */
    template<typename Visitor>
    bool visit(const Key& k, Visitor&& vis,
               bool create_if_missing = false) noexcept
    {
        std::size_t idx = hasher_(k) % Capacity;

        for(std::size_t probe = 0; probe < Capacity;
            ++probe, idx = (idx + 1) % Capacity)
        {
            BucketT& b = buckets_[idx];

            while(true)
            {
                uint32_t f = b.flag.load(std::memory_order_acquire);

                /* ----------- 已有条目 ---------- */
                if(f == READY)
                {
                    if(!key_equal_(b.key, k)) break;     /* hash 冲突 */

                    /* 抢占写权，保证独占 */
                    uint32_t exp = READY;
                    if(!b.flag.compare_exchange_strong(
                            exp, MODIFYING,
                            std::memory_order_acquire,
                            std::memory_order_relaxed))
                        continue;                       /* 被其他写者抢走 */

                    /* 独占区开始 */
                    std::forward<Visitor>(vis)(b.value, false);
                    b.flag.store(READY, std::memory_order_release);
                    return true;
                }

                /* ----------  试图插入 ----------- */
                if(f == EMPTY && create_if_missing)
                {
                    uint32_t exp = EMPTY;
                    if(!b.flag.compare_exchange_strong(
                            exp, INSERTING,
                            std::memory_order_acquire,
                            std::memory_order_relaxed))
                        continue;                       /* 竞争失败 */

                    /* 我是首写者，初始化 key/value */
                    b.key   = k;           /* Key 拷贝 */
                    b.value = Value{};     /* Value 默认 */
                    /* 让外部回调填充 value */
                    std::forward<Visitor>(vis)(b.value, true);

                    b.flag.store(READY, std::memory_order_release);
                    return true;
                }

                /* ---------- 特殊情况 ----------- */
                if(f == EMPTY && !create_if_missing)
                    return false;                      /* not found   */

                /* insert / modify 正在进行 */
                cpu_relax();
            }
        }
        return false;                                  /* 表满或探测失败 */
    }

private:
    StaticUnifiedHashMap() = default;      /* 仅 placement-new */

    alignas(kLine) BucketT buckets_[Capacity];
    Hasher   hasher_{};
    KeyEqual key_equal_{};

    /* 让 SharedBlock 成为友元 */
    template<typename M> friend struct SharedBlock;
};

/* ====================================================== */
/*  SharedBlock : 跨进程一次构造                          */
/* ====================================================== */
template<typename MapT>
struct SharedBlock
{
    std::atomic<uint32_t> state { UNINIT }; // 使用枚举值初始化
    MapT map;

    static constexpr uint32_t UNINIT = 0;
    static constexpr uint32_t BUILDING = 1;
    static constexpr uint32_t READY = 2;

    static constexpr std::size_t bytes() noexcept
    { return sizeof(SharedBlock); }

    static SharedBlock* create(void* mem) noexcept
    {
        SharedBlock* blk = static_cast<SharedBlock*>(mem);
        uint32_t exp = UNINIT;
        if (blk->state.compare_exchange_strong(exp, BUILDING,
                std::memory_order_acquire,
                std::memory_order_relaxed))
        {
            // Placement new构造map成员
            new (&blk->map) MapT();
            blk->state.store(READY, std::memory_order_release);
            return blk;
        }
        else
        {
            blk->wait_ready();
            return blk;
        }
    }

    static SharedBlock* open(void* mem) noexcept
    {
        auto* blk = static_cast<SharedBlock*>(mem);
        blk->wait_ready();
        return blk;
    }

private:
    SharedBlock() = default; // 私有构造函数

    void wait_ready() noexcept
    {
        while (state.load(std::memory_order_acquire) != READY)
            cpu_relax();
    }
};

#endif