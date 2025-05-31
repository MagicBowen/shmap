#include <gtest/gtest.h>
#include <atomic>
#include <type_traits>

struct Foo {
    // will not be trivial
    // int x {0};

    int a;
    std::atomic<int> b;

    Foo() = default;

    // will not be trivial and trivially copyable
    // Foo& operator=(const Foo& other) {
    //     if (this != &other) {
    //         a = other.a;
    //         b.store(other.b.load());
    //     }
    //     return *this;
    // }

    // will not be standard layout
// private:
//     int c;
};

static_assert(std::is_trivial_v<std::atomic<int>>, "std::atomic<int> should be trivial");
static_assert(std::is_trivially_copyable_v<std::atomic<int>>, "std::atomic<int> should be trivially copyable");
static_assert(std::is_standard_layout_v<std::atomic<int>>, "std::atomic<int> should have standard layout");

// if Foo.a initialized to 0, it is none trivial, but trivially copyable
static_assert(std::is_trivial_v<Foo>, "Foo should be trivial");

// if Foo impl operator=, it is not trivial and is not trivially copyable
static_assert(std::is_trivially_copyable_v<Foo>, "Foo should be trivially copyable");

// Not affected by the presence of initialization or operator=, but class should not have private members
static_assert(std::is_standard_layout_v<Foo>, "Foo should have standard layout");


// TEST(TrivialTest, assign_struct_contains_atomicInt) {
//     Foo foo1;
//     foo1.a = 42;
//     foo1.b.store(100);

//     Foo foo2;
//     foo2 = foo1;

//     EXPECT_EQ(foo2.a, 42);
//     EXPECT_EQ(foo2.b.load(), 100);
//     EXPECT_TRUE(std::is_trivially_copyable_v<Foo>);
// }