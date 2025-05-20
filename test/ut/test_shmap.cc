#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <iostream>

#include "shmap.h"

using namespace shmap;

namespace {
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

    using Map   = ShmTable<Fix32, int, 8, FHash, FEq>;
    using Block = ShmBlock<Map>;
}

struct ShmapTtest : public testing::Test {
protected:
};
    
TEST_F(ShmapTtest, shm_base_test) {
    const char* shm_name = "/shmmap_ut";
    bool creator = true;

    shm_unlink(shm_name);

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    ASSERT_NE(-1, fd);


    constexpr std::size_t kShmSize = Block::GetMemUsage();
    int ftruncate_ret = ftruncate(fd, kShmSize);
    ASSERT_EQ(0, ftruncate_ret);

    void* mem = mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(MAP_FAILED, mem);

    close(fd);

    Block* blk = nullptr;
    if (creator) {
        blk = Block::Create(mem);
    } else {
        blk = Block::Open(mem);
    }
    ASSERT_NE(nullptr, blk);

    Map* table_ = &blk->table_;

    Fix32 k{}; std::strncpy(k.data(),"cnt",32);

    table_->Visit(k,
        [](int& v,bool created){ v += 1+created; }, true);

    table_->Visit(k,
        [](int& v,bool){ std::cout<<"value="<<v<<"\n"; });

    munmap(mem, Block::GetMemUsage());
}
