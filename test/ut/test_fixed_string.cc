#include <gtest/gtest.h>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <functional>

#include "fixed_string.h"

using namespace shmap;

namespace {
    std::string make_long_string(std::size_t n, char c = 'x') {
        return std::string(n, c);
    }

    constexpr std::size_t FIXED_STR_LEN_MAX = 128;
}

TEST(FixedStringBasic, FromAndToString) {
    FixedString fs = FixedString::FromString("hello");
    EXPECT_EQ(fs.ToString(), "hello");

    // Empty string
    FixedString fs2 = FixedString::FromString("");
    EXPECT_EQ(fs2.ToString(), "");

    // Default-constructed should be zeroed → empty
    FixedString fs3;
    EXPECT_EQ(fs3.ToString(), "");
}

TEST(FixedStringBasic, StoreAndPadding) {
    std::string s = "abc";
    FixedString fs = FixedString::FromString(s);
    EXPECT_EQ(fs.ToString(), s);

    // Truncation
    std::string long_s = make_long_string(FIXED_STR_LEN_MAX + 10, 'z');
    FixedString fs_long = FixedString::FromString(long_s);

    // Since no '\0' in first FIXED_STR_LEN_MAX bytes, ToString returns FIXED_STR_LEN_MAX chars
    std::string ts = fs_long.ToString();
    EXPECT_EQ(ts.size(), FIXED_STR_LEN_MAX);
    for (char c : ts) {
        EXPECT_EQ(c, 'z');
    }
}

TEST(FixedStringCompare, FixedStringVsFixedString) {
    FixedString a = FixedString::FromString("abc");
    FixedString b = FixedString::FromString("abc");
    FixedString c = FixedString::FromString("abcd");
    FixedString d = FixedString::FromString("ab");

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a < c);
    EXPECT_TRUE(c > a);
    EXPECT_TRUE(d < a);
    EXPECT_TRUE(a >= b);
    EXPECT_TRUE(a <= b);
}

TEST(FixedStringCompare, FixedStringVsStdString) {
    FixedString a = FixedString::FromString("foo");
    std::string s1 = "foo";
    std::string s2 = "bar";

    EXPECT_TRUE(a == s1);
    EXPECT_TRUE(s1 == a);
    EXPECT_FALSE(a != s1);
    EXPECT_TRUE(a != s2);
    EXPECT_TRUE(a > s2);
    EXPECT_TRUE(a >= s1);
    EXPECT_TRUE(a < std::string("zoo"));
    EXPECT_TRUE(std::string("a") < a);
}

TEST(FixedStringStreaming, Ostream) {
    FixedString a = FixedString::FromString("stream test");
    std::ostringstream oss;
    oss << a;
    EXPECT_EQ(oss.str(), "stream test");
}

TEST(FixedStringHash, UnorderedSetAndMap) {
    FixedString a = FixedString::FromString("key1");
    FixedString b = FixedString::FromString("key2");
    FixedString a2 = FixedString::FromString("key1");

    // unordered_set
    std::unordered_set<FixedString> uset;
    uset.insert(a);
    uset.insert(b);
    EXPECT_EQ(uset.size(), 2u);
    EXPECT_TRUE(uset.find(a2) != uset.end());

    // unordered_map
    std::unordered_map<FixedString, int> umap;
    umap[a] = 10;
    umap[b] = 20;
    EXPECT_EQ(umap[a2], 10);
    EXPECT_EQ(umap[FixedString::FromString("key2")], 20);
}

TEST(FixedStringEqualTo, StdEqualToSpecialization) {
    FixedString a = FixedString::FromString("xyz");
    FixedString b = FixedString::FromString("xyz");
    std::equal_to<FixedString> eq;
    EXPECT_TRUE(eq(a, b));
    EXPECT_FALSE(eq(a, FixedString::FromString("xy")));
}

TEST(FixedStringFormat, FromFormatBasic) {
    FixedString f1 = FixedString::FromFormat("Hello %s %d", "World", 123);
    EXPECT_EQ(f1.ToString(), "Hello World 123");

    // Leading zeros / width
    FixedString f2 = FixedString::FromFormat("%04d-%02d", 7, 5);
    EXPECT_EQ(f2.ToString(), "0007-05");
}

TEST(FixedStringFormat, FromFormatTruncation) {
    // create a format string that will expand to > FIXED_STR_LEN_MAX chars
    std::string pat = "%s";
    std::string big = make_long_string(FIXED_STR_LEN_MAX + 50, 'A');
    std::string fmt = pat;
    FixedString f = FixedString::FromFormat(fmt.c_str(), big.c_str());
    std::string out = f.ToString();
    // Should be truncated to FIXED_STR_LEN_MAX characters
    EXPECT_EQ(out.size(), FIXED_STR_LEN_MAX - 1);
    for (char c : out) EXPECT_EQ(c, 'A');
}