add_executable(
    tests
)

set_target_properties(
    tests
    PROPERTIES
    CXX_CLANG_TIDY ""
)

target_include_directories(
    tests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/deps
)

target_sources(
    tests
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/transaction_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/exception_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/messages_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mqueue_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cache_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/match_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

target_link_libraries(
    tests
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    "$<$<STREQUAL:${ENABLE_COVERAGE},ON>:-lgcov>"
    Threads::Threads
    nplex-cpp
    libuv::uv
    fmt::fmt
    flatbuffers::flatbuffers_shared
)

add_test(
    NAME tests
    COMMAND $<TARGET_FILE:tests>
)
