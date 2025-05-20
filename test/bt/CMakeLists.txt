# ---- Name of test target ----

set(TEST_TARGET ${TARGET_LIB}_bench_test)

# ---- Source files of test ----

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "*.c" "*.C" "*.cc" "*.CC" "*.cpp" "*.CPP" "*.c++")

# ---- Define test target ----

add_executable(${TEST_TARGET} ${SOURCES})

target_include_directories(${TEST_TARGET}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/test )

target_link_libraries(${TEST_TARGET} PRIVATE benchmark)

set_target_properties(${TEST_TARGET} PROPERTIES CXX_STANDARD 17)