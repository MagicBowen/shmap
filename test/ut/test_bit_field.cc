#include <gtest/gtest.h>
#include "bit_field.h"

using namespace shmap;

namespace {
    // Test with uint8_t
    enum class ColorComponent {
        Red, Green, Blue
    };

    using RedField   = BitField<ColorComponent, ColorComponent::Red,   0, 3>;
    using GreenField = BitField<ColorComponent, ColorComponent::Green, 3, 3>;
    using BlueField  = BitField<ColorComponent, ColorComponent::Blue,  6, 2>;

    using ColorBitField = BitPackInteger<uint8_t, ColorComponent, RedField, GreenField, BlueField>;
}

TEST(ColorBitField, BasicOperations) {
    ColorBitField color;
    
    // Test default construction
    ASSERT_EQ(color.GetRawValue(), 0);
    
    // Test setting individual fields
    color.Set<ColorComponent::Red>(7);   // 3 bits, max value 7
    ASSERT_EQ(color.Get<ColorComponent::Red>(), 7);
    
    color.Set<ColorComponent::Green>(5); // 3 bits, max value 7
    ASSERT_EQ(color.Get<ColorComponent::Green>(), 5);
    
    color.Set<ColorComponent::Blue>(3);  // 2 bits, max value 3
    ASSERT_EQ(color.Get<ColorComponent::Blue>(), 3);
    
    // Check the combined value
    // Red: 111 (7), Green: 101 (5), Blue: 11 (3)
    // Binary: 11101111 = 0xEF
    ASSERT_EQ(color.GetRawValue(), 0xEF);
}

TEST(ColorBitField, ConstructorAndAssignment) {
    // Test construction from underlying type
    ColorBitField color1(0x2A); // 00101010
    ASSERT_EQ(color1.Get<ColorComponent::Red>(), 2);   // 010
    ASSERT_EQ(color1.Get<ColorComponent::Green>(), 5); // 101
    ASSERT_EQ(color1.Get<ColorComponent::Blue>(), 0);  // 00
    
    // Test copy constructor
    ColorBitField color2(color1);
    ASSERT_EQ(color2, color1);
    
    // Test assignment from underlying type
    ColorBitField color3;
    color3 = 0x15; // 00010101
    ASSERT_EQ(color3.Get<ColorComponent::Red>(), 5);   // 101
    ASSERT_EQ(color3.Get<ColorComponent::Green>(), 2); // 010
    ASSERT_EQ(color3.Get<ColorComponent::Blue>(), 0);  // 00
    
    // Test assignment operator
    ColorBitField color4;
    color4 = color3;
    ASSERT_EQ(color4, color3);
}

TEST(ColorBitField, ImplicitConversion) {
    ColorBitField color;
    color.Set<ColorComponent::Red>(3);
    color.Set<ColorComponent::Green>(4);
    color.Set<ColorComponent::Blue>(2);
    
    // Test implicit conversion to underlying type
    uint8_t value = color;
    ASSERT_EQ(value, (2 << 6) | (4 << 3) | 3);
}

TEST(ColorBitField, ComparisonOperators) {
    ColorBitField color1;
    ColorBitField color2;
    
    // Test equality when both are default constructed
    ASSERT_EQ(color1, color2);
    EXPECT_FALSE(color1 != color2);
    
    // Test after setting values
    color1.Set<ColorComponent::Red>(5);
    EXPECT_NE(color1, color2);
    
    color2.Set<ColorComponent::Red>(5);
    ASSERT_EQ(color1, color2);
    
    // Test comparison with underlying type
    ASSERT_EQ(color1, 5);
    ASSERT_EQ(5, color1);
    EXPECT_NE(color1, 6);
    EXPECT_NE(6, color1);
}

TEST(ColorBitField, FieldOverflow) {
    ColorBitField color;
    
    // Red field is 3 bits, max value is 7
    color.Set<ColorComponent::Red>(15); // Will be masked to 7
    ASSERT_EQ(color.Get<ColorComponent::Red>(), 7);
    
    // Blue field is 2 bits, max value is 3
    color.Set<ColorComponent::Blue>(7); // Will be masked to 3
    ASSERT_EQ(color.Get<ColorComponent::Blue>(), 3);
}

TEST(ColorBitField, Clear) {
    ColorBitField color(0xFF);
    EXPECT_NE(color.GetRawValue(), 0);
    
    color.Clear();
    ASSERT_EQ(color.GetRawValue(), 0);
    ASSERT_EQ(color.Get<ColorComponent::Red>(), 0);
    ASSERT_EQ(color.Get<ColorComponent::Green>(), 0);
    ASSERT_EQ(color.Get<ColorComponent::Blue>(), 0);
}


