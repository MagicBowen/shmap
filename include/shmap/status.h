/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef STATUS_H
#define STATUS_H

#include <cstdint>
#include <ostream>
#include <string>

namespace shmap {

struct Status {
    enum Code : uint32_t {
        SUCCESS = 0,
        ERROR,
        EXCEPTION,
        NOT_FOUND,
        ALREADY_EXISTS,
        TIMEOUT,
        NOT_READY,
        OUT_OF_MEMORY,
        INVALID_ARGUMENT,
        NOT_IMPLEMENTED,
        CRASH,
        UNKNOWN,
    };

    constexpr Status(Code code) noexcept
        : code_{code} {}

    explicit constexpr Status(uint32_t code) noexcept
        : code_{Code::UNKNOWN} {
        if (code < static_cast<uint32_t>(Code::UNKNOWN)) {
            code_ = static_cast<Code>(code);
        }
    }

    constexpr uint32_t GetCode()    const noexcept { return static_cast<uint32_t>(code_); }
    constexpr operator uint32_t()   const noexcept { return static_cast<uint32_t>(code_); }
    constexpr operator bool()       const noexcept { return code_ == Code::SUCCESS; }
    constexpr bool     IsSuccess()  const noexcept { return code_ == Code::SUCCESS; }
    constexpr bool     IsFailed()   const noexcept { return code_ != Code::SUCCESS; }

    constexpr bool operator==(uint32_t c) const noexcept { return static_cast<uint32_t>(code_) == c; }
    constexpr bool operator!=(uint32_t c) const noexcept { return static_cast<uint32_t>(code_) != c; }

    constexpr bool operator==(Code c) const noexcept { return code_ == c; }
    constexpr bool operator!=(Code c) const noexcept { return code_ != c; }

    constexpr bool operator==(const Status& o) const noexcept { return code_ == o.code_; }
    constexpr bool operator!=(const Status& o) const noexcept { return code_ != o.code_; }

    std::string ToString() const {
        switch (code_) {
            case Code::SUCCESS:          return "SUCCESS";
            case Code::ERROR:            return "ERROR";
            case Code::EXCEPTION:        return "EXCEPTION";
            case Code::NOT_FOUND:        return "NOT_FOUND";
            case Code::ALREADY_EXISTS:   return "ALREADY_EXISTS";
            case Code::TIMEOUT:          return "TIMEOUT";
            case Code::NOT_READY:        return "NOT_READY";
            case Code::OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
            case Code::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
            case Code::NOT_IMPLEMENTED:  return "NOT_IMPLEMENTED";
            case Code::CRASH:            return "CRASH";
            default:
                return "UNKNOWN(" + std::to_string(static_cast<uint32_t>(code_)) + ")";
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const Status& s) {
        return os << s.ToString();
    }

private:
    Code code_;
};

}

#endif
