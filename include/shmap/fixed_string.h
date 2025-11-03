/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_FIXED_STRING_H
#define SHMAP_FIXED_STRING_H

#include <string_view>
#include <cstring>
#include <cstdarg>
#include <string>
#include <array>
#include <ostream>
#include <algorithm>
#include <functional>
#include <type_traits>

namespace shmap {

struct FixedString {
    static FixedString FromString(const std::string& src) {
        FixedString fs;
        fs.Store(src);
        return fs;
    }

    static FixedString FromFormat(const char* fmt, ...) {
        FixedString fs;
        std::memset(fs.chars_.data(), 0, FIXED_STRING_LEN_MAX);

        if (!fmt) {
            fs.chars_[0] = '\0';
            return fs;
        }

        va_list args;
        va_start(args, fmt);
        int n = std::vsnprintf(fs.chars_.data(), FIXED_STRING_LEN_MAX, fmt, args);
        va_end(args);

        if (n < 0) {
            // Format failed, clean fs
            fs.chars_[0] = '\0';
        } else if (static_cast<std::size_t>(n) >= FIXED_STRING_LEN_MAX) {
            // The output is truncated. Ensure that the tail is still '\0'
            fs.chars_[FIXED_STRING_LEN_MAX - 1] = '\0';
        }
        return fs;
    }

    FixedString() = default;

    FixedString(const std::string& str) {
        Store(str);
    }

    FixedString(const char* cstr) {
        Store(std::string(cstr));
    }

    std::string ToString() const {
        const char* begin = chars_.data();
        const char* end = static_cast<const char*>(std::memchr(begin, '\0', FIXED_STRING_LEN_MAX));
        if (end) {
            return std::string(begin, end);
        } else {
            // No '\0' found, take the entire buffer
            return std::string(begin, begin + FIXED_STRING_LEN_MAX);
        }
    }

    FixedString& operator= (const std::string& src) {
        Store(src);
        return *this;
    }

    FixedString& operator= (const char* cstr) {
        Store(std::string(cstr));
        return *this;
    }

private:
    void Store(const std::string& src) {
        std::size_t copy_len = std::min(src.size(), FIXED_STRING_LEN_MAX);
        std::memcpy(chars_.data(), src.data(), copy_len);
        if (copy_len < FIXED_STRING_LEN_MAX) {
            std::memset(chars_.data() + copy_len, 0, FIXED_STRING_LEN_MAX - copy_len);
        }
    }

private:
    static constexpr std::size_t FIXED_STRING_LEN_MAX{128};
    std::array<char, FIXED_STRING_LEN_MAX> chars_;

private:
    friend bool operator==(const FixedString& a, const FixedString& b);
    friend bool operator<(const FixedString& a, const FixedString& b);
    friend struct std::hash<FixedString>;
    friend std::ostream& operator<<(std::ostream& os, const FixedString& fs);
};

static_assert(std::is_trivially_copyable<FixedString>::value, "FixedString should be trivially copyable!");
static_assert(std::is_standard_layout<FixedString>::value, "FixedString should be standard layout!");

inline bool operator==(const FixedString& a, const FixedString& b) {
    return std::memcmp(a.chars_.data(), b.chars_.data(), FixedString::FIXED_STRING_LEN_MAX) == 0;
}
inline bool operator!=(const FixedString& a, const FixedString& b) { 
    return !(a == b); 
}
inline bool operator<(const FixedString& a, const FixedString& b) {
    return std::memcmp(a.chars_.data(), b.chars_.data(), FixedString::FIXED_STRING_LEN_MAX) < 0;
}
inline bool operator>(const FixedString& a, const FixedString& b) { 
    return b < a; 
}
inline bool operator<=(const FixedString& a, const FixedString& b) { 
    return !(b < a); 
}
inline bool operator>=(const FixedString& a, const FixedString& b) { 
    return !(a < b); 
}

inline bool operator==(const FixedString& a, const std::string& b) {
    return a.ToString() == b;
}
inline bool operator==(const std::string& a, const FixedString& b) {
    return a == b.ToString();
}
inline bool operator!=(const FixedString& a, const std::string& b) {
    return !(a == b);
}
inline bool operator!=(const std::string& a, const FixedString& b) {
    return !(a == b);
}
inline bool operator<(const FixedString& a, const std::string& b) {
    return a.ToString() < b;
}
inline bool operator<(const std::string& a, const FixedString& b) {
    return a < b.ToString();
}
inline bool operator<=(const FixedString& a, const std::string& b) {
    return a.ToString() <= b;
}
inline bool operator<=(const std::string& a, const FixedString& b) {
    return a <= b.ToString();
}
inline bool operator>(const FixedString& a, const std::string& b) {
    return a.ToString() > b;
}
inline bool operator>(const std::string& a, const FixedString& b) {
    return a > b.ToString();
}
inline bool operator>=(const FixedString& a, const std::string& b) {
    return a.ToString() >= b;
}
inline bool operator>=(const std::string& a, const FixedString& b) {
    return a >= b.ToString();
}

inline std::ostream& operator<<(std::ostream& os, const FixedString& fs) {
    os << fs.ToString();
    return os;
}

}

namespace std {
    template<>
    struct hash<shmap::FixedString> {
        std::size_t operator()(const shmap::FixedString& fs) const noexcept {
            // Hash the full buffer (including trailing zeros) for consistency with operator==
            return std::hash<std::string_view>()(
                std::string_view(fs.chars_.data(), shmap::FixedString::FIXED_STRING_LEN_MAX)
            );
        }
    };

    template<>
    struct equal_to<shmap::FixedString> {
        bool operator()(const shmap::FixedString& lhs, const shmap::FixedString& rhs) const noexcept {
            return lhs == rhs;
        }
    };
}

#endif
