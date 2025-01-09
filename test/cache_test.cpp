#include <doctest.h>
#include "exception.hpp"
#include "messages_test.hpp"
#include "cache.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

TEST_CASE("cache_restore")
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
    cache_t cache;

    REQUIRE_NOTHROW(cache.restore(ptr));

    CHECK(cache.m_rev == 1024);

    CHECK(cache.m_users.size() == 2);
    CHECK(cache.m_users.contains("jdoe"));
    CHECK(cache.m_users.contains("ljohnson"));

    CHECK(cache.m_metas.size() == 2);

    auto cache_it = cache.m_metas.find(42);
    REQUIRE(cache_it != cache.m_metas.end());
    CHECK(cache_it->second->rev == 42);
    CHECK(cache_it->second->timestamp == millis_t{1234567890});
    CHECK(cache_it->second->type == 15);
    CHECK(cache_it->second->user == "jdoe");

    cache_it = cache.m_metas.find(546);
    REQUIRE(cache_it != cache.m_metas.end());
    CHECK(cache_it->second->rev == 546);
    CHECK(cache_it->second->timestamp == millis_t{1234567999});
    CHECK(cache_it->second->type == 7);
    CHECK(cache_it->second->user == "ljohnson");

    CHECK(cache.m_data.size() == 4);

    auto data_it = cache.m_data.find("key1");
    REQUIRE(data_it != cache.m_data.end());
    CHECK(data_it->second->rev() == 42);
    CHECK(data_it->second->data() == gto::cstring{"a"});

    data_it = cache.m_data.find("key2");
    REQUIRE(data_it != cache.m_data.end());
    CHECK(data_it->second->rev() == 42);
    CHECK(data_it->second->data() == gto::cstring{"b"});

    data_it = cache.m_data.find("key5");
    REQUIRE(data_it != cache.m_data.end());
    CHECK(data_it->second->rev() == 546);
    CHECK(data_it->second->data() == gto::cstring{"e"});

    data_it = cache.m_data.find("key6");
    REQUIRE(data_it != cache.m_data.end());
    CHECK(data_it->second->rev() == 546);
    CHECK(data_it->second->data() == gto::cstring{"f"});
}

TEST_CASE("cache_restore_empty")
{
    auto snapshot = make_snapshot(
        1024,
        {}      // no updates
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    cache_t cache;

    REQUIRE_NOTHROW(cache.restore(ptr));

    CHECK(cache.m_rev == 1024);
    CHECK(cache.m_users.size() == 0);
    CHECK(cache.m_metas.size() == 0);
    CHECK(cache.m_data.size() == 0);
}

TEST_CASE("cache_restore_nullptr")
{
    cache_t cache;

    REQUIRE_NOTHROW(cache.restore(nullptr));

    CHECK(cache.m_rev == 0);
    CHECK(cache.m_users.size() == 0);
    CHECK(cache.m_metas.size() == 0);
    CHECK(cache.m_data.size() == 0);
}

TEST_CASE("cache_restore_error_has_tx_rev_gt_rev")
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
    cache_t cache;

    CHECK_THROWS_AS(cache.restore(ptr), nplex_exception);
}

TEST_CASE("cache_restore_error_unsorted_tx")
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
    cache_t cache;

    CHECK_THROWS_AS(cache.restore(ptr), nplex_exception);
}

TEST_CASE("cache_restore_error_invalid_key")
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
    cache_t cache;

    CHECK_THROWS_AS(cache.restore(ptr), nplex_exception);
}

TEST_CASE("cache_restore_error_invalid_value")
{
    auto snapshot = make_snapshot(
        1024,
        {
            make_update(546, "jdoe", 1234567890, 15,
                {
                    { .key = "key1", .value = {}},
                },
                {}  // there are no deletes on snapshots
            )
        }
    );

    auto buf = serialize(snapshot);
    auto *ptr = ::GetRoot<nplex::msgs::Snapshot>(buf.data());
    cache_t cache;

    CHECK_THROWS_AS(cache.restore(ptr), nplex_exception);
}

