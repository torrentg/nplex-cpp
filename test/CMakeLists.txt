add_executable(
    tests
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
    ${CMAKE_CURRENT_LIST_DIR}/exception_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mqueue_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

target_link_libraries(
    tests
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    Threads::Threads
    libuv::uv
    fmt::fmt
    flatbuffers
)

add_test(
    NAME tests
    COMMAND $<TARGET_FILE:tests>
)
