## shmap



/* ------------------------------------------------------------------
 *  Strict-UB-free SharedBlock
 * ----------------------------------------------------------------- */
// template<typename MapT>
// struct SharedBlock
// {
//     /* 1) 头 4 字节只是占位；真正的 atomic 在赢家构造时就地放入 */
//     alignas(std::atomic<uint32_t>) unsigned char state_buf[sizeof(std::atomic<uint32_t>)];

//     MapT map;

//     /* 状态值 */
//     static constexpr uint32_t UNINIT   = 0;
//     static constexpr uint32_t BUILDING = 1;
//     static constexpr uint32_t READY    = 2;

//     static constexpr std::size_t bytes() noexcept { return sizeof(SharedBlock); }

//     /* ----------------- 统一入口 ----------------- */
//     static SharedBlock* attach(void* mem) noexcept
//     {
//         /* 1. 把首 4 字节当作普通 uint32_t，用 atomic_ref 做 CAS */
//         auto* word   = static_cast<uint32_t*>(mem);
//         std::atomic_ref<uint32_t> ref(*word);

//         uint32_t exp = UNINIT;
//         if(ref.compare_exchange_strong(exp, BUILDING,
//                                        std::memory_order_acquire,
//                                        std::memory_order_relaxed))
//         {
//             /* ===== 我是赢家 ===== */

//             /* 2. 在原位正式 placement-new 一个 std::atomic<uint32_t>
//              *    初值保持 BUILDING，不能再写 0                     */
//             new (mem) std::atomic<uint32_t>(BUILDING);

//             /* 3. 构造 map（其它字段都是 Trivial，无需再构造） */
//             auto* blk = reinterpret_cast<SharedBlock*>(mem);
//             new (&blk->map) MapT;

//             /* 4. 发布 READY */
//             ref.store(READY, std::memory_order_release);
//             return blk;
//         }

//         /* ===== 失败者：等待 READY ===== */
//         while(ref.load(std::memory_order_acquire) != READY)
//             cpu_relax();

//         return reinterpret_cast<SharedBlock*>(mem);
//     }

//     static SharedBlock* create(void* m) noexcept { return attach(m); }
//     static SharedBlock* open  (void* m) noexcept { return attach(m); }

//     /* ------------ 只允许通过 attach 取得实例 ------------ */
// private:
//     SharedBlock()  = default;
//     ~SharedBlock() = default;
// };

// /* ------------------------------------------------------------------ */
// /*  shared_block.hpp — C++17, UB-free, TSAN-clean                      */
// /* ------------------------------------------------------------------ */
// template<typename MapT>
// struct SharedBlock
// {
//     /* 4-byte 控制字。只通过 GNU/Clang 内建原子操作访问 */
//     alignas(4) uint32_t state_raw{0};        // 0-init = UNINIT
//     MapT               map;                  // 真正的哈希表

//     enum : uint32_t { UNINIT = 0, BUILDING = 1, READY = 2 };

//     static constexpr std::size_t bytes() noexcept { return sizeof(SharedBlock); }

//     /* ------------------ 统一入口 ------------------ */
//     static SharedBlock* attach(void* mem) noexcept
//     {
//         auto* word = static_cast<uint32_t*>(mem);

//         uint32_t exp = UNINIT;
//         if(__atomic_compare_exchange_n(word, &exp, BUILDING,
//                                        /*weak*/false,
//                                        __ATOMIC_ACQUIRE,
//                                        __ATOMIC_RELAXED))
//         {
//             /* ========== 赢家：构造 map ========== */
//             auto* blk = reinterpret_cast<SharedBlock*>(mem);

//             /* state_raw 已经写成 BUILDING，无需再修改 */
//             new (&blk->map) MapT;               // placement-new 整张表

//             __atomic_store_n(word, READY, __ATOMIC_RELEASE);
//             return blk;
//         }

//         /* ========== 失败者：等待 READY ========== */
//         while(__atomic_load_n(word, __ATOMIC_ACQUIRE) != READY)
//             cpu_relax();

//         return reinterpret_cast<SharedBlock*>(mem);
//     }

//     /* 语义糖：保持你原有的接口名 */
//     static SharedBlock* create(void* m) noexcept { return attach(m); }
//     static SharedBlock* open  (void* m) noexcept { return attach(m); }

// private:
//     SharedBlock()  = default;
//     ~SharedBlock() = default;
// };
/* ------------------------------------------------------------------ */
/* ¹ C++17 §3.8/8：对同一存储区域再次 placement-new 构造
 *   “同一类型的对象” 行为未定义已被消除，故安全。                  */
/* ------------------------------------------------------------------ */
