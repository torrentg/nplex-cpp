#include <memory>
#include <vector>
#include <type_traits>
#include <doctest.h>
#include "test_utils.hpp"
#include "messages.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

TEST_CASE("LoginRequest")
{
    LoginRequestT req = {
        {},         // Native table
        1,          // cid
        "jdoe",     // user
        "password"  // password
    };
    
    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::LoginRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 1);
    CHECK(ptr->user()->str() == "jdoe");
    CHECK(ptr->password()->str() == "password");
}

TEST_CASE("LoginResponse")
{
    LoginResponseT resp = {
        {},             // Native table
        1,              // cid
        LoginCode::AUTHORIZED,
        "3b12-7aac7",   // session
        456,            // rev0
        2048            // crev
    };

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::LoginResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 1);
    CHECK(ptr->code() == LoginCode::AUTHORIZED);
    CHECK(ptr->session()->str() == "3b12-7aac7");
    CHECK(ptr->rev0() == 456);
    CHECK(ptr->crev() == 2048);
}

TEST_CASE("PingRequest")
{
    PingRequestT req = {
        {},         // Native table
        2,          // cid
        "payload"   // payload
    };

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::PingRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 2);
    CHECK(ptr->payload()->str() == "payload");
}

TEST_CASE("PingResponse")
{
    PingResponseT resp = {
        {},         // Native table
        2,          // cid
        2048,       // crev
        "payload"   // payload
    };

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::PingResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 2);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->payload()->str() == "payload");
}   

TEST_CASE("LoadRequest")
{
    LoadRequestT req = {
        {},         // Native table
        3,          // cid
        LoadMode::SNAPSHOT_AT_FIXED_REV,
        1024        // rev
    };

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::LoadRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->mode() == LoadMode::SNAPSHOT_AT_FIXED_REV);
    CHECK(ptr->rev() == 1024);
}

TEST_CASE("LoadResponse")
{
    LoadResponseT resp = make_load_response(
        3,          // cid
        2048,       // crev
        true,       // accepted
        make_snapshot(
            1024,
            {
                make_transaction(42, "jdoe", 1234567890, 15,
                    {
                        { .key = "key1", .value = {1, 2, 3}},
                        { .key = "key2", .value = {4, 5, 6}}
                    },
                    {}  // there are no deletes on snapshots
                ),
                make_transaction(546, "jdoe", 1234567999, 7,
                    {
                        { .key = "key5", .value = {1, 2, 3}},
                        { .key = "key6", .value = {4, 5, 6}}
                    },
                    {}  // there are no deletes on snapshots
                )
            }
        )
    );

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::LoadResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->accepted() == true);

    REQUIRE(ptr->snapshot());
    CHECK(ptr->snapshot()->rev() == 1024);
    REQUIRE(ptr->snapshot()->transactions());
    REQUIRE(ptr->snapshot()->transactions()->size() == 2);

    for (uoffset_t i = 0; i < ptr->snapshot()->transactions()->size(); i++)
    {
        auto tx = ptr->snapshot()->transactions()->Get(i);

        CHECK(tx->rev() == resp.snapshot->transactions[i]->rev);
        CHECK(tx->user()->str() == resp.snapshot->transactions[i]->user);
        CHECK(tx->timestamp() == resp.snapshot->transactions[i]->timestamp);
        CHECK(tx->type() == resp.snapshot->transactions[i]->type);

        REQUIRE(tx->upserts());
        REQUIRE(tx->upserts()->size() == resp.snapshot->transactions[i]->upserts.size());

        for (uoffset_t j = 0; j < tx->upserts()->size(); j++)
        {
            auto kv = tx->upserts()->Get(j);

            CHECK(kv->key()->str() == resp.snapshot->transactions[i]->upserts[j]->key);
            CHECK(kv->value()->size() == resp.snapshot->transactions[i]->upserts[j]->value.size());

            for (uoffset_t k = 0; k < kv->value()->size(); k++)
            {
                CHECK(kv->value()->Get(k) == resp.snapshot->transactions[i]->upserts[j]->value[k]);
            }
        }

        CHECK(!tx->deletes());
    }
}

