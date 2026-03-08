cmake_minimum_required(VERSION 3.20)

project(
    nplex-cpp
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
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

# Create messages_generated.hpp from schema (src/messages.fbs)
flatbuffers_generate_headers(
    TARGET flatbuffers_generated
    SCHEMAS ${PROJECT_SOURCE_DIR}/src/messages.fbs
    FLAGS --cpp --filename-ext hpp --cpp-std c++17 --scoped-enums --gen-object-api
)

# Copy messages_generated.hpp to src/messages.hpp
add_custom_command(
    OUTPUT "${PROJECT_SOURCE_DIR}/src/messages.hpp"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/flatbuffers_generated/messages_generated.hpp"
        "${PROJECT_SOURCE_DIR}/src/messages.hpp"
    DEPENDS
        GENERATE_flatbuffers_generated
        ${PROJECT_SOURCE_DIR}/src/messages.fbs
    COMMENT "Copying generated messages.hpp to src directory"
)

add_compile_options(
    -march=native
    -Wall
    -Wextra
    -Wshadow
    -Wpedantic
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
if(ENABLE_SANITIZERS)
    add_compile_options(
        -fno-omit-frame-pointer
        -fsanitize=address
        -fsanitize=undefined
        -fsanitize=float-cast-overflow
        -fsanitize-address-use-after-scope
        -fno-sanitize-recover
    )
endif()

option(ENABLE_COVERAGE "Compile and link code for coverage" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(
        --coverage
        -O0
    )
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

#set(
#    # see https://clang.llvm.org/extra/clang-tidy/checks/list.html
#    CMAKE_CXX_CLANG_TIDY
#    clang-tidy
#)

add_library(
    nplex-cpp
    STATIC
)

target_sources(
    nplex-cpp
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/deps/cppcrc.h
    ${CMAKE_CURRENT_LIST_DIR}/deps/match.c
    ${CMAKE_CURRENT_LIST_DIR}/src/addr.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/addr.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/client_impl.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/client_impl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/client.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/connection.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/connection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/messages.fbs
    ${CMAKE_CURRENT_LIST_DIR}/src/messages.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/messaging.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/messaging.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/misc.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/misc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/params.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/store.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/store.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/transaction_impl.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/transaction_impl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/user.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/user.cpp
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include/nplex-cpp/client.hpp
    ${CMAKE_CURRENT_LIST_DIR}/include/nplex-cpp/exception.hpp
    ${CMAKE_CURRENT_LIST_DIR}/include/nplex-cpp/transaction.hpp
    ${CMAKE_CURRENT_LIST_DIR}/include/nplex-cpp/types.hpp
)

set_target_properties(
    nplex-cpp
    PROPERTIES
    POSITION_INDEPENDENT_CODE 1
    OUTPUT_NAME "nplex-cpp"
)

target_include_directories(
    nplex-cpp
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/deps
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/src/messaging.cpp
    PROPERTIES 
    COMPILE_OPTIONS "-Wno-conversion"
)

target_link_libraries(
    nplex-cpp
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    "$<$<STREQUAL:${ENABLE_COVERAGE},ON>:-lgcov>"
    Threads::Threads
    libuv::uv
    fmt::fmt
    flatbuffers::flatbuffers_shared
)

add_executable(
    example1
)

target_sources(
    example1
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/examples/example1.cpp
)

target_include_directories(
    example1
    PRIVATE
    ${PROJECT_SOURCE_DIR}/deps
)

target_link_libraries(
    example1
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    "$<$<STREQUAL:${ENABLE_COVERAGE},ON>:-lgcov>"
    nplex-cpp
)

include(CTest)
enable_testing()
add_subdirectory(test)