namespace {
    // Test with uint16_t
    enum class RegisterFlags {
        Carry, Zero, Sign, Overflow, Interrupt
    };

    using CarryField     = BitField<RegisterFlags, RegisterFlags::Carry,     0, 1>;
    using ZeroField      = BitField<RegisterFlags, RegisterFlags::Zero,      1, 1>;
    using SignField      = BitField<RegisterFlags, RegisterFlags::Sign,      2, 1>;
    using OverflowField  = BitField<RegisterFlags, RegisterFlags::Overflow,  3, 1>;
    using InterruptField = BitField<RegisterFlags, RegisterFlags::Interrupt, 8, 4>;

    using StatusRegister = BitPackInteger<uint16_t, RegisterFlags, CarryField, ZeroField, SignField, OverflowField, InterruptField>;
}

TEST(StatusRegister, FlagOperations) {
    StatusRegister status;
    
    // Test individual flag operations
    status.Set<RegisterFlags::Carry>(1);
    ASSERT_EQ(status.Get<RegisterFlags::Carry>(), 1);
    
    status.Set<RegisterFlags::Zero>(1);
    ASSERT_EQ(status.Get<RegisterFlags::Zero>(), 1);
    
    status.Set<RegisterFlags::Sign>(0);
    ASSERT_EQ(status.Get<RegisterFlags::Sign>(), 0);
    
    status.Set<RegisterFlags::Overflow>(1);
    ASSERT_EQ(status.Get<RegisterFlags::Overflow>(), 1);
    
    status.Set<RegisterFlags::Interrupt>(0xA);
    ASSERT_EQ(status.Get<RegisterFlags::Interrupt>(), 0xA);
    
    // Check combined value
    // Bits 0-3: 1011 (carry=1, zero=1, sign=0, overflow=1)
    // Bits 8-11: 1010 (interrupt=0xA)
    ASSERT_EQ(status.GetRawValue(), 0x0A0B);
}


namespace {
    // Test with uint64_t
    enum class MemoryAddress {
        Offset, Segment, Page, Directory
    };

    using OffsetField    = BitField<MemoryAddress, MemoryAddress::Offset,    0,  12>;
    using SegmentField   = BitField<MemoryAddress, MemoryAddress::Segment,   12, 16>;
    using PageField      = BitField<MemoryAddress, MemoryAddress::Page,      32, 16>;
    using DirectoryField = BitField<MemoryAddress, MemoryAddress::Directory, 48, 16>;

    using VirtualAddress = BitPackInteger<uint64_t, MemoryAddress, OffsetField, SegmentField, PageField, DirectoryField>;
}

TEST(VirtualAddress, LargeFieldOperations) {
    VirtualAddress addr;
    
    addr.Set<MemoryAddress::Offset>(0xABC);
    addr.Set<MemoryAddress::Segment>(0x1234);
    addr.Set<MemoryAddress::Page>(0x5678);
    addr.Set<MemoryAddress::Directory>(0x9ABC);
    
    ASSERT_EQ(addr.Get<MemoryAddress::Offset>(), 0xABC);
    ASSERT_EQ(addr.Get<MemoryAddress::Segment>(), 0x1234);
    ASSERT_EQ(addr.Get<MemoryAddress::Page>(), 0x5678);
    ASSERT_EQ(addr.Get<MemoryAddress::Directory>(), 0x9ABC);
    
    uint64_t expected = (uint64_t(0x9ABC) << 48) | (uint64_t(0x5678) << 32) | (0x1234 << 12) | 0xABC;
    ASSERT_EQ(addr.GetRawValue(), expected);
}

// Test edge cases
TEST(BitPackInteger, EdgeCases) {
    // Test with single bit field
    enum class SingleBit { Flag };
    using FlagField = BitField<SingleBit, SingleBit::Flag, 7, 1>;
    using SingleBitField = BitPackInteger<uint8_t, SingleBit, FlagField>;
    
    SingleBitField flag;
    flag.Set<SingleBit::Flag>(1);
    ASSERT_EQ(flag.GetRawValue(), 0x80);
    ASSERT_EQ(flag.Get<SingleBit::Flag>(), 1);
    
    flag.Set<SingleBit::Flag>(0);
    ASSERT_EQ(flag.GetRawValue(), 0x00);
    ASSERT_EQ(flag.Get<SingleBit::Flag>(), 0);
}

