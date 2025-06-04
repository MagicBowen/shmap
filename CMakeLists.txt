cmake_minimum_required(VERSION 3.10)

# ---- Project ----

project(shmap VERSION 1.0.0)

set(TARGET_LIB ${PROJECT_NAME})

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif()

option(ENABLE_UT   "Build unit tests" ON)
option(ENABLE_BT   "Build bench tests" OFF)
option(ENABLE_ASON "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

set(CMAKE_CXX_STANDARD 17)

if(ENABLE_UT)
    add_definitions(-DENABLE_UT)
    message(STATUS "Open debug mode")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-inline -std=c++17")
    add_definitions(-DENABLE_LOG -Wno-invalid-offsetof -Wno-format)
endif()

if(ENABLE_BT)
    add_definitions(-DENABLE_BT)
    message(STATUS "Open release mode")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
    add_definitions(-Wno-invalid-offsetof -Wno-format)
endif()

if(ENABLE_ASON)
    message(STATUS "AddressSanitizer enabled")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address") 
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIC -fPIE")
endif()

add_subdirectory(test)
