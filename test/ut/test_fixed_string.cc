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
    ASSERT_EQ(fs.ToString(), "hello");

    // Empty string
    FixedString fs2 = FixedString::FromString("");
    ASSERT_EQ(fs2.ToString(), "");

    // Default-constructed should be zeroed → empty
    FixedString fs3;
    ASSERT_EQ(fs3.ToString(), "");
}

TEST(FixedStringBasic, StoreAndPadding) {
    std::string s = "abc";
    FixedString fs = FixedString::FromString(s);
    ASSERT_EQ(fs.ToString(), s);

    // Truncation
    std::string long_s = make_long_string(FIXED_STR_LEN_MAX + 10, 'z');
    FixedString fs_long = FixedString::FromString(long_s);

    // Since no '\0' in first FIXED_STR_LEN_MAX bytes, ToString returns FIXED_STR_LEN_MAX chars
    std::string ts = fs_long.ToString();
    ASSERT_EQ(ts.size(), FIXED_STR_LEN_MAX);
    for (char c : ts) {
        ASSERT_EQ(c, 'z');
    }
}

TEST(FixedStringCompare, FixedStringVsFixedString) {
    FixedString a = FixedString::FromString("abc");
    FixedString b = FixedString::FromString("abc");
    FixedString c = FixedString::FromString("abcd");
    FixedString d = FixedString::FromString("ab");

    ASSERT_TRUE(a == b);
    ASSERT_FALSE(a != b);
    ASSERT_TRUE(a != c);
    ASSERT_TRUE(a < c);
    ASSERT_TRUE(c > a);
    ASSERT_TRUE(d < a);
    ASSERT_TRUE(a >= b);
    ASSERT_TRUE(a <= b);
}

TEST(FixedStringCompare, FixedStringVsStdString) {
    FixedString a = FixedString::FromString("foo");
    std::string s1 = "foo";
    std::string s2 = "bar";

    ASSERT_TRUE(a == s1);
    ASSERT_TRUE(s1 == a);
    ASSERT_FALSE(a != s1);
    ASSERT_TRUE(a != s2);
    ASSERT_TRUE(a > s2);
    ASSERT_TRUE(a >= s1);
    ASSERT_TRUE(a < std::string("zoo"));
    ASSERT_TRUE(std::string("a") < a);
}

TEST(FixedStringStreaming, Ostream) {
    FixedString a = FixedString::FromString("stream test");
    std::ostringstream oss;
    oss << a;
    ASSERT_EQ(oss.str(), "stream test");
}

TEST(FixedStringHash, UnorderedSetAndMap) {
    FixedString a = FixedString::FromString("key1");
    FixedString b = FixedString::FromString("key2");
    FixedString a2 = FixedString::FromString("key1");

    // unordered_set
    std::unordered_set<FixedString> uset;
    uset.insert(a);
    uset.insert(b);
    ASSERT_EQ(uset.size(), 2u);
    ASSERT_TRUE(uset.find(a2) != uset.end());

    // unordered_map
    std::unordered_map<FixedString, int> umap;
    umap[a] = 10;
    umap[b] = 20;
    ASSERT_EQ(umap[a2], 10);
    ASSERT_EQ(umap[FixedString::FromString("key2")], 20);
}

TEST(FixedStringEqualTo, StdEqualToSpecialization) {
    FixedString a = FixedString::FromString("xyz");
    FixedString b = FixedString::FromString("xyz");
    std::equal_to<FixedString> eq;
    ASSERT_TRUE(eq(a, b));
    ASSERT_FALSE(eq(a, FixedString::FromString("xy")));
}

TEST(FixedStringFormat, FromFormatBasic) {
    FixedString f1 = FixedString::FromFormat("Hello %s %d", "World", 123);
    ASSERT_EQ(f1.ToString(), "Hello World 123");

    // Leading zeros / width
    FixedString f2 = FixedString::FromFormat("%04d-%02d", 7, 5);
    ASSERT_EQ(f2.ToString(), "0007-05");
}

TEST(FixedStringFormat, FromFormatTruncation) {
    // create a format string that will expand to > FIXED_STR_LEN_MAX chars
    std::string pat = "%s";
    std::string big = make_long_string(FIXED_STR_LEN_MAX + 50, 'A');
    std::string fmt = pat;
    FixedString f = FixedString::FromFormat(fmt.c_str(), big.c_str());
    std::string out = f.ToString();
    // Should be truncated to FIXED_STR_LEN_MAX characters
    ASSERT_EQ(out.size(), FIXED_STR_LEN_MAX - 1);
    for (char c : out) ASSERT_EQ(c, 'A');
}