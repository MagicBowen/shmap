#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <iostream>

#include "shmap.h"
#include "fixed_string.h"

using namespace shmap;

namespace {
    using Map   = ShmTable<FixedString, int, 8>;
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

    FixedString k = FixedString::FromString("cnt");

    table_->Visit(k,
        [](int& v,bool created){ v += 1+created; }, true);

    table_->Visit(k,
        [](int& v,bool){ std::cout<<"value="<<v<<"\n"; });

    munmap(mem, Block::GetMemUsage());
}
