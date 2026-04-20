#include <doctest.h>
#include "nplex-cpp/exception.hpp"
#include "nplex-cpp/transaction.hpp"
#include "transaction_impl.hpp"
#include "schema_test.hpp"
#include "store.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

namespace {

class transaction_test : public transaction_impl
{
  public:
    transaction_test(store_ptr store, isolation_e isolation, bool read_only = false) : transaction_impl(store, isolation, read_only) {}
    void set_state(state_e state) { m_state = state; }
    void set_dirty(bool dirty) { m_dirty = dirty; }
};

auto update_store(store_ptr &store, const msgs::UpdateT &upd)
{
    meta_ptr meta = nullptr;
    std::vector<change_t> changes;
    auto detached_buf = serialize(upd);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    REQUIRE_NOTHROW(std::tie(changes, meta) = store->update(update_ptr));

    return changes;
}

/**
 * Creates a basic store initialized with some values.
 * 
 * // value first digit represents the key number, 
 * // second digit represents the last transaction that modified.
 * 
 * key-values = {
 *      key1 = 11
 *      key2 = 21
 *      key3 = 32
 *      key4 = 42
 *      key5 = removed
 *      key6 = removed
 *      key7 = 72
 *      key8 = 82
 * }
 * 
 * tx-metadatas = {
 *     { rev = 1, user = jdoe },
 *     { rev = 2, user = ljohnson }
 * }
 * 
 * tx-users = { 
 *      jdoe, 
 *      ljohnson
 * }
 * 
 * @param[in] rev The store revision.
 * 
 * @return Initialized store.
 */
store_ptr make_basic_store()
{
    flatbuffers::DetachedBuffer buf;
    store_ptr store = std::make_shared<store_t>();
    const msgs::Update *ptr = nullptr;

    auto update_msg1 = make_update(1, "jdoe", 1234567890, 15,
        {
            { .key = "key1", .value = {11}},
            { .key = "key2", .value = {21}},
            { .key = "key3", .value = {31}},
            { .key = "key4", .value = {41}},
            { .key = "key5", .value = {51}},
            { .key = "key6", .value = {61}}
        },
        {}
    );

    buf = serialize(update_msg1);
    ptr = flatbuffers::GetRoot<nplex::msgs::Update>(buf.data());

    REQUIRE_NOTHROW(store->update(ptr));
    CHECK(store->m_rev == 1);

    auto update_msg2 = make_update(2, "ljohnson", 1234567895, 7,
        {
            { .key = "key3", .value = {32}},
            { .key = "key4", .value = {42}},
            { .key = "key7", .value = {72}},
            { .key = "key8", .value = {82}}
        },
        { "key5", "key6" }
    );

    buf = serialize(update_msg2);
    ptr = flatbuffers::GetRoot<nplex::msgs::Update>(buf.data());

    REQUIRE_NOTHROW(store->update(ptr));
    CHECK(store->m_rev == 2);

    return store;
}

void basic_step_1(std::shared_ptr<transaction_test> &tx)
{
    // scenario: tx just created and no updates yet

    CHECK(tx->state() == transaction::state_e::OPEN);
    CHECK(!tx->is_read_only());
    CHECK(!tx->is_dirty());

    CHECK(tx->type() == 0);
    tx->set_type(17);
    CHECK(tx->type() == 17);

    // read an existing key
    REQUIRE(tx->read("key1"));
    CHECK(tx->read("key1")->data()[0] == 11);
    CHECK(tx->read("key1")->rev() == 1);

    // read a non-existing key
    CHECK(!tx->read("key99"));

    // update a key
    REQUIRE(tx->read("key2"));
    CHECK(tx->read("key2")->data()[0] == 21);
    CHECK(tx->read("key2")->rev() == 1);
    CHECK_NOTHROW(tx->upsert("key2", "x"));
    CHECK(tx->read("key2")->data()[0] == 'x');
    CHECK(tx->read("key2")->rev() == 0);

    // insert a key-value
    CHECK(!tx->read("key10"));
    CHECK_NOTHROW(tx->upsert("key10", "yy"));
    CHECK(tx->read("key10")->data()[0] == 'y');
    CHECK(tx->read("key10")->rev() == 0);
    CHECK(tx->upsert_if_changed("key10", "yy") == false);

    // delete a key
    REQUIRE(tx->read("key3"));
    CHECK(tx->remove(gto::cstring{"key3"}));
    CHECK(!tx->read("key3"));
}

} // namespace unnamed

