add_subdirectory(3rd)
add_subdirectory(fixture)

if(ENABLE_UT)
    add_subdirectory(ut)
endif()

if(ENABLE_BT)
    add_subdirectory(bt)
endif()