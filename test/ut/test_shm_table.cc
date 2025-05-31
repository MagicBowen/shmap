#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "shmap/fixed_string.h"
#include "shmap/shm_hash_table.h"

using namespace shmap;

using Table = ShmHashTable<int, FixedString, 16>;

TEST(ShmHashTableTest, InsertAndAccess) {
    Table tbl;
    bool inserted = tbl.Visit(42, AccessMode::CreateIfMiss,
        [&](size_t idx, FixedString& val, bool is_new){
            ASSERT_TRUE(is_new);
            val = "hello";
        });
    ASSERT_TRUE(inserted);

    bool found = tbl.Visit(42, AccessMode::AccessExist,
        [&](size_t idx, FixedString& val, bool){
            ASSERT_EQ(val, FixedString("hello"));
        });
    ASSERT_TRUE(found);

    bool updated = tbl.Visit(42, AccessMode::CreateIfMiss,
        [&](size_t, FixedString& v, bool is_new){
            ASSERT_FALSE(is_new);
            v = "world";
        });
    ASSERT_TRUE(updated);

    bool found2 = tbl.Visit(42, AccessMode::AccessExist,
        [&](size_t, FixedString& v, bool){
            ASSERT_EQ(v, FixedString("world"));
        });
    ASSERT_TRUE(found2);
}

TEST(ShmHashTableTest, AccessNonExist) {
    Table tbl;
    bool f = tbl.Visit(999, AccessMode::AccessExist,
        [](size_t, FixedString&, bool){});
    ASSERT_FALSE(f);
}

TEST(ShmHashTableTest, EnumerateAll) {
    Table tbl;
    for (int i = 0; i < 5; ++i) {
        tbl.Visit(i, AccessMode::CreateIfMiss,
            [&](size_t, FixedString& v, bool){
                v = std::to_string(i);
            });
    }
    std::vector<int> seen;
    tbl.Travel([&](size_t, const int& k, const FixedString& v){
        seen.push_back(k);
    });
    std::sort(seen.begin(), seen.end());
    ASSERT_EQ(seen, std::vector<int>({0,1,2,3,4}));
}

TEST(ShmHashTableTest, VisitBucket_AllStatesAccessible) {
    constexpr size_t CAPACITY = 8;
    using Table = ShmHashTable<int, int, CAPACITY>;
    Table* table = new (aligned_alloc(CACHE_LINE_SIZE, sizeof(Table))) Table();
    
    for (size_t i = 0; i < 8; ++i) {
        std::size_t bucketId;
        auto status = table->Visit(i, AccessMode::CreateIfMiss, [&](auto idx, auto& value, bool isNew) {
            bucketId = idx;
            value = i * 10;
        });

        EXPECT_TRUE(status);
        
        int result = 0;
        status = table->VisitBucket(bucketId, [&](auto& bucket) {
            result = bucket.value;
        });
        
        EXPECT_EQ(status, Status::SUCCESS);
        EXPECT_EQ(result, i * 10);
    }

    auto status = table->VisitBucket(9, [&](auto& bucket) {
        bucket.value = 42; // This should fail, as bucketId 9 is out of range
    });
    // Expect error since bucketId 9 does not exist
    EXPECT_EQ(status, Status::INVALID_ARGUMENT); 
}

TEST(ShmHashTableTest, VisitBucket_FullRollback) {
    constexpr size_t CAPACITY = 8;
    using Table = ShmHashTable<int, int, CAPACITY, std::hash<int>, std::equal_to<int>, true>;
    Table* table = new (aligned_alloc(CACHE_LINE_SIZE, sizeof(Table))) Table();
    
    std::size_t bucketId;
    table->Visit(42, AccessMode::CreateIfMiss, [&](auto idx, auto& value, bool isNew) {
        bucketId = idx;
        value = 100;
    });
    
    auto status = table->VisitBucket(bucketId, [](auto& bucket) {
        bucket.state = Table::Bucket::INSERTING;
        bucket.key = 99;
        bucket.value = 200;
        return Status::ERROR;
    });

    EXPECT_EQ(status, Status::ERROR);

    status = table->VisitBucket(bucketId, [](auto& bucket) {
        EXPECT_EQ(bucket.state.load(), Table::Bucket::READY);
        EXPECT_EQ(bucket.key, 42);
        EXPECT_EQ(bucket.value, 100);
    });
}

TEST(ShmHashTableTest, TravelBucketConst_ReadOnly) {
    constexpr size_t CAPACITY = 8;
    using Table = ShmHashTable<int, int, CAPACITY>;
    Table* table = new (aligned_alloc(CACHE_LINE_SIZE, sizeof(Table))) Table();
    
    for (size_t i = 0; i < CAPACITY; i++) {
        std::size_t bucketId;
        auto status = table->Visit(i, AccessMode::CreateIfMiss, [&](auto idx, auto& value, bool isNew) {
            bucketId = idx;
            value = i * 10;
        });

        EXPECT_EQ(status, Status::SUCCESS);
        EXPECT_TRUE(bucketId < CAPACITY);
    }
    
    int sum = 0;
    auto status = ((const Table*)table)->TravelBucket([&](size_t, const auto& b) {
        sum += b.value;
    });
    
    EXPECT_EQ(status, Status::SUCCESS);
    EXPECT_EQ(sum, (0 + 7) * 8 / 2 * 10);
}
