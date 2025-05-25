#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "shmap.h"
#include "fixed_string.h"

using namespace shmap;

using Table = ShmHashTable<int, FixedString, 16>;

TEST(ShmTable_Basic, InsertAndAccess) {
    Table tbl;
    bool inserted = tbl.Visit(42, AccessMode::CreateIfMiss,
        [&](size_t idx, FixedString& val, bool is_new){
            ASSERT_TRUE(is_new);
            val = "hello";
        });
    ASSERT_TRUE(inserted);

    bool found = tbl.Visit(42, AccessMode::AccessExist,
        [&](size_t idx, FixedString& val, bool){
            ASSERT_EQ(val, "hello");
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
            ASSERT_EQ(v, "world");
        });
    ASSERT_TRUE(found2);
}

TEST(ShmTable_Basic, AccessNonExist) {
    Table tbl;
    bool f = tbl.Visit(999, AccessMode::AccessExist,
        [](size_t, FixedString&, bool){});
    ASSERT_FALSE(f);
}

TEST(ShmTable_Travel, EnumerateAll) {
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
