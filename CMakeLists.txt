cmake_minimum_required(VERSION 3.20)

project(
    nplex-cpp
    VERSION 0.6.0
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

# Requires libuv >= 1.35
find_package(
    libuv REQUIRED
)

find_package(
    fmt 8 REQUIRED
)

find_package(
    flatbuffers 22.01.01 REQUIRED
)

# Re-configure when any .fbs file changes (to recompute schema hash)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/schema1.fbs
    ${PROJECT_SOURCE_DIR}/src/schema2.fbs
    ${PROJECT_SOURCE_DIR}/src/schema3.fbs
)

# Find flatc executable
find_program(FLATC_EXECUTABLE NAMES flatc REQUIRED)

# Generate binary schema at configure time and compute the hash
execute_process(
    COMMAND ${FLATC_EXECUTABLE}
        -b --schema
        -o ${CMAKE_CURRENT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/src/schema3.fbs
    RESULT_VARIABLE FLATC_BFBS_RESULT
    ERROR_VARIABLE  FLATC_BFBS_ERROR
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src
)
if(NOT FLATC_BFBS_RESULT EQUAL 0)
    message(FATAL_ERROR "flatc -b --schema schema3.fbs failed: ${FLATC_BFBS_ERROR}")
endif()

# Compute SHA-256 hash of the binary schema3, truncated to 32 bits
file(SHA256 "${CMAKE_CURRENT_BINARY_DIR}/schema3.bfbs" _HASH_FULL)
string(SUBSTRING "${_HASH_FULL}" 0 8 SCHEMA3_HASH)
message(STATUS "SCHEMA3_HASH hash (bfbs): 0x${SCHEMA3_HASH}")

# Generate buildinfo.hpp
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/buildinfo.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.hpp
    @ONLY
)

# Copy buildinfo.hpp to src/buildinfo.hpp
add_custom_command(
    OUTPUT "${PROJECT_SOURCE_DIR}/src/buildinfo.hpp"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/buildinfo.hpp"
        "${PROJECT_SOURCE_DIR}/src/buildinfo.hpp"
    DEPENDS 
        "${CMAKE_CURRENT_BINARY_DIR}/buildinfo.hpp"
    COMMENT "Copying buildinfo.hpp to src directory"
)

# Create schema3_generated.hpp from binary schema
set(SCHEMA_GENERATED_HPP "${CMAKE_BINARY_DIR}/flatbuffers_generated/schema3_generated.hpp")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/flatbuffers_generated")

add_custom_command(
    OUTPUT ${SCHEMA_GENERATED_HPP}
    COMMAND ${FLATC_EXECUTABLE}
        --cpp
        --filename-ext hpp
        --cpp-std c++17
        --scoped-enums
        --gen-object-api
        -o "${CMAKE_BINARY_DIR}/flatbuffers_generated"
        "${CMAKE_CURRENT_BINARY_DIR}/schema3.bfbs"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/schema3.bfbs"
    COMMENT "Generating schema3_generated.hpp from schema3.bfbs"
)

add_custom_target(
    GENERATE_flatbuffers_generated ALL
    DEPENDS ${SCHEMA_GENERATED_HPP}
)

# Copy schema3_generated.hpp to src/schema.hpp
add_custom_command(
    OUTPUT "${PROJECT_SOURCE_DIR}/src/schema.hpp"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/flatbuffers_generated/schema3_generated.hpp"
        "${PROJECT_SOURCE_DIR}/src/schema.hpp"
    DEPENDS
        GENERATE_flatbuffers_generated
        "${CMAKE_CURRENT_BINARY_DIR}/schema3.bfbs"
    COMMENT "Copying schema.hpp to src directory"
)

# Copy generated files to src/ directory
add_custom_target(
    copy_generated
    ALL
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/buildinfo.hpp"
        "${PROJECT_SOURCE_DIR}/src/schema.hpp"
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
option(ENABLE_THREAD_SANITIZER "Enables ThreadSanitizer." OFF)

if(ENABLE_SANITIZERS AND ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_THREAD_SANITIZER cannot be enabled at the same time.")
endif()

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

if(ENABLE_THREAD_SANITIZER)
    add_compile_options(
        -fno-omit-frame-pointer
        -fsanitize=thread
    )
endif()

option(ENABLE_COVERAGE "Compile and link code for coverage" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(
        --coverage
        -O0
    )
endif()

option(ENABLE_PROFILER "Compile and link code for gprof profiling" OFF)
if(ENABLE_PROFILER)
    add_compile_options(
        -O2
        -pg
    )
    add_link_options(
        -pg
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

add_dependencies(
    nplex-cpp
    copy_generated
)

target_sources(
    nplex-cpp
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/deps/cppcrc.h
    ${CMAKE_CURRENT_LIST_DIR}/deps/cqueue.hpp
    ${CMAKE_CURRENT_LIST_DIR}/deps/cstring.hpp
    ${CMAKE_CURRENT_LIST_DIR}/deps/match.c
    ${CMAKE_CURRENT_LIST_DIR}/deps/match.h
    ${CMAKE_CURRENT_LIST_DIR}/deps/utf8.h
    ${CMAKE_CURRENT_LIST_DIR}/src/addr.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/addr.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/buildinfo.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/client_impl.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/client_impl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/client.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/connection.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/connection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/json.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/json.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/loggable.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/messaging.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/messaging.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/params.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/schema1.fbs
    ${CMAKE_CURRENT_LIST_DIR}/src/schema2.fbs
    ${CMAKE_CURRENT_LIST_DIR}/src/schema3.fbs
    ${CMAKE_CURRENT_LIST_DIR}/src/schema.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/store.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/store.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/transaction_impl.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/transaction_impl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/user.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/user.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils.hpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils.cpp
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
    "$<$<STREQUAL:${ENABLE_THREAD_SANITIZER},ON>:-ltsan>"
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
    "$<$<STREQUAL:${ENABLE_THREAD_SANITIZER},ON>:-ltsan>"
    "$<$<STREQUAL:${ENABLE_COVERAGE},ON>:-lgcov>"
    nplex-cpp
)

add_executable(
    flooder
)

target_sources(
    flooder
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/examples/flooder.cpp
)

target_include_directories(
    flooder
    PRIVATE
    ${PROJECT_SOURCE_DIR}/deps
)

target_link_libraries(
    flooder
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    "$<$<STREQUAL:${ENABLE_THREAD_SANITIZER},ON>:-ltsan>"
    "$<$<STREQUAL:${ENABLE_COVERAGE},ON>:-lgcov>"
    nplex-cpp
)

add_executable(
    functests
)

target_sources(
    functests
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/examples/functests.cpp
)

target_include_directories(
    functests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/deps
)

target_link_libraries(
    functests
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    "$<$<STREQUAL:${ENABLE_THREAD_SANITIZER},ON>:-ltsan>"
    "$<$<STREQUAL:${ENABLE_COVERAGE},ON>:-lgcov>"
    nplex-cpp
)

include(CTest)
enable_testing()
add_subdirectory(test)