TEST_CASE("CommitResponse")
{
    CommitResponseT resp = make_commit(
        3,          // cid
        2048,       // crev
        make_transaction(1056, "akay", 1234568000, 4,
            {
                { .key = "key1", .value = {97, 98, 99}},
                { .key = "key6", .value = {10}}
            },
            {
                "key2"
            }
        )
    );

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::CommitResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->crev() == 2048);

    REQUIRE(ptr->transaction());
    CHECK(ptr->transaction()->rev() == resp.transaction->rev);
    CHECK(ptr->transaction()->user()->str() == resp.transaction->user);
    CHECK(ptr->transaction()->timestamp() == resp.transaction->timestamp);
    CHECK(ptr->transaction()->type() == resp.transaction->type);

    REQUIRE(ptr->transaction()->upserts());
    REQUIRE(ptr->transaction()->upserts()->size() == resp.transaction->upserts.size());

    for (uoffset_t i = 0; i < ptr->transaction()->upserts()->size(); i++)
    {
        auto kv = ptr->transaction()->upserts()->Get(i);

        CHECK(kv->key()->str() == resp.transaction->upserts[i]->key);
        CHECK(kv->value()->size() == resp.transaction->upserts[i]->value.size());

        for (uoffset_t j = 0; j < kv->value()->size(); j++)
        {
            CHECK(kv->value()->Get(j) == resp.transaction->upserts[i]->value[j]);
        }
    }

    REQUIRE(ptr->transaction()->deletes());
    REQUIRE(ptr->transaction()->deletes()->size() == resp.transaction->deletes.size());

    for (uoffset_t i = 0; i < ptr->transaction()->deletes()->size(); i++)
    {
        CHECK(ptr->transaction()->deletes()->Get(i)->str() == resp.transaction->deletes[i]);
    }
}

TEST_CASE("SubmitRequest")
{
    SubmitRequestT req = make_submit_request(
        4,          // cid
        2048,       // crev
        1,          // type
        {
            { .key = "key1", .value = {1, 2, 3}},
            { .key = "key2", .value = {4, 5, 6}}
        },
        {
            "key3",
            "key4"
        },
        {
            { .pattern = "/devices/*", .mode = 1},
            { .pattern = "/user/**", .mode = 7}
        }
    );

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::SubmitRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 4);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->type() == 1);

    REQUIRE(ptr->upserts());
    REQUIRE(ptr->upserts()->size() == req.upserts.size());

    for (uoffset_t i = 0; i < ptr->upserts()->size(); i++)
    {
        auto kv = ptr->upserts()->Get(i);

        CHECK(kv->key()->str() == req.upserts[i]->key);
        CHECK(kv->value()->size() == req.upserts[i]->value.size());

        for (uoffset_t j = 0; j < kv->value()->size(); j++)
        {
            CHECK(kv->value()->Get(j) == req.upserts[i]->value[j]);
        }
    }

    REQUIRE(ptr->deletes());
    REQUIRE(ptr->deletes()->size() == req.deletes.size());

    for (uoffset_t i = 0; i < ptr->deletes()->size(); i++)
    {
        CHECK(ptr->deletes()->Get(i)->str() == req.deletes[i]);
    }

    REQUIRE(ptr->checks());
    REQUIRE(ptr->checks()->size() == req.checks.size());

    for (uoffset_t i = 0; i < ptr->checks()->size(); i++)
    {
        CHECK(ptr->checks()->Get(i)->pattern()->str() == req.checks[i]->pattern);
    }
}

TEST_CASE("SubmitResponse")
{
    SubmitResponseT resp = {
        {},         // Native table
        4,          // cid
        2048,       // crev
        2049,       // erev
        ""          // error
    };

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::SubmitResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 4);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->erev() == 2049);
    CHECK(!ptr->error());
}

TEST_CASE("KeepAlive")
{
    KeepAliveRequestT req = {
        {},         // Native table
        2049        // crev
    };

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::KeepAliveRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->crev() == 2049);
}
