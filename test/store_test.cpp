#include <doctest.h>
#include "nplex-cpp/exception.hpp"
#include "schema_test.hpp"
#include "store.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

TEST_CASE("store_load")
{
    auto snapshot = make_snapshot(
        1024,
        {
            make_update(42, "jdoe", 1234567890, 15,
                {
                    { .key = "key1", .value = {'a'}},
                    { .key = "key2", .value = {'b'}}
                },
                {}  // there are no deletes on snapshots
            ),
            make_update(546, "ljohnson", 1234567999, 7,
                {
                    { .key = "key5", .value = {'e'}},
                    { .key = "key6", .value = {'f'}}
                },
                {}  // there are no deletes on snapshots
            )
        }
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    store_t store;

    REQUIRE_NOTHROW(store.load(ptr));

    CHECK(store.m_rev == 1024);

    CHECK(store.m_users.size() == 2);
    CHECK(store.m_users.contains("jdoe"));
    CHECK(store.m_users.contains("ljohnson"));

    CHECK(store.m_metas.size() == 2);

    auto metas_it = store.m_metas.find(42);
    REQUIRE(metas_it != store.m_metas.end());
    CHECK(metas_it->second->rev == 42);
    CHECK(metas_it->second->timestamp == millis_t{1234567890});
    CHECK(metas_it->second->tx_type == 15);
    CHECK(metas_it->second->user == "jdoe");

    metas_it = store.m_metas.find(546);
    REQUIRE(metas_it != store.m_metas.end());
    CHECK(metas_it->second->rev == 546);
    CHECK(metas_it->second->timestamp == millis_t{1234567999});
    CHECK(metas_it->second->tx_type == 7);
    CHECK(metas_it->second->user == "ljohnson");

    CHECK(store.m_data.size() == 4);

    auto data_it = store.m_data.find("key1");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 42);
    CHECK(data_it->second->data() == gto::cstring{"a"});

    data_it = store.m_data.find("key2");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 42);
    CHECK(data_it->second->data() == gto::cstring{"b"});

    data_it = store.m_data.find("key5");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 546);
    CHECK(data_it->second->data() == gto::cstring{"e"});

    data_it = store.m_data.find("key6");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 546);
    CHECK(data_it->second->data() == gto::cstring{"f"});
}

