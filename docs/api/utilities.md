# Utility Components API Reference

## Overview

shmap provides several utility components that support the core data structures and enable safe, efficient shared memory operations.

## FixedString

Fixed-size string type designed for shared memory environments.

### Class Declaration

```cpp
struct FixedString;
```

### Static Methods

#### Construction

```cpp
static FixedString FromString(const std::string& src);
static FixedString FromFormat(const char* fmt, ...);
```

**Example:**
```cpp
auto fs1 = FixedString::FromString("Hello World");
auto fs2 = FixedString::FromFormat("Value: %d", 42);
```

### Instance Methods

#### Construction and Assignment

```cpp
FixedString();
FixedString(const std::string& str);
FixedString(const char* cstr);
FixedString& operator=(const std::string& src);
FixedString& operator=(const char* cstr);
```

#### Conversion

```cpp
std::string ToString() const;
```

**Example:**
```cpp
FixedString fs("test");
std::string str = fs.ToString();
```

### Comparison Operators

All standard comparison operators are supported:
- `==`, `!=`, `<`, `>`, `<=`, `>=`
- Comparisons with `std::string` and `const char*`

**Example:**
```cpp
FixedString a("apple"), b("banana");
if (a < b) { /* true */ }
if (a == "apple") { /* true */ }
```

### Hash Support

Specialized for `std::hash` and `std::equal_to`:

```cpp
std::hash<FixedString> hasher;
std::size_t hash = hasher(fs);
```

### Memory Characteristics

- **Fixed size**: 128 bytes total
- **Null-terminated**: Always maintains null termination
- **Trivially copyable**: Safe for shared memory
- **Standard layout**: Consistent memory layout

## BitsInteger

Bit field manipulation utility for compact data storage.

### Class Declaration

```cpp
template<typename UnderlyingType, typename... Fields>
struct BitsInteger;
```

### BitField Definition

Define bit fields using the `BitField` template:

```cpp
template<auto EnumValue, std::size_t StartBit, std::size_t BitCount>
struct BitField;
```

**Example:**
```cpp
enum class MyFields { A, B, C };

using MyBits = BitsInteger<uint32_t,
    BitField<MyFields::A, 0, 8>,   // 8 bits at position 0
    BitField<MyFields::B, 8, 16>,  // 16 bits at position 8
    BitField<MyFields::C, 24, 8>   // 8 bits at position 24
>;
```

### Public Methods

#### Construction

```cpp
BitsInteger();
BitsInteger(UnderlyingType value);
static BitsInteger INVALID() noexcept;
```

#### Field Operations

```cpp
template<auto E>
UnderlyingType Get() const;           // Get field value

template<auto E>
BitsInteger& Set(UnderlyingType value); // Set field value
```

**Example:**
```cpp
MyBits bits;
bits.Set<MyFields::A>(42);
bits.Set<MyFields::B>(1000);

uint32_t a = bits.Get<MyFields::A>(); // 42
uint32_t b = bits.Get<MyFields::B>(); // 1000
```

#### Value Operations

```cpp
UnderlyingType GetValue() const noexcept;
void SetValue(UnderlyingType value) noexcept;
void Clear() noexcept;
```

#### Validation

```cpp
bool IsValid() const noexcept;
operator bool() const noexcept;

template<typename T>
static bool Verify(const T& value) noexcept;
```

### Comparison Operators

All standard comparison operators are supported for both `BitsInteger` and integral types.

## Backoff

Exponential backoff algorithm for contention management.

### Class Declaration

```cpp
struct Backoff;
```

### Constructor

```cpp
Backoff(std::chrono::nanoseconds timeout);
```

### Public Methods

```cpp
bool next();  // Perform one backoff step
```

**Example:**
```cpp
Backoff backoff(std::chrono::seconds(5));

while (!try_operation()) {
    if (!backoff.next()) {
        // Timeout reached
        break;
    }
}
```

### Backoff Strategy

1. **First 10 attempts**: Use `std::this_thread::yield()`
2. **Subsequent attempts**: Exponential sleep from 1ns to ~1ms
3. **Timeout**: Returns `false` when overall timeout exceeded

## Status

Comprehensive error handling with status codes.

### Class Declaration

```cpp
struct Status;
```

### Status Codes

| Code | Value | Description |
|------|-------|-------------|
| `SUCCESS` | 0 | Operation completed successfully |
| `ERROR` | 1 | General error |
| `EXCEPTION` | 2 | Exception occurred |
| `NOT_FOUND` | 3 | Element not found |
| `ALREADY_EXISTS` | 4 | Element already exists |
| `TIMEOUT` | 5 | Operation timeout |
| `NOT_READY` | 6 | Resource not ready |
| `OUT_OF_MEMORY` | 7 | Memory allocation failed |
| `INVALID_ARGUMENT` | 8 | Invalid parameter |
| `NOT_IMPLEMENTED` | 9 | Feature not implemented |
| `CRASH` | 10 | System crash |
| `UNKNOWN` | 11 | Unknown error |

### Public Methods

#### Construction and Conversion

```cpp
constexpr Status(Code code) noexcept;
constexpr operator bool() const noexcept;     // True if SUCCESS
constexpr bool IsSuccess() const noexcept;
constexpr bool IsFailed() const noexcept;
```

#### String Conversion

```cpp
std::string ToString() const;
friend std::ostream& operator<<(std::ostream& os, const Status& s);
```

**Example:**
```cpp
Status result = SomeOperation();
if (!result) {
    std::cout << "Error: " << result << std::endl;
    // Output: "Error: TIMEOUT"
}
```

### Comparison Operators

All standard comparison operators are supported for both `Status` codes and integral values.

## Memory Requirements

All utility components satisfy shared memory requirements:

- **Trivially copyable**: Safe for `memcpy` operations
- **Standard layout**: Consistent memory layout across processes
- **Fixed size**: Predictable memory usage
- **No dynamic allocation**: Suitable for shared memory

## Integration Examples

### Using FixedString with ShmHashTable

```cpp
ShmHashTable<FixedString, int, 1024> table;

table.Visit(FixedString("key1"), AccessMode::CreateIfMiss,
    [](size_t idx, int& value, bool isNew) {
        if (isNew) value = 100;
        return Status::SUCCESS;
    });
```

### Using BitsInteger for Compact Storage

```cpp
enum class UserFields { ID, Age, Flags };

using UserBits = BitsInteger<uint64_t,
    BitField<UserFields::ID, 0, 32>,
    BitField<UserFields::Age, 32, 8>,
    BitField<UserFields::Flags, 40, 24>
>;

ShmHashTable<FixedString, UserBits, 1024> user_table;
```

### Error Handling with Status

```cpp
Status ProcessData() {
    Backoff backoff(std::chrono::seconds(2));

    while (true) {
        Status result = TryOperation();
        if (result) return result;

        if (result == Status::TIMEOUT || !backoff.next()) {
            return Status::TIMEOUT;
        }
    }
}
```

These utility components provide essential functionality for building robust shared memory applications with shmap.