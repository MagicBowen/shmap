#ifndef BIT_FIELD_H
#define BIT_FIELD_H

#include <type_traits>
#include <cstdint>
#include <limits>

namespace shmap {

namespace detail {
    // Helper to check if two fields overlap
    template<typename Field1, typename Field2>
    struct FieldsOverlap {
        static constexpr bool value = 
            (Field1::start_bit < Field2::end_bit) && (Field2::start_bit < Field1::end_bit);
    };

    // Recursive helper to check one field against all others
    template<typename Field, typename... OtherFields>
    struct CheckOneFieldAgainstOthers;

    template<typename Field>
    struct CheckOneFieldAgainstOthers<Field> {
        static constexpr bool value = false;
    };

    template<typename Field, typename First, typename... Rest>
    struct CheckOneFieldAgainstOthers<Field, First, Rest...> {
        static constexpr bool value = 
            FieldsOverlap<Field, First>::value || 
            CheckOneFieldAgainstOthers<Field, Rest...>::value;
    };

    // Helper to check all fields for overlaps
    template<typename... Fields>
    struct CheckFieldOverlaps;

    template<>
    struct CheckFieldOverlaps<> {
        static constexpr bool value = false;
    };

    template<typename Field>
    struct CheckFieldOverlaps<Field> {
        static constexpr bool value = false;
    };

    template<typename Field, typename... Rest>
    struct CheckFieldOverlaps<Field, Rest...> {
        static constexpr bool value = 
            CheckOneFieldAgainstOthers<Field, Rest...>::value || 
            CheckFieldOverlaps<Rest...>::value;
    };

    // Helper to check if a field exceeds type width
    template<typename UnderlyingType, typename Field>
    struct FieldExceedsTypeWidth {
        static constexpr bool value = 
            Field::end_bit > std::numeric_limits<UnderlyingType>::digits;
    };

    // Helper to check if any field exceeds type width
    template<typename UnderlyingType, typename... Fields>
    struct AnyFieldExceedsTypeWidth;

    template<typename UnderlyingType>
    struct AnyFieldExceedsTypeWidth<UnderlyingType> {
        static constexpr bool value = false;
    };

    template<typename UnderlyingType, typename Field, typename... Rest>
    struct AnyFieldExceedsTypeWidth<UnderlyingType, Field, Rest...> {
        static constexpr bool value = 
            FieldExceedsTypeWidth<UnderlyingType, Field>::value ||
            AnyFieldExceedsTypeWidth<UnderlyingType, Rest...>::value;
    };

    // Helper to find a field by enum value
    template<typename EnumType, EnumType Value, typename... Fields>
    struct FindField;

    template<typename EnumType, EnumType Value>
    struct FindField<EnumType, Value> {
        using type = void;
    };

    template<typename EnumType, EnumType Value, typename Field, typename... Rest>
    struct FindField<EnumType, Value, Field, Rest...> {
        using type = std::conditional_t<
            Field::enum_value == Value,
            Field,
            typename FindField<EnumType, Value, Rest...>::type
        >;
    };
} // namespace detail

// Field descriptor to define a bit field range
template<typename EnumType, EnumType EnumValue, std::size_t StartBit, std::size_t BitCount>
struct BitField {
    static constexpr EnumType enum_value = EnumValue;
    static constexpr std::size_t start_bit = StartBit;
    static constexpr std::size_t bit_count = BitCount;
    static constexpr std::size_t end_bit = StartBit + BitCount;
    
    template<typename T>
    static constexpr T ExtractValue(T full_value) {
        return (full_value & CreateMask<T>()) >> start_bit;
    }
    