TEST_CASE("transaction_test")
{
    store_ptr store = make_basic_store();

    SUBCASE("read_committed_basic")
    {
        auto tx = std::make_shared<transaction_test>(store, transaction::isolation_e::READ_COMMITTED);

        CHECK(tx->isolation() == transaction::isolation_e::READ_COMMITTED);

        // no updates yet
        basic_step_1(tx);

        // updating store + tx
        auto changes = update_store(store, 
            make_update(3, "jdoe", 1234567890, 15, {
                    { .key = "key1", .value = {13}},
                    { .key = "key2", .value = {23}},    // cause dirty
                    { .key = "key3", .value = {33}},    // cause dirty
                    { .key = "key7", .value = {73}},
                    { .key = "key99", .value = {99}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());

        // reading a read key whose value was updated externally
        REQUIRE(tx->read("key1"));
        CHECK(tx->read("key1")->data()[0] == 13);
        CHECK(tx->read("key1")->rev() == 3);

        // reading a modified key whose value was updated externally
        REQUIRE(tx->read("key2"));
        CHECK(tx->read("key2")->data()[0] == 'x');
        CHECK(tx->read("key2")->rev() == 0);

        // reading a deleted key that was updated externally
        CHECK(!tx->read("key3"));

        // reading a non-read key not updated externally
        REQUIRE(tx->read("key4"));
        CHECK(tx->read("key4")->data()[0] == 42);
        CHECK(tx->read("key4")->rev() == 2);

        // reading a non-read key updated externally
        REQUIRE(tx->read("key7"));
        CHECK(tx->read("key7")->data()[0] == 73);
        CHECK(tx->read("key7")->rev() == 3);

        // reading a new key
        REQUIRE(tx->read("key99"));
        CHECK(tx->read("key99")->data()[0] == 99);
        CHECK(tx->read("key99")->rev() == 3);
    }

    SUBCASE("repeatable_read_basic")
    {
        auto tx = std::make_shared<transaction_test>(store, transaction::isolation_e::REPEATABLE_READ);

        CHECK(tx->isolation() == transaction::isolation_e::REPEATABLE_READ);

        basic_step_1(tx);

        // updating store + tx
        auto changes = update_store(store, 
            make_update(3, "jdoe", 1234567890, 15, {
                    { .key = "key1", .value = {13}},
                    { .key = "key2", .value = {23}},    // cause dirty
                    { .key = "key3", .value = {33}},    // cause dirty
                    { .key = "key7", .value = {73}},
                    { .key = "key99", .value = {99}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());

        // reading a read key whose value was updated externally
        REQUIRE(tx->read("key1"));
        CHECK(tx->read("key1")->data()[0] == 11);
        CHECK(tx->read("key1")->rev() == 1);

        // reading a modified key whose value was updated externally
        REQUIRE(tx->read("key2"));
        CHECK(tx->read("key2")->data()[0] == 'x');
        CHECK(tx->read("key2")->rev() == 0);

        // reading a deleted key that was updated externally
        CHECK(!tx->read("key3"));

        // reading a non-read key not updated externally
        REQUIRE(tx->read("key4"));
        CHECK(tx->read("key4")->data()[0] == 42);
        CHECK(tx->read("key4")->rev() == 2);

        // reading a non-read key updated externally
        REQUIRE(tx->read("key7"));
        CHECK(tx->read("key7")->data()[0] == 73);
        CHECK(tx->read("key7")->rev() == 3);

        // reading a previously read non-existing key updated externally
        CHECK(tx->read("key99") == nullptr);
    }

    SUBCASE("serializable_basic")
    {
        auto tx = std::make_shared<transaction_test>(store, transaction::isolation_e::SERIALIZABLE);

        CHECK(tx->isolation() == transaction::isolation_e::SERIALIZABLE);

        basic_step_1(tx);

        // updating store + tx
        auto changes = update_store(store, 
            make_update(3, "jdoe", 1234567890, 15, {
                    { .key = "key1", .value = {13}},
                    { .key = "key2", .value = {23}},    // cause dirty
                    { .key = "key3", .value = {33}},    // cause dirty
                    { .key = "key7", .value = {73}},
                    { .key = "key99", .value = {99}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());

        // reading a read key whose value was updated externally
        REQUIRE(tx->read("key1"));
        CHECK(tx->read("key1")->data()[0] == 11);
        CHECK(tx->read("key1")->rev() == 1);

        // reading a modified key whose value was updated externally
        REQUIRE(tx->read("key2"));
        CHECK(tx->read("key2")->data()[0] == 'x');
        CHECK(tx->read("key2")->rev() == 0);

        // reading a deleted key that was updated externally
        CHECK(!tx->read("key3"));

        // reading a non-read key not updated externally
        REQUIRE(tx->read("key4"));
        CHECK(tx->read("key4")->data()[0] == 42);
        CHECK(tx->read("key4")->rev() == 2);

        // reading a non-read key updated externally
        REQUIRE(tx->read("key7"));
        CHECK(tx->read("key7")->data()[0] == 72);
        CHECK(tx->read("key7")->rev() == 2);

        // reading a previously read non-existing key updated externally
        CHECK(tx->read("key99") == nullptr);
    }

    SUBCASE("read_upsert_remove_exceptions")
    {
        auto tx = std::make_shared<transaction_test>(store, transaction::isolation_e::SERIALIZABLE, true);

        CHECK_THROWS_AS(tx->upsert("key1", "abc"), nplex_exception); // read-only exception
        CHECK_THROWS_AS(tx->remove("key1"), nplex_exception); // read-only exception
        tx->set_state(transaction::state_e::ABORTED);
        CHECK_THROWS_AS(tx->read("key1"), nplex_exception); // not-open exception
    }
}

TEST_CASE("transaction_for_each")
{
    store_ptr store = make_basic_store();
    auto tx = std::make_shared<transaction_test>(store, transaction::isolation_e::READ_COMMITTED);

    SUBCASE("iterate_with_callback_null")
    {
        CHECK(tx->for_each("key1", transaction::callback_t{}) == 0);
    }

    SUBCASE("iterate_with_key_null")
    {
        CHECK(tx->for_each(nullptr, [](auto, auto) { return true; }) == 0);
    }

    SUBCASE("iterate_all_only_store")
    {
        const char *keys[] = { "key1", "key2", "key3", "key4", "key7", "key8" };
        std::size_t pos = 0;
        std::size_t count = 0;
        bool success = true;

        REQUIRE_NOTHROW(count = tx->for_each("*", [&keys, &pos, &success]([[maybe_unused]] const nplex::key_t &key, [[maybe_unused]] const value_t &value) {
            if (pos >= 6 || key != keys[pos++])
                success = false;
            return true;
        }));

        CHECK(count == 6);
        CHECK(success);
    }

    SUBCASE("iterate_all_tx_and_store")
    {
        const char *keys[] = { "key1", "key10", "key2", "key4", "key7", "key8" };
        std::size_t pos = 0;
        std::size_t count = 0;
        bool success = true;

        basic_step_1(tx);

        // removed key3 and added key10
        REQUIRE_NOTHROW(count = tx->for_each("*", [&keys, &pos, &success]([[maybe_unused]] const nplex::key_t &key, [[maybe_unused]] const value_t &value) {
            if (pos >= 6 || key != keys[pos++])
                success = false;
            return true;
        }));

        CHECK(count == 6);
        CHECK(success);
    }

    SUBCASE("iterate_some_tx_and_store_1")
    {
        const char *keys[] = { "key1", "key10" };
        std::size_t pos = 0;
        std::size_t count = 0;
        bool success = true;

        basic_step_1(tx);

        // removed key3 and added key10
        REQUIRE_NOTHROW(count = tx->for_each("key1*", [&keys, &pos, &success]([[maybe_unused]] const nplex::key_t &key, [[maybe_unused]] const value_t &value) {
            if (pos >= 2 || key != keys[pos++])
                success = false;
            return true;
        }));

        CHECK(count == 2);
        CHECK(success);
    }

    SUBCASE("iterate_some_tx_and_store_2")
    {
        const char *keys[] = { "key4" };
        std::size_t pos = 0;
        std::size_t count = 0;
        bool success = true;

        basic_step_1(tx);

        REQUIRE_NOTHROW(count = tx->for_each("*4", [&keys, &pos, &success]([[maybe_unused]] const nplex::key_t &key, [[maybe_unused]] const value_t &value) {
            if (pos >= 2 || key != keys[pos++])
                success = false;
            return true;
        }));

        CHECK(count == 1);
        CHECK(success);
    }

    SUBCASE("no_match")
    {
        std::size_t count = 0;

        REQUIRE_NOTHROW(count = tx->for_each("unknown*", []([[maybe_unused]] const nplex::key_t &key, [[maybe_unused]] const value_t &value) {
            return true;
        }));

        CHECK(count == 0);
    }

    SUBCASE("iterate_exceptions")
    {
        tx->set_state(transaction::state_e::ABORTED);
        CHECK_THROWS_AS(tx->for_each("key1", []([[maybe_unused]] const nplex::key_t &key, [[maybe_unused]] const value_t &value) {
                return true;
            }), nplex_exception);
    }
}

TEST_CASE("transaction_ensure")
{
    store_ptr store = make_basic_store();
    std::vector<nplex::change_t> changes;
    auto tx = std::make_shared<transaction_test>(store, transaction::isolation_e::READ_COMMITTED);

    SUBCASE("ensure_all")
    {
        // any change will make the transaction dirty
        REQUIRE_NOTHROW(tx->ensure("**"));

        // updating key1
        tx->set_dirty(false);
        changes = update_store(store, 
            make_update(3, "jdoe", 1234567890, 15, {
                    { .key = "key1", .value = {13}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());

        // adding new key
        tx->set_dirty(false);
        changes = update_store(store, 
            make_update(4, "jdoe", 1234567890, 15, {
                    { .key = "key99", .value = {13}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());

        // deleting key2
        tx->set_dirty(false);
        changes = update_store(store, 
            make_update(5, "jdoe", 1234567890, 15, 
                {},
                { "key2" }
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());
    }

    SUBCASE("ensure_some")
    {
        // any change on keys matching key1* will make the transaction dirty
        REQUIRE_NOTHROW(tx->ensure("key1*"));

        // updating key1
        tx->set_dirty(false);
        changes = update_store(store, 
            make_update(3, "jdoe", 1234567890, 15, {
                    { .key = "key1", .value = {13}}
                },
                {}
            ));

        // adding key11
        tx->set_dirty(false);
        changes = update_store(store, 
            make_update(4, "jdoe", 1234567890, 15, {
                    { .key = "key11", .value = {13}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());

        // deleting key11
        tx->set_dirty(false);
        changes = update_store(store, 
            make_update(5, "jdoe", 1234567890, 15, {
                    { .key = "key11", .value = {15}}
                },
                {}
            ));

        tx->update(changes);
        CHECK(tx->is_dirty());
    }

    SUBCASE("ensure_error_not_open")
    {
        tx->set_state(transaction::state_e::COMMITTED);
        CHECK_THROWS_AS(tx->ensure("key1"), nplex_exception);
    }
}
