#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <iostream>

#include "shmap.h"

using Fix32 = std::array<char,32>;

struct FHash {
    std::size_t operator()(const Fix32& s) const noexcept {
        std::size_t h=14695981039346656037ull;
        for(char c:s){ if(c==0) break;
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        } return h;
    }
};
struct FEq {
    bool operator()(const Fix32& a,const Fix32& b)const noexcept {
        return std::memcmp(a.data(),b.data(),32)==0;
    }
};

using Map   = StaticUnifiedHashMap<Fix32, int, 8, FHash, FEq>;
using Block = SharedBlock<Map>;



struct ShmapTtest : public testing::Test {
protected:
};
    
TEST_F(ShmapTtest, shm_base_test) {
   const char* shm_name = "/u_map";  // 共享内存名称需以 / 开头（POSIX 规范）
    bool creator = true;

    shm_unlink(shm_name);

    // 1. 打开/创建共享内存
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    ASSERT_NE(-1, fd);


    // 2. 调整共享内存大小
    constexpr std::size_t kShmSize = Block::bytes();
    int ftruncate_ret = ftruncate(fd, kShmSize);
    ASSERT_EQ(0, ftruncate_ret);

    // 3. 映射共享内存到进程地址空间
    void* mem = mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(MAP_FAILED, mem);

    close(fd);  // 关闭文件描述符（mmap 仍可访问内存）

    // 4. 创建或打开 SharedBlock
    Block* blk = nullptr;
    if (creator) {
        blk = Block::create(mem);
    } else {
        blk = Block::open(mem);
    }
    ASSERT_NE(nullptr, blk);

    Map* map = &blk->map;

    Fix32 k{}; std::strncpy(k.data(),"cnt",32);

    /* 累加计数 */
    map->visit(k,
        [](int& v,bool created){ v += 1+created; }, true);

    /* 只读打印 */
    map->visit(k,
        [](int& v,bool){ std::cout<<"value="<<v<<"\n"; });

    munmap(mem, Block::bytes());
}