    template<typename T>
    static constexpr T InsertValue(T full_value, T field_value) {
        T mask = CreateMask<T>();
        T shifted_value = (field_value << start_bit) & mask;
        return (full_value & ~mask) | shifted_value;
    }
private:
    template<typename T>
    static constexpr T CreateMask() {
        static_assert(std::is_unsigned_v<T>, "Underlying type must be unsigned");
        
        if constexpr (bit_count == 0) {
            return 0;
        } else if constexpr (bit_count >= std::numeric_limits<T>::digits) {
            return std::numeric_limits<T>::max();
        } else {
            return ((static_cast<T>(1) << bit_count) - 1) << start_bit;
        }
    }    
};

// Main BitsInteger class
template<typename UnderlyingType, typename EnumType, typename... Fields>
class BitsInteger {
    static_assert(std::is_unsigned_v<UnderlyingType>, "Underlying type must be unsigned");
    static_assert(std::is_enum_v<EnumType>, "EnumType must be an enum");
    
    // Check for overlapping fields
    static constexpr bool has_overlapping_fields = detail::CheckFieldOverlaps<Fields...>::value;
    static_assert(!has_overlapping_fields, "Field ranges must not overlap");
    
    // Check that no field exceeds type width
    static constexpr bool any_field_exceeds_width = detail::AnyFieldExceedsTypeWidth<UnderlyingType, Fields...>::value;
    static_assert(!any_field_exceeds_width, "All fields must fit within the underlying type");
    
public:
    using underlying_type = UnderlyingType;
    using enum_type = EnumType;
    
    constexpr BitsInteger() noexcept 
    : value_(0) {}
    
    explicit constexpr BitsInteger(UnderlyingType value) noexcept 
    : value_(value) {}
    
    constexpr BitsInteger(const BitsInteger&) noexcept = default;
    constexpr BitsInteger(BitsInteger&&) noexcept = default;

    BitsInteger& operator=(const BitsInteger&) noexcept = default;
    BitsInteger& operator=(BitsInteger&&) noexcept = default;
    
    BitsInteger& operator=(UnderlyingType value) noexcept {
        value_ = value;
        return *this;
    }

    constexpr operator UnderlyingType() const noexcept {
        return value_;
    }
    
    template<EnumType E>
    void Set(UnderlyingType value) {
        using Field = typename detail::FindField<EnumType, E, Fields...>::type;
        static_assert(!std::is_void_v<Field>, "Invalid enum value for this BitsInteger");
        
        value_ = Field::template InsertValue<UnderlyingType>(value_, value);
    }
    
    template<EnumType E>
    constexpr UnderlyingType Get() const {
        using Field = typename detail::FindField<EnumType, E, Fields...>::type;
        static_assert(!std::is_void_v<Field>, "Invalid enum value for this BitsInteger");
        
        return Field::template ExtractValue<UnderlyingType>(value_);
    }

    constexpr UnderlyingType GetRawValue() const noexcept {
        return value_;
    }
    
    void SetRawValue(UnderlyingType value) noexcept {
        value_ = value;
    }

    void Clear() noexcept {
        value_ = 0;
    }        
    
    friend constexpr bool operator==(const BitsInteger& lhs, const BitsInteger& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }
    
    friend constexpr bool operator!=(const BitsInteger& lhs, const BitsInteger& rhs) noexcept {
        return lhs.value_ != rhs.value_;
    }
    
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    friend constexpr bool operator==(const BitsInteger& lhs, T rhs) noexcept {
        return lhs.value_ == static_cast<UnderlyingType>(rhs);
    }
    
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    friend constexpr bool operator!=(const BitsInteger& lhs, T rhs) noexcept {
        return lhs.value_ != static_cast<UnderlyingType>(rhs);
    }
    
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    friend constexpr bool operator==(T lhs, const BitsInteger& rhs) noexcept {
        return static_cast<UnderlyingType>(lhs) == rhs.value_;
    }
    
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    friend constexpr bool operator!=(T lhs, const BitsInteger& rhs) noexcept {
        return static_cast<UnderlyingType>(lhs) != rhs.value_;
    }
    
private:
    UnderlyingType value_;
};

}

#endif // BIT_FIELD_H