cmake_minimum_required(VERSION 3.17)
project(rectify C)
enable_testing()

set(CMAKE_C_STANDARD 99)


set(deps ../../deps/)
set(local_deps ./deps/)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(isDebug TRUE)
else()
    set(isDebug FALSE)
endif()

if (isDebug)
    message("Debug build detected")
    add_compile_definitions(CONFIGURATION_DEBUG TYRAN_CONFIGURATION_DEBUG)
    set(CONFIGURATION_DEBUG 1)
endif()

file(GLOB_RECURSE local_deps_src FOLLOW_SYMLINKS
        "${local_deps}*/src/lib/*.c"
        "${deps}*/*/src/platform/posix/*.c"
        "${deps}*/*/src/lib/*.c"
        )

add_executable(rectify_test
    main.c
    test.c
    ${local_deps_src}
)

add_test(NAME rectify_test
         COMMAND rectify_test)


target_include_directories(rectify_test PUBLIC ${deps}piot/flood-c/src/include)

if (WIN32)
target_link_libraries(rectify_test rectify)
else()
target_link_libraries(rectify_test rectify m)
endif(WIN32)