TEST_CASE("cache_update")
{
    cache_t cache;
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

    REQUIRE_NOTHROW(cache.restore(snapshot_ptr));

    // content = {key1, key2, key3}
    CHECK(cache.m_rev == 1024);
    CHECK(cache.m_data.size() == 3);

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

    REQUIRE_NOTHROW(changes = cache.update(update_ptr));

    REQUIRE(changes.size() == 3);
    CHECK(changes[0].action == change_t::action_e::UPDATE);
    CHECK(changes[0].key == "key2");
    CHECK(changes[0].old_value->rev() == 42);
    CHECK(changes[0].old_value->data() == gto::cstring("b"));
    CHECK(changes[0].value->rev() == 1032);
    CHECK(changes[0].value->data() == gto::cstring("x"));
    CHECK(changes[1].action == change_t::action_e::CREATE);
    CHECK(changes[1].key == "key4");
    CHECK(changes[1].value->rev() == 1032);
    CHECK(changes[1].value->data() == gto::cstring("y"));
    CHECK(changes[2].action == change_t::action_e::DELETE);
    CHECK(changes[2].key == "key1");
    CHECK(changes[2].old_value->rev() == 42);
    CHECK(changes[2].old_value->data() == gto::cstring("a"));

    // content = {key2, key3, key4}
    CHECK(cache.m_rev == 1032);
    CHECK(cache.m_data.size() == 3);
    CHECK(cache.m_users.size() == 2);
    CHECK(cache.m_metas.size() == 2);

    auto data_it = cache.m_data.find("key1");
    CHECK(data_it == cache.m_data.end());

    data_it = cache.m_data.find("key2");
    REQUIRE(data_it != cache.m_data.end());
    CHECK(data_it->second->rev() == 1032);
    CHECK(data_it->second->data() == gto::cstring("x"));

    data_it = cache.m_data.find("key3");
    REQUIRE(data_it != cache.m_data.end());
    CHECK(data_it->second->rev() == 42);
    CHECK(data_it->second->data() == gto::cstring("c"));

    data_it = cache.m_data.find("key4");
    REQUIRE(data_it != cache.m_data.end());
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

    REQUIRE_NOTHROW(changes = cache.update(update_ptr));

    REQUIRE(changes.size() == 1);
    CHECK(changes[0].action == change_t::action_e::DELETE);
    CHECK(changes[0].key == "key3");
    CHECK(changes[0].old_value->rev() == 42);
    CHECK(changes[0].old_value->data() == gto::cstring("c"));

    // content = {key2, key4}
    CHECK(cache.m_rev == 1033);
    CHECK(cache.m_data.size() == 2);

    // we verify that unreferenced matedata and user was removed
    CHECK(cache.m_users.size() == 1);
    CHECK(cache.m_metas.size() == 1);

    CHECK(cache.m_users.contains("ljohnson"));
    CHECK(cache.m_data.find("key2") != cache.m_data.end());
    CHECK(cache.m_data.find("key4") != cache.m_data.end());
}

TEST_CASE("cache_update_no_changes")
{
    cache_t cache;
    std::vector<change_t> changes;

    auto update = make_update(1024, "ljohnson", 1234567891, 16,
        {},
        {}
    );

    auto detached_buf = serialize(update);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    cache.m_rev = 42;

    REQUIRE_NOTHROW(changes = cache.update(update_ptr));

    CHECK(changes.empty());
    CHECK(cache.m_rev == 1024);
}

TEST_CASE("cache_update_error_prev_rev")
{
    cache_t cache;

    auto update = make_update(10, "ljohnson", 1234567891, 16,
        {
            { .key = "key1", .value = {9, 9, 9}},
        },
        {}
    );

    auto detached_buf = serialize(update);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    cache.m_rev = 42;

    // trying to commit a tx with rev less than current rev
    CHECK_THROWS(cache.update(update_ptr));
}

TEST_CASE("cache_update_invalid_key")
{
    cache_t cache;

    auto update = make_update(10, "ljohnson", 1234567891, 16,
        {
            { .key = "", .value = {9, 9, 9}},
        },
        {}
    );

    auto detached_buf = serialize(update);
    auto update_ptr = ::GetRoot<nplex::msgs::Update>(detached_buf.data());

    CHECK_THROWS(cache.update(update_ptr));
}
