#include "transaction_test.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

namespace {

struct tx_test_t : public transaction_t {
    using transaction_t::transaction_t;
    void update(const std::vector<change_t> &changes) { transaction_t::update(changes); }
};

using tx_ptr = std::shared_ptr<tx_test_t>;

auto update_cache(cache_ptr &cache, const msgs::UpdateT &upd)
{
    std::vector<change_t> changes;
    auto detached_buf = serialize(upd);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    REQUIRE_NOTHROW(changes = cache->update(update_ptr));

    return changes;
}

void basic_step_1(tx_ptr &tx)
{
    // scenario: tx just created and no updates yet

    CHECK(tx->state() == transaction_t::state_e::OPEN);
    CHECK(!tx->read_only());
    CHECK(!tx->dirty());

    CHECK(tx->type() == 0);
    tx->type(17);
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

    // delete a key
    REQUIRE(tx->read("key3"));
    CHECK(tx->remove(gto::cstring{"key3"}));
    CHECK(!tx->read("key3"));
}

} // namespace unnamed

TEST_CASE("transaction_test")
{
    cache_ptr cache = make_basic_cache();

    SUBCASE("read_committed_basic")
    {
        auto tx = std::make_shared<tx_test_t>(cache, transaction_t::isolation_e::READ_COMMITTED);

        CHECK(tx->isolation() == transaction_t::isolation_e::READ_COMMITTED);

        // no updates yet
        basic_step_1(tx);

        // updating cache + tx
        auto changes = update_cache(cache, 
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
        CHECK(tx->dirty());

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

    SUBCASE("repeatable_reads_basic")
    {
        auto tx = std::make_shared<tx_test_t>(cache, transaction_t::isolation_e::REPEATABLE_READS);

        CHECK(tx->isolation() == transaction_t::isolation_e::REPEATABLE_READS);

        basic_step_1(tx);

        // updating cache + tx
        auto changes = update_cache(cache, 
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
        CHECK(tx->dirty());

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

        // reading a new key
        REQUIRE(tx->read("key99"));
        CHECK(tx->read("key99")->data()[0] == 99);
        CHECK(tx->read("key99")->rev() == 3);
    }

    SUBCASE("serializable_basic")
    {
        auto tx = std::make_shared<tx_test_t>(cache, transaction_t::isolation_e::SERIALIZABLE);

        CHECK(tx->isolation() == transaction_t::isolation_e::SERIALIZABLE);

        basic_step_1(tx);

        // updating cache + tx
        auto changes = update_cache(cache, 
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
        CHECK(tx->dirty());

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

        // reading a new key
        REQUIRE(tx->read("key99"));
        CHECK(tx->read("key99")->data()[0] == 99);
        CHECK(tx->read("key99")->rev() == 3);
    }
}
