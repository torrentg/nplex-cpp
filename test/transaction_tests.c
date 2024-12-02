
#include "acutest.h"
#include "transaction.h"

// gcc -march=native -std=c11 -g -Wall -Wextra -D_DEFAULT_SOURCE -DRUNNING_ON_VALGRIND -I../deps -I../src -I/usr/include/tirpc -o transaction_tests ../src/transaction.c ../deps/yyjson.c ../src/utils.c ../src/json_funcs.c  ../src/xdr_funcs.c ../deps/base64.c transaction_tests.c -ltirpc
// valgrind --tool=memcheck --leak-check=yes ./transaction_tests

static bool str_eq(const char *lhs, const char *rhs)
{
    if (lhs && rhs && strcmp(lhs, rhs) != 0)
        return false;

    if ((!lhs || !rhs) && lhs != rhs)
        return false;

    return true;
}

static bool buf_eq(buf_t lhs, buf_t rhs)
{
    if (lhs.length != rhs.length)
        return false;

    if (lhs.length == 0)
        return true;
    
    if (!lhs.data || !rhs.data)
        return false;

    if (strncmp(lhs.data, rhs.data, lhs.length) != 0)
        return false;

    return true;
}

static bool tx_entry_eq(tx_entry_t *lhs, tx_entry_t *rhs)
{
    if (!str_eq(lhs->key, rhs->key))
        return false;

    if (!buf_eq(lhs->value, rhs->value))
        return false;

    return true;
}

static bool transaction_eq(transaction_t *lhs, transaction_t *rhs)
{
    if (!lhs || !rhs)
        return (lhs == rhs);
    
    if (lhs->rev != rhs->rev)
        return false;

    if (!str_eq(lhs->user, rhs->user))
        return false;

    if (lhs->timestamp != rhs->timestamp)
        return false;

    if (lhs->type != rhs->type)
        return false;

    if (lhs->mode != rhs->mode)
        return false;

    if (lhs->entries.length != rhs->entries.length)
        return false;

    if (lhs->entries.length == 0)
        return true;

    for (size_t i = 0; i < lhs->entries.length; i++)
        if (!tx_entry_eq(&lhs->entries.data[i], &rhs->entries.data[i]))
            return false;

    return true;
}

static void test_transaction_format_ok(format_e format)
{
    transaction_writer_t *writer = NULL;
    transaction_reader_t *reader = NULL;
    buf_t buf = {0};

    writer = transaction_writer_new(format);
    TEST_ASSERT(writer);

    reader = transaction_reader_new(format);
    TEST_ASSERT(reader);

    tx_entry_t entries[] = {
        { .key = "/energy/as27/status", .value = { .data = "disabled", .capacity = 8, .length = 8 } },
        { .key = "/energy/as26/status", .action = TX_ACTION_UPSERT, .value = { .data = "enabled", .capacity = 7, .length = 7 } },
        { .key = "/energy/as28/status", .action = TX_ACTION_CHECK },
        { .key = "/energy/as29/status", .action = TX_ACTION_DELETE },
        { .key = "/energy/as27/watts", .value = { .data = "25", .capacity = 2, .length = 2 }, .action = TX_ACTION_UPSERT },
        { .key = "/energy/as27/png", .value = { .data = NULL, .capacity = 0, .length = 0 } },
        { .key = "/energy/as27/json", .value = { .data = "{ \"var\": [ 42 ] }", .capacity = 17, .length = 17 } },
    };

    transaction_t tx1 = {
        .rev = 1024,
        .user = "jdoe",
        .timestamp = 1732791529411,
        .type = 15,
        .mode = TX_MODE_DIRTY,
        .entries = (tx_entries_t) { .data = entries, .length = 7, .capacity = 7 }
    };
    transaction_t tx2 = {0};

    // we check for memory leaks in the allocator
    for (size_t i = 0; i < 10; i++)
    {
        TEST_CHECK(transaction_writer_serialize(writer, &tx1, &buf));
        TEST_ASSERT(buf.data && buf.length > 10);

        TEST_CHECK(transaction_reader_deserialize(reader, &buf, &tx2));
        TEST_CHECK(transaction_eq(&tx1, &tx2));

        TEST_CHECK(tx2.entries.data != NULL);
        TEST_CHECK(tx2.entries.length == 7);
        transaction_clear(&tx2);
        TEST_CHECK(tx2.entries.data != NULL);
        TEST_CHECK(tx2.entries.length == 0);

        transaction_reset(&tx2);
    }

    // if (format == FORMAT_JSON)
    //     printf("\n%s\n", buf.data);

    transaction_writer_free(writer);
    transaction_reader_free(reader);
}

static void test_transaction_format_ko(format_e format)
{
    transaction_writer_t *writer = NULL;
    transaction_reader_t *reader = NULL;
    buf_t buf = {0};

    writer = transaction_writer_new(format);
    TEST_ASSERT(writer);

    reader = transaction_reader_new(format);
    TEST_ASSERT(reader);

    tx_entry_t entries[] = {
        { .key = "/energy/as27/status", .value = { .data = "disabled", .capacity = 8, .length = 8 } },
        { .key = "/energy/as26/status", .action = TX_ACTION_UPSERT, .value = { .data = "enabled", .capacity = 7, .length = 7 } },
        { .key = "/energy/as28/status", .action = TX_ACTION_CHECK },
        { .key = "/energy/as29/status", .action = TX_ACTION_DELETE },
        { .key = "/energy/as27/watts", .value = { .data = "25", .capacity = 2, .length = 2 }, .action = TX_ACTION_UPSERT },
        { .key = NULL, .value = { .data = NULL, .capacity = 0, .length = 0 } }, // <- INVALID KEY!!!
        { .key = "/energy/as27/json", .value = { .data = "{ \"var\": [ 42 ] }", .capacity = 17, .length = 17 } },
    };

    transaction_t tx1 = {
        .rev = 1024,
        .user = "jdoe",
        .timestamp = 1732791529411,
        .type = 15,
        .mode = TX_MODE_DIRTY,
        .entries = (tx_entries_t) { .data = entries, .length = 7, .capacity = 7 }
    };

    // try to encode an invalid transaction (key with NULL)
    TEST_CHECK(!transaction_writer_serialize(writer, &tx1, &buf));

    // try to encode an invalid transaction (num_entries > 0 and entries = NULL)
    tx1.entries = (tx_entries_t) { .data = NULL, .length = 7, .capacity = 7 };
    TEST_CHECK(!transaction_writer_serialize(writer, &tx1, &buf));

    // trying to decode garbage
    transaction_t tx2 = {0};
    buf = (buf_t){ .data = "garbage", .length = 7, .capacity = 7 };
    TEST_CHECK(!transaction_reader_deserialize(reader, &buf, &tx2));

    transaction_writer_free(writer);
    transaction_reader_free(reader);
}

static void test_transaction_json(void)
{
    test_transaction_format_ok(FORMAT_JSON);
    test_transaction_format_ko(FORMAT_JSON);
}

static void test_transaction_xdr(void)
{
    test_transaction_format_ok(FORMAT_XDR);
    test_transaction_format_ko(FORMAT_XDR);
}

TEST_LIST = {
    { "transaction_json",                  test_transaction_json },
    { "transaction_xdr",                   test_transaction_xdr },
    { NULL, NULL }
};
