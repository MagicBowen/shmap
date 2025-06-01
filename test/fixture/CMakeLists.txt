file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "*.c" "*.C" "*.cc" "*.CC" "*.cpp" "*.CPP" "*.c++")

add_library(fixture ${SOURCES})

target_include_directories(fixture
    PUBLIC   ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE  ${PROJECT_SOURCE_DIR}/include)

set_target_properties(fixture PROPERTIES CXX_STANDARD 17)