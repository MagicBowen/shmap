/**
* Copyright (c) wangbo@joycode.art 2024
*/

#ifndef SHM_LOG_H
#define SHM_LOG_H

#include <cstdio>

namespace shmap {

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
