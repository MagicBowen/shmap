if(ENABLE_UT)
    add_subdirectory(googletest-release-1.12.0)
endif()

if(ENABLE_BT)
    add_subdirectory(benchmark-1.6.1)
endif()