namespace {
    // Test with uint32_t
    enum class IdOffset {
        GraphId, RegisterId, NodeId
    };

    using NodeField     = BitField<IdOffset, IdOffset::NodeId,     0, 16>;
    using RegisterField = BitField<IdOffset, IdOffset::RegisterId, 16, 8>;
    using GraphField    = BitField<IdOffset, IdOffset::GraphId,    24, 8>;

    using GraphNodeId = BitPackInteger<uint32_t, IdOffset, NodeField, RegisterField, GraphField>;
}

// Test move semantics
TEST(GraphNodeId, MoveSemantics) {
    GraphNodeId id1;
    id1.Set<IdOffset::GraphId>(5);
    id1.Set<IdOffset::RegisterId>(10);
    id1.Set<IdOffset::NodeId>(1000);
    
    // Test move constructor
    GraphNodeId id2(std::move(id1));
    ASSERT_EQ(id2.Get<IdOffset::GraphId>(), 5);
    ASSERT_EQ(id2.Get<IdOffset::RegisterId>(), 10);
    ASSERT_EQ(id2.Get<IdOffset::NodeId>(), 1000);
    
    // Test move assignment
    GraphNodeId id3;
    id3 = std::move(id2);
    ASSERT_EQ(id3.Get<IdOffset::GraphId>(), 5);
    ASSERT_EQ(id3.Get<IdOffset::RegisterId>(), 10);
    ASSERT_EQ(id3.Get<IdOffset::NodeId>(), 1000);
}

// Test constexpr support
TEST(GraphNodeId, ConstexprSupport) {
    constexpr GraphNodeId id(0x01020304);
    constexpr auto graph_id = id.Get<IdOffset::GraphId>();
    constexpr auto register_id = id.Get<IdOffset::RegisterId>();
    constexpr auto node_id = id.Get<IdOffset::NodeId>();
    
    ASSERT_EQ(graph_id, 1);
    ASSERT_EQ(register_id, 2);
    ASSERT_EQ(node_id, 0x0304);
}

TEST(GraphNodeId, SetAndGetFields) {
    GraphNodeId graphNodeId;
    
    graphNodeId.Set<IdOffset::GraphId>(1);
    ASSERT_EQ(graphNodeId.Get<IdOffset::GraphId>(), 1);
    
    graphNodeId.Set<IdOffset::RegisterId>(3);
    ASSERT_EQ(graphNodeId.Get<IdOffset::RegisterId>(), 3);
    
    graphNodeId.Set<IdOffset::NodeId>(7);
    ASSERT_EQ(graphNodeId.Get<IdOffset::NodeId>(), 7);
    
    ASSERT_EQ(graphNodeId, 0x01030007);
}

TEST(GraphNodeId, ComprehensiveOperations) {
    GraphNodeId id;
    
    // Test setting maximum values for each field
    id.Set<IdOffset::NodeId>(0xFFFF);      // 16 bits, max value
    id.Set<IdOffset::RegisterId>(0xFF);    // 8 bits, max value
    id.Set<IdOffset::GraphId>(0xFF);       // 8 bits, max value
    
    EXPECT_EQ(id.Get<IdOffset::NodeId>(), 0xFFFF);
    EXPECT_EQ(id.Get<IdOffset::RegisterId>(), 0xFF);
    EXPECT_EQ(id.Get<IdOffset::GraphId>(), 0xFF);
    EXPECT_EQ(id.GetRawValue(), 0xFFFFFFFFu);
}

TEST(GraphNodeId, PartialUpdate) {
    GraphNodeId id(0x12345678u);
    
    // Initial values
    EXPECT_EQ(id.Get<IdOffset::NodeId>(), 0x5678);
    EXPECT_EQ(id.Get<IdOffset::RegisterId>(), 0x34);
    EXPECT_EQ(id.Get<IdOffset::GraphId>(), 0x12);
    
    // Update only RegisterId
    id.Set<IdOffset::RegisterId>(0xAB);
    EXPECT_EQ(id.Get<IdOffset::NodeId>(), 0x5678);     // Unchanged
    EXPECT_EQ(id.Get<IdOffset::RegisterId>(), 0xAB);    // Updated
    EXPECT_EQ(id.Get<IdOffset::GraphId>(), 0x12);       // Unchanged
    EXPECT_EQ(id.GetRawValue(), 0x12AB5678u);
}

