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
    struct ShmPath { static constexpr const char* value = "/shm_storage_test"; };
    using Storage = ShmStorage<ShmTable<FixedString, int, 8>, ShmPath>;
}

struct ShmStorgeTest : public testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
        Storage::GetInstance().Destroy();
    }
};
    
TEST_F(ShmStorgeTest, shm_storage_function_test) {
    auto& storage = Storage::GetInstance();

    FixedString k = FixedString::FromString("cnt");
    int value = 0x123456;

    bool inserted = storage->Visit(k, AccessMode::CreateIfMiss,
        [value](std::size_t id, int& v, bool created) { 
            ASSERT_TRUE(created);
            v = value;
    });

    ASSERT_TRUE(inserted);

    bool found = storage->Visit(k, AccessMode::AccessExist,
        [value](std::size_t id, int& v, bool created) { 
        ASSERT_FALSE(created);
        ASSERT_EQ(value, v);
    });

    ASSERT_TRUE(found);
}
