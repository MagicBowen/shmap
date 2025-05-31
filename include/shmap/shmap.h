/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHMAP_SHMAP_H
#define SHMAP_SHMAP_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

namespace shmap {

#if __cpp_lib_hardware_interference_size >= 201603
    constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

#ifndef SHMAP_DEBUG_ENABLE
#define SHMAP_DEBUG_ENABLE 0
#endif

#if SHMAP_DEBUG_ENABLE
    #define SHMAP_LOG(FMT, ...) printf("[%s:%d:%d]" FMT "\n", __FILE__, __LINE__, getpid(), ##__VA_ARGS__)
#else
    #define SHMAP_LOG(FMT, ...)
#endif

}

#endif