TEST_CASE("store_load_empty")
{
    auto snapshot = make_snapshot(
        1024,
        {}      // no updates
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    store_t store;

    REQUIRE_NOTHROW(store.load(ptr));

    CHECK(store.m_rev == 1024);
    CHECK(store.m_users.size() == 0);
    CHECK(store.m_metas.size() == 0);
    CHECK(store.m_data.size() == 0);
}

TEST_CASE("store_load_nullptr")
{
    store_t store;

    REQUIRE_NOTHROW(store.load(nullptr));

    CHECK(store.m_rev == 0);
    CHECK(store.m_users.size() == 0);
    CHECK(store.m_metas.size() == 0);
    CHECK(store.m_data.size() == 0);
}

TEST_CASE("store_load_error_has_tx_rev_gt_rev")
{
    auto snapshot = make_snapshot(
        100,
        {
            make_update(42, "jdoe", 1234567890, 15,
                {
                    { .key = "key1", .value = {1, 2, 3}},
                    { .key = "key2", .value = {4, 5, 6}}
                },
                {}  // there are no deletes on snapshots
            ),
            make_update(546, "ljohnson", 1234567999, 7,
                {
                    { .key = "key5", .value = {7, 8}},
                    { .key = "key6", .value = {9}}
                },
                {}  // there are no deletes on snapshots
            )
        }
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    store_t store;

    CHECK_THROWS_AS(store.load(ptr), nplex_exception);
}

TEST_CASE("store_load_error_unsorted_tx")
{
    auto snapshot = make_snapshot(
        1024,
        {
            make_update(546, "jdoe", 1234567890, 15,
                {
                    { .key = "key1", .value = {1, 2, 3}},
                    { .key = "key2", .value = {4, 5, 6}}
                },
                {}  // there are no deletes on snapshots
            ),
            make_update(42, "ljohnson", 1234567999, 7,
                {
                    { .key = "key5", .value = {7, 8}},
                    { .key = "key6", .value = {9}}
                },
                {}  // there are no deletes on snapshots
            )
        }
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    store_t store;

    CHECK_THROWS_AS(store.load(ptr), nplex_exception);
}

TEST_CASE("store_load_error_invalid_key")
{
    auto snapshot = make_snapshot(
        1024,
        {
            make_update(546, "jdoe", 1234567890, 15,
                {
                    { .key = "", .value = {1, 2, 3}},
                },
                {}  // there are no deletes on snapshots
            )
        }
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    store_t store;

    CHECK_THROWS_AS(store.load(ptr), nplex_exception);
}

TEST_CASE("store_update")
{
    store_t store;
    meta_ptr meta = nullptr;
    std::vector<change_t> changes;
    flatbuffers::DetachedBuffer detached_buf;
    const nplex::msgs::Update *update_ptr = nullptr;

    auto snapshot = make_snapshot(
        1024,
        {
            make_update(42, "jdoe", 1234567890, 15,
                {
                    { .key = "key1", .value = {'a'}},
                    { .key = "key2", .value = {'b'}},
                    { .key = "key3", .value = {'c'}}
                },
                {}  // there are no deletes on snapshots
            )
        }
    );

    detached_buf = serialize(snapshot);
    auto *snapshot_ptr = ::GetRoot<nplex::msgs::Snapshot>(detached_buf.data());

    REQUIRE_NOTHROW(store.load(snapshot_ptr));

    // content = {key1, key2, key3}
    CHECK(store.m_rev == 1024);
    CHECK(store.m_data.size() == 3);

    auto update1 = make_update(1032, "ljohnson", 1234567891, 16,
        {
            { .key = "key2", .value = {'x'}},
            { .key = "key4", .value = {'y'}}
        },
        {
            "key1" // Delete key1
        }
    );

    detached_buf = serialize(update1);
    update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    REQUIRE_NOTHROW(std::tie(changes, meta) = store.update(update_ptr));

    REQUIRE(changes.size() == 3);
    CHECK(changes[0].action == change_t::action_e::UPDATE);
    CHECK(changes[0].key == "key2");
    CHECK(changes[0].old_value->rev() == 42);
    CHECK(changes[0].old_value->data() == gto::cstring("b"));
    CHECK(changes[0].new_value->rev() == 1032);
    CHECK(changes[0].new_value->data() == gto::cstring("x"));
    CHECK(changes[1].action == change_t::action_e::CREATE);
    CHECK(changes[1].key == "key4");
    CHECK(changes[1].new_value->rev() == 1032);
    CHECK(changes[1].new_value->data() == gto::cstring("y"));
    CHECK(changes[2].action == change_t::action_e::DELETE);
    CHECK(changes[2].key == "key1");
    CHECK(changes[2].old_value->rev() == 42);
    CHECK(changes[2].old_value->data() == gto::cstring("a"));

    // content = {key2, key3, key4}
    CHECK(store.m_rev == 1032);
    CHECK(store.m_data.size() == 3);
    CHECK(store.m_users.size() == 2);
    CHECK(store.m_metas.size() == 2);

    auto data_it = store.m_data.find("key1");
    CHECK(data_it == store.m_data.end());

    data_it = store.m_data.find("key2");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 1032);
    CHECK(data_it->second->data() == gto::cstring("x"));

    data_it = store.m_data.find("key3");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 42);
    CHECK(data_it->second->data() == gto::cstring("c"));

    data_it = store.m_data.find("key4");
    REQUIRE(data_it != store.m_data.end());
    CHECK(data_it->second->rev() == 1032);
    CHECK(data_it->second->data() == gto::cstring("y"));

    auto update2 = make_update(1033, "ljohnson", 1234567892, 14,
        {}, // no upserts
        {
            "key3" // Delete key3
        }
    );

    detached_buf = serialize(update2);
    update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    REQUIRE_NOTHROW(std::tie(changes, meta) = store.update(update_ptr));

    REQUIRE(changes.size() == 1);
    CHECK(changes[0].action == change_t::action_e::DELETE);
    CHECK(changes[0].key == "key3");
    CHECK(changes[0].old_value->rev() == 42);
    CHECK(changes[0].old_value->data() == gto::cstring("c"));

    // content = {key2, key4}
    CHECK(store.m_rev == 1033);
    CHECK(store.m_data.size() == 2);

    // we verify that unreferenced matedata and user was removed
    CHECK(store.m_users.size() == 1);
    CHECK(store.m_metas.size() == 1);

    CHECK(store.m_users.contains("ljohnson"));
    CHECK(store.m_data.find("key2") != store.m_data.end());
    CHECK(store.m_data.find("key4") != store.m_data.end());
}

TEST_CASE("store_update_no_changes")
{
    store_t store;
    meta_ptr meta = nullptr;
    std::vector<change_t> changes;

    auto update = make_update(1024, "ljohnson", 1234567891, 16,
        {},
        {}
    );

    auto detached_buf = serialize(update);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    store.m_rev = 42;

    REQUIRE_NOTHROW(std::tie(changes, meta) = store.update(update_ptr));

    CHECK(changes.empty());
    CHECK(store.m_rev == 1024);
}

TEST_CASE("store_update_empty_value")
{
    auto update = make_update(546, "jdoe", 1234567890, 15,
                {
                    { .key = "key1", .value = {}},
                },
                {}  // there are no deletes on snapshots
            );

    auto buf = serialize(update);
    auto *ptr = ::GetRoot<nplex::msgs::Update>(buf.data());
    std::vector<change_t> changes;
    meta_ptr meta = nullptr;
    store_t store;

    REQUIRE_NOTHROW(std::tie(changes, meta) = store.update(ptr));

    CHECK(changes.size() == 1);
    CHECK(changes[0].action == change_t::action_e::CREATE);
    CHECK(changes[0].key == "key1");
    CHECK(changes[0].new_value->rev() == 546);
    CHECK(changes[0].new_value->data() == gto::cstring{});
}

TEST_CASE("store_update_error_prev_rev")
{
    store_t store;

    auto update = make_update(10, "ljohnson", 1234567891, 16,
        {
            { .key = "key1", .value = {9, 9, 9}},
        },
        {}
    );

    auto detached_buf = serialize(update);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    store.m_rev = 42;

    // trying to commit a tx with rev less than current rev
    CHECK_THROWS(store.update(update_ptr));
}

TEST_CASE("store_update_invalid_key")
{
    store_t store;

    auto update = make_update(10, "ljohnson", 1234567891, 16,
        {
            { .key = "", .value = {9, 9, 9}},
        },
        {}
    );

    auto detached_buf = serialize(update);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    CHECK_THROWS(store.update(update_ptr));
}