TEST(GraphNodeId, OverflowBehavior) {
    GraphNodeId id;
    
    // Set values that exceed field width
    id.Set<IdOffset::NodeId>(0x12345);     // Only lower 16 bits should be kept
    id.Set<IdOffset::RegisterId>(0x1234);  // Only lower 8 bits should be kept
    id.Set<IdOffset::GraphId>(0x1234);     // Only lower 8 bits should be kept
    
    EXPECT_EQ(id.Get<IdOffset::NodeId>(), 0x2345);
    EXPECT_EQ(id.Get<IdOffset::RegisterId>(), 0x34);
    EXPECT_EQ(id.Get<IdOffset::GraphId>(), 0x34);
}

TEST(GraphNodeId, BitwiseOperations) {
    GraphNodeId id1;
    id1.Set<IdOffset::GraphId>(0xAA);
    id1.Set<IdOffset::RegisterId>(0x55);
    id1.Set<IdOffset::NodeId>(0xF0F0);
    
    GraphNodeId id2;
    id2.Set<IdOffset::GraphId>(0x55);
    id2.Set<IdOffset::RegisterId>(0xAA);
    id2.Set<IdOffset::NodeId>(0x0F0F);
    
    // Perform bitwise operations on raw values
    uint32_t and_result = id1.GetRawValue() & id2.GetRawValue();
    uint32_t or_result = id1.GetRawValue() | id2.GetRawValue();
    uint32_t xor_result = id1.GetRawValue() ^ id2.GetRawValue();
    
    GraphNodeId id_and(and_result);
    GraphNodeId id_or(or_result);
    GraphNodeId id_xor(xor_result);
    
    EXPECT_EQ(id_and.Get<IdOffset::GraphId>(), 0x00);
    EXPECT_EQ(id_and.Get<IdOffset::RegisterId>(), 0x00);
    EXPECT_EQ(id_and.Get<IdOffset::NodeId>(), 0x0000);
    
    EXPECT_EQ(id_or.Get<IdOffset::GraphId>(), 0xFF);
    EXPECT_EQ(id_or.Get<IdOffset::RegisterId>(), 0xFF);
    EXPECT_EQ(id_or.Get<IdOffset::NodeId>(), 0xFFFF);
    
    EXPECT_EQ(id_xor.Get<IdOffset::GraphId>(), 0xFF);
    EXPECT_EQ(id_xor.Get<IdOffset::RegisterId>(), 0xFF);
    EXPECT_EQ(id_xor.Get<IdOffset::NodeId>(), 0xFFFF);
}

TEST(GraphNodeId, SequentialIds) {
    std::vector<GraphNodeId> ids;
    
    // Create sequential IDs
    for (uint32_t graph = 0; graph < 3; ++graph) {
        for (uint32_t reg = 0; reg < 3; ++reg) {
            for (uint32_t node = 0; node < 3; ++node) {
                GraphNodeId id;
                id.Set<IdOffset::GraphId>(graph);
                id.Set<IdOffset::RegisterId>(reg);
                id.Set<IdOffset::NodeId>(node);
                ids.push_back(id);
            }
        }
    }
    
    // Verify uniqueness
    std::set<uint32_t> unique_values;
    for (const auto& id : ids) {
        unique_values.insert(id.GetRawValue());
    }
    EXPECT_EQ(unique_values.size(), ids.size());
}

TEST(GraphNodeId, CopyAndSwap) {
    GraphNodeId id1;
    id1.Set<IdOffset::GraphId>(10);
    id1.Set<IdOffset::RegisterId>(20);
    id1.Set<IdOffset::NodeId>(30);
    
    GraphNodeId id2;
    id2.Set<IdOffset::GraphId>(40);
    id2.Set<IdOffset::RegisterId>(50);
    id2.Set<IdOffset::NodeId>(60);
    
    // Manual swap using temporary
    GraphNodeId temp = id1;
    id1 = id2;
    id2 = temp;
    
    EXPECT_EQ(id1.Get<IdOffset::GraphId>(), 40);
    EXPECT_EQ(id1.Get<IdOffset::RegisterId>(), 50);
    EXPECT_EQ(id1.Get<IdOffset::NodeId>(), 60);
    
    EXPECT_EQ(id2.Get<IdOffset::GraphId>(), 10);
    EXPECT_EQ(id2.Get<IdOffset::RegisterId>(), 20);
    EXPECT_EQ(id2.Get<IdOffset::NodeId>(), 30);
}

