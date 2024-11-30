#include "acutest.h"
#include "utils.h"

// gcc -march=native -std=c11 -g -Wall -Wextra -D_DEFAULT_SOURCE -DRUNNING_ON_VALGRIND -I../src -I../deps -o utils_tests utils_tests.c ../src/utils.c
// valgrind --tool=memcheck --leak-check=yes ./utils_tests

static void test_iso8601_conversions(void)
{
    char buffer[25] = {0};
    uint64_t millis = 0;

    millis = iso8601_to_millis("2024-11-26T12:00:23");
    TEST_CHECK(millis == 1732622423000);

    millis = iso8601_to_millis("2024-11-26T12:00:23Z");
    TEST_CHECK(millis == 1732622423000);

    millis = iso8601_to_millis("2024-11-26T12:00:23+00:00");
    TEST_CHECK(millis == 1732622423000);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930");
    TEST_CHECK(millis == 1732622423930);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930Z");
    TEST_CHECK(millis == 1732622423930);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930z");
    TEST_CHECK(millis == 1732622423930);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930+00");
    TEST_CHECK(millis == 1732622423930);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930+00:00");
    TEST_CHECK(millis == 1732622423930);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930-00");
    TEST_CHECK(millis == 1732622423930);

    millis = iso8601_to_millis("2024-11-26T12:00:23.930-00:00");
    TEST_CHECK(millis == 1732622423930);

    TEST_CHECK(millis_to_iso8601(1732622423930, buffer, sizeof(buffer)));
    TEST_CHECK(strcmp(buffer, "2024-11-26T12:00:23.930Z") == 0);

    millis = iso8601_to_millis("2024-11-26T12:00:23.1");
    TEST_CHECK(millis == 1732622423001);

    millis = iso8601_to_millis("2024-11-26T12:00:23.01");
    TEST_CHECK(millis == 1732622423001);

    millis = iso8601_to_millis("2024-11-26T12:00:23.001");
    TEST_CHECK(millis == 1732622423001);

    // negative cases
    TEST_CHECK(!millis_to_iso8601(1732622423930, NULL, sizeof(buffer)));
    TEST_CHECK(!millis_to_iso8601(1732622423930, buffer, 24));

    // negative cases
    TEST_CHECK(iso8601_to_millis(NULL) == 0);
    TEST_CHECK(iso8601_to_millis("") == 0);
    TEST_CHECK(iso8601_to_millis(" 2024-11-26T12:00:23.930") == 0);         // initial space
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930 ") == 0);         // trailing chars
    TEST_CHECK(iso8601_to_millis("abc") == 0);                              // garbage
    TEST_CHECK(iso8601_to_millis("2024-11-26") == 0);                       // incomplete
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.1000") == 0);         // ms out-of-range
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.+3") == 0);           // ms signed
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930P") == 0);         // unrecognized timezone
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930+1") == 0);        // invalid timezone format
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930+01:0") == 0);     // invalid timezone format
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930*00:00") == 0);    // invalid timezone format
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930+24:00") == 0);    // invalid timezone range
    TEST_CHECK(iso8601_to_millis("2024-11-26T12:00:23.930+01:70") == 0);    // invalid timezone range
}

static void test_is_utf8(void)
{
    char buffer[1024] = {0};

    // ascii content
    strcpy(buffer, "abc\t\n");
    TEST_CHECK(is_utf8(buffer, strlen(buffer)));

    // extended content (I)
    strcpy(buffer, "ñç€");
    TEST_CHECK(is_utf8(buffer, strlen(buffer)));

    // extended content (II)
    strcpy(buffer, "안녕하세요, 세상");
    TEST_CHECK(is_utf8(buffer, strlen(buffer)));

    // json content
    strcpy(buffer, "{ \"var1\": \"value\", \"var2\":[1, 2, 3] }");
    TEST_CHECK(is_utf8(buffer, strlen(buffer)));

    // non-valid utf8 sequences
    strcpy(buffer, "\xc3\x28");
    TEST_CHECK(!is_utf8(buffer, 2));

    // binary content
    strcpy(buffer, "abc\x00xyz");
    TEST_CHECK(!is_utf8(buffer, 8));
}

static void test_buf_functions(void)
{
    buf_t buf = {0};
    char *ptr = NULL;

    buf_reset(&buf);
    TEST_CHECK(buf.data == NULL);
    TEST_CHECK(buf.length == 0);
    TEST_CHECK(buf.reserved == 0);

    TEST_CHECK(buf_reserve(&buf, 14));
    TEST_CHECK(buf.data != NULL);
    TEST_CHECK(buf.length == 0);
    TEST_CHECK(buf.reserved == 14);

    ptr = buf.data;
    TEST_CHECK(buf_append(&buf, "1234567890", 10));
    TEST_CHECK(buf.data == ptr);
    TEST_CHECK(buf.length == 10);
    TEST_CHECK(buf.reserved == 14);

    TEST_CHECK(buf_reserve(&buf, 3));
    TEST_CHECK(buf.data == ptr);
    TEST_CHECK(buf.length == 10);
    TEST_CHECK(buf.reserved == 14);

    TEST_CHECK(buf_append(&buf, "1234567890", 10));
    TEST_CHECK(buf.data != NULL);
    TEST_CHECK(buf.length == 20);
    TEST_CHECK(buf.reserved == 28);

    buf_reset(&buf);
    TEST_CHECK(buf.data == NULL);
    TEST_CHECK(buf.length == 0);
    TEST_CHECK(buf.reserved == 0);

    TEST_CHECK(buf_append(&buf, "1234567890", 10));
    TEST_CHECK(buf.data != NULL);
    TEST_CHECK(buf.length == 10);
    TEST_CHECK(buf.reserved == 10);

    buf_reset(&buf);
    TEST_CHECK(buf.data == NULL);
    TEST_CHECK(buf.length == 0);
    TEST_CHECK(buf.reserved == 0);

    // buf_append errors
    TEST_CHECK(!buf_append(NULL, "1234567890", 10));
    TEST_CHECK(!buf_append(&buf, NULL, 10));

    // corrupted object
    buf_t bad = (buf_t){ .data = NULL, .length = 0, .reserved = 10 };
    TEST_CHECK(!buf_reserve(&bad, 1));
    TEST_CHECK(!buf_append(&bad, "1234567890", 10));
    bad = (buf_t){ .data = NULL, .length = 10, .reserved = 0 };
    TEST_CHECK(!buf_reserve(&bad, 1));
    TEST_CHECK(!buf_append(&bad, "1234567890", 10));
}

TEST_LIST = {
    { "iso8601_conversions",            test_iso8601_conversions },
    { "is_utf8()",                      test_is_utf8 },
    { "buf_functions",                  test_buf_functions },
    { NULL, NULL }
};
