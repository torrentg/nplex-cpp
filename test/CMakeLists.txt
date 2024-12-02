find_package(
    Threads REQUIRED
)

find_package(
    libuv REQUIRED
)

set(TEST_DIR ${CMAKE_CURRENT_LIST_DIR})
set(DEPS_DIR ${PROJECT_SOURCE_DIR}/deps)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)

add_definitions(-DRUNNING_ON_VALGRIND -D_DEFAULT_SOURCE)

include_directories(
    SYSTEM
    /usr/include/tirpc
)

include_directories(
    ${DEPS_DIR}
    ${SRC_DIR}
)

link_libraries(
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lasan>"
    "$<$<STREQUAL:${ENABLE_SANITIZERS},ON>:-lubsan>"
    Threads::Threads
    -ltirpc
    libuv::uv
)

add_executable(
    utils_tests
    ${SRC_DIR}/utils.c
    ${TEST_DIR}/utils_tests.c
)
add_test(
    NAME utils_tests
    COMMAND COMMAND $<TARGET_FILE:utils_tests>
)

add_executable(
    permissions_tests
    ${DEPS_DIR}/match.c
    ${SRC_DIR}/utils.c
    ${SRC_DIR}/permissions.c
    ${TEST_DIR}/permissions_tests.c
)
add_test(
    NAME permissions_tests
    COMMAND COMMAND $<TARGET_FILE:permissions_tests>
)

add_executable(
    transaction_tests
    ${DEPS_DIR}/yyjson.c
    ${DEPS_DIR}/base64.c
    ${SRC_DIR}/utils.c
    ${SRC_DIR}/xdr_funcs.c
    ${SRC_DIR}/json_funcs.c
    ${SRC_DIR}/transaction.c
    ${TEST_DIR}/transaction_tests.c
)
add_test(
    NAME transaction_tests
    COMMAND $<TARGET_FILE:transaction_tests>
)