// Test to verify compile-time field validation works correctly
TEST(BitFieldCompileTime, NonOverlappingFields) {
    // This should compile successfully - no overlaps
    enum class GoodEnum {
        First, Second, Third, Fourth
    };
    
    using GoodField1 = BitField<GoodEnum, GoodEnum::First,  0, 8>;
    using GoodField2 = BitField<GoodEnum, GoodEnum::Second, 8, 8>;
    using GoodField3 = BitField<GoodEnum, GoodEnum::Third,  16, 8>;
    using GoodField4 = BitField<GoodEnum, GoodEnum::Fourth, 24, 8>;
    using GoodBitField = BitPackInteger<uint32_t, GoodEnum, GoodField1, GoodField2, GoodField3, GoodField4>;
    
    GoodBitField good;
    good.Set<GoodEnum::First>(0x11);
    good.Set<GoodEnum::Second>(0x22);
    good.Set<GoodEnum::Third>(0x33);
    good.Set<GoodEnum::Fourth>(0x44);
    
    EXPECT_EQ(good.GetRawValue(), 0x44332211u);
}

// Test edge case: single bit fields at type boundaries
TEST(BitFieldEdgeCase, TypeBoundaryFields) {
    enum class Flags {
        FirstBit, LastBit
    };
    
    using FirstBitField = BitField<Flags, Flags::FirstBit, 0, 1>;
    using LastBitField = BitField<Flags, Flags::LastBit, 7, 1>;
    using FlagBitField = BitPackInteger<uint8_t, Flags, FirstBitField, LastBitField>;
    
    FlagBitField flags;
    flags.Set<Flags::FirstBit>(1);
    flags.Set<Flags::LastBit>(1);
    
    EXPECT_EQ(flags.GetRawValue(), 0x81u);  // 10000001
}

// These tests verify that the compile-time checks work correctly
// Uncomment each TEST block one at a time to see the compile error

/*
TEST(CompileError, OverlappingFields1) {
    enum class BadEnum1 {
        Field1, Field2
    };
    
    using BadField1 = BitField<BadEnum1, BadEnum1::Field1, 0, 8>;
    using BadField2 = BitField<BadEnum1, BadEnum1::Field2, 4, 8>;  // Overlaps with Field1
    using BadBitField1 = BitPackInteger<uint16_t, BadEnum1, BadField1, BadField2>;
    
    // This line will trigger the compile error
    BadBitField1 bad_instance; 
}
*/

/*
TEST(CompileError, OverlappingFields2) {
    enum class BadEnum2 {
        Field1, Field2, Field3
    };
    
    using BadField2_1 = BitField<BadEnum2, BadEnum2::Field1, 0, 4>;
    using BadField2_2 = BitField<BadEnum2, BadEnum2::Field2, 3, 4>;  // Starts at bit 3, overlaps with Field1
    using BadField2_3 = BitField<BadEnum2, BadEnum2::Field3, 7, 4>;
    using BadBitField2 = BitPackInteger<uint8_t, BadEnum2, BadField2_1, BadField2_2, BadField2_3>;
    
    BadBitField2 bad_instance;
}
*/

/*
TEST(CompileError, FieldExceedsWidth1) {
    enum class BadEnum3 {
        TooLarge
    };
    
    using BadField3 = BitField<BadEnum3, BadEnum3::TooLarge, 0, 16>;  // 16 bits doesn't fit in uint8_t
    using BadBitField3 = BitPackInteger<uint8_t, BadEnum3, BadField3>;
    
    BadBitField3 bad_instance;
}
*/

/*
TEST(CompileError, FieldExceedsWidth2) {
    enum class BadEnum4 {
        OutOfBounds
    };
    
    using BadField4 = BitField<BadEnum4, BadEnum4::OutOfBounds, 30, 4>;  // Starts at bit 30, ends at 34
    using BadBitField4 = BitPackInteger<uint32_t, BadEnum4, BadField4>;
    
    BadBitField4 bad_instance;
}
*/

/*
TEST(CompileError, ComplexOverlapping) {
    enum class BadEnum5 {
        A, B, C, D
    };
    
    using BadField5_A = BitField<BadEnum5, BadEnum5::A, 0, 10>;
    using BadField5_B = BitField<BadEnum5, BadEnum5::B, 8, 10>;   // Overlaps with A
    using BadField5_C = BitField<BadEnum5, BadEnum5::C, 16, 10>;  // OK
    using BadField5_D = BitField<BadEnum5, BadEnum5::D, 20, 10>;  // Overlaps with C
    using BadBitField5 = BitPackInteger<uint32_t, BadEnum5, BadField5_A, BadField5_B, BadField5_C, BadField5_D>;
    
    BadBitField5 bad_instance;
}
*/