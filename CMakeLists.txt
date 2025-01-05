cmake_minimum_required(VERSION 3.20)

project(
    nplex
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

find_package(
    Threads REQUIRED
)

find_package(
    libuv REQUIRED
)

find_package(
    fmt 7 REQUIRED
)

find_package(
    flatbuffers REQUIRED
)

add_compile_options(
    -march=native
    -Wall
    -Wextra
    -Wshadow
    #-Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdisabled-optimization
    -Wundef
)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
add_compile_options(
    -Wduplicated-branches
    -Wduplicated-cond
    -Wuseless-cast
    -Wlogical-op
)
endif()

option(ENABLE_SANITIZERS "Enables AddressSanitizer and UndefinedBehaviorSanitizer." OFF)
if (ENABLE_SANITIZERS)
    add_compile_options(
        -fno-omit-frame-pointer
        -fsanitize=address
        -fsanitize=undefined
        -fsanitize=float-cast-overflow
        -fsanitize-address-use-after-scope
        -fno-sanitize-recover
    )
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

option(DEBUG "Compile sources in DEBUG mode" OFF)
option(COVERAGE "Compile and link code for coverage" OFF)

include(CTest)
enable_testing()
add_subdirectory(test)
