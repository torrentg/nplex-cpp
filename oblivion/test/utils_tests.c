#include "acutest.h"
#include "utils.h"

// gcc -march=native -std=c11 -g -Wall -Wextra -D_DEFAULT_SOURCE -DRUNNING_ON_VALGRIND -I../src -I../deps -o utils_tests utils_tests.c ../src/utils.c
// valgrind --tool=memcheck --leak-check=yes ./utils_tests

DECL_BUF_T(uint64_t) uint64_buf_t;

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

static void test_buf_type(buf_t *buf, size_t size)
{
    const void *ptr = NULL;
    char data[3*1024] = {0};

    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = '0' + i;

    buf_reset(buf);
    TEST_CHECK(buf->data == NULL);
    TEST_CHECK(buf->length == 0);
    TEST_CHECK(buf->capacity == 0);

    TEST_CHECK(buf_reserve(buf, 14, size));
    TEST_CHECK(buf->data != NULL);
    TEST_CHECK(buf->length == 0);
    TEST_CHECK(buf->capacity == 14);

    ptr = buf->data;
    TEST_CHECK(buf_append(buf, data, 10, size));
    TEST_CHECK(buf->data == ptr);
    TEST_CHECK(buf->length == 10);
    TEST_CHECK(buf->capacity == 14);

    TEST_CHECK(buf_reserve(buf, 3, size));
    TEST_CHECK(buf->data == ptr);
    TEST_CHECK(buf->length == 10);
    TEST_CHECK(buf->capacity == 14);

    TEST_CHECK(buf_append(buf, data, 10, size));
    TEST_CHECK(buf->data != NULL);
    TEST_CHECK(buf->length == 20);
    TEST_CHECK(buf->capacity == 28);

    buf_reset(buf);
    TEST_CHECK(buf->data == NULL);
    TEST_CHECK(buf->length == 0);
    TEST_CHECK(buf->capacity == 0);

    TEST_CHECK(buf_append(buf, data, 10, size));
    TEST_CHECK(buf->data != NULL);
    TEST_CHECK(buf->length == 10);
    TEST_CHECK(buf->capacity == 10);

    buf_reset(buf);
    TEST_CHECK(buf->data == NULL);
    TEST_CHECK(buf->length == 0);
    TEST_CHECK(buf->capacity == 0);

    // buf_append errors
    TEST_CHECK(!buf_is_valid(NULL));
    TEST_CHECK(!buf_append(NULL, data, 10, size));
    TEST_CHECK(!buf_append(buf, NULL, 10, size));

    // corrupted object (NULL data and capacity > 0)
    *buf = (buf_t){ .data = NULL, .length = 0, .capacity = 10 };
    TEST_CHECK(!buf_is_valid(buf));
    TEST_CHECK(!buf_reserve(buf, 1, size));
    TEST_CHECK(!buf_append(buf, data, 10, size));

    // corrupted object (length > capacity)
    *buf = (buf_t){ .data = NULL, .length = 10, .capacity = 0 };
    TEST_CHECK(!buf_is_valid(buf));
    TEST_CHECK(!buf_reserve(buf, 1, size));
    TEST_CHECK(!buf_append(buf, data, 10, size));

    *buf = (buf_t){0};
}

static void test_buf_functions(void)
{
    char_buf_t buf1 = {0};
    test_buf_type((buf_t *) &buf1, sizeof(char));

    uint64_buf_t buf2 = {0};
    test_buf_type((buf_t *) &buf2, sizeof(uint64_t));
}

TEST_LIST = {
    { "iso8601_conversions",            test_iso8601_conversions },
    { "is_utf8()",                      test_is_utf8 },
    { "buf_functions",                  test_buf_functions },
    { NULL, NULL }
};
