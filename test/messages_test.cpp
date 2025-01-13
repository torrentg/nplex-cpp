#include <doctest.h>
#include "messages_test.hpp"
#include "messages.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

TEST_CASE("LoginRequest")
{
    SUBCASE("hard-way")
    {
        flatbuffers::FlatBufferBuilder builder;

        auto req = CreateLoginRequest(builder, 
            1, 
            builder.CreateString("jdoe"), 
            builder.CreateString("password"));

        builder.Finish(req);
        auto buf = builder.Release();

        auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
        CHECK(verifier.VerifyBuffer<LoginRequest>(nullptr));

        auto *ptr = ::GetRoot<nplex::msgs::LoginRequest>(buf.data());
        REQUIRE(ptr);
        CHECK(ptr->cid() == 1);
        CHECK(ptr->user()->str() == "jdoe");
        CHECK(ptr->password()->str() == "password");
    }

    SUBCASE("easy-way")
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
}

TEST_CASE("LoginResponse")
{
    LoginResponseT resp = {
        {},             // Native table
        1,              // cid
        LoginCode::AUTHORIZED,
        "3b12-7aac7",   // session
        456,            // rev0
        2048,           // crev
        false           // can_force
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
                make_update(42, "jdoe", 1234567890, 15,
                    {
                        { .key = "key1", .value = {1, 2, 3}},
                        { .key = "key2", .value = {4, 5, 6}}
                    },
                    {}  // there are no deletes on snapshots
                ),
                make_update(546, "ljohnson", 1234567999, 7,
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
    REQUIRE(ptr->snapshot()->updates());
    REQUIRE(ptr->snapshot()->updates()->size() == 2);

    for (uoffset_t i = 0; i < ptr->snapshot()->updates()->size(); i++)
    {
        auto tx = ptr->snapshot()->updates()->Get(i);

        CHECK(tx->rev() == resp.snapshot->updates[i]->rev);
        CHECK(tx->user()->str() == resp.snapshot->updates[i]->user);
        CHECK(tx->timestamp() == resp.snapshot->updates[i]->timestamp);
        CHECK(tx->type() == resp.snapshot->updates[i]->type);

        REQUIRE(tx->upserts());
        REQUIRE(tx->upserts()->size() == resp.snapshot->updates[i]->upserts.size());

        for (uoffset_t j = 0; j < tx->upserts()->size(); j++)
        {
            auto kv = tx->upserts()->Get(j);

            CHECK(kv->key()->str() == resp.snapshot->updates[i]->upserts[j]->key);
            CHECK(kv->value()->size() == resp.snapshot->updates[i]->upserts[j]->value.size());

            for (uoffset_t k = 0; k < kv->value()->size(); k++)
            {
                CHECK(kv->value()->Get(k) == resp.snapshot->updates[i]->upserts[j]->value[k]);
            }
        }

        CHECK(!tx->deletes());
    }
}

TEST_CASE("UpdatePush")
{
    UpdatePushT push = make_update_push(
        3,          // cid
        2048,       // crev
        make_update(1056, "akay", 1234568000, 4,
            {
                { .key = "key1", .value = {97, 98, 99}},
                { .key = "key6", .value = {10}}
            },
            {
                "key2"
            }
        )
    );

    auto buf = serialize(push);
    auto *ptr = ::GetRoot<nplex::msgs::UpdatePush>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->crev() == 2048);

    REQUIRE(ptr->update());
    CHECK(ptr->update()->rev() == push.update->rev);
    CHECK(ptr->update()->user()->str() == push.update->user);
    CHECK(ptr->update()->timestamp() == push.update->timestamp);
    CHECK(ptr->update()->type() == push.update->type);

    REQUIRE(ptr->update()->upserts());
    REQUIRE(ptr->update()->upserts()->size() == push.update->upserts.size());

    for (uoffset_t i = 0; i < ptr->update()->upserts()->size(); i++)
    {
        auto kv = ptr->update()->upserts()->Get(i);

        CHECK(kv->key()->str() == push.update->upserts[i]->key);
        CHECK(kv->value()->size() == push.update->upserts[i]->value.size());

        for (uoffset_t j = 0; j < kv->value()->size(); j++)
        {
            CHECK(kv->value()->Get(j) == push.update->upserts[i]->value[j]);
        }
    }

    REQUIRE(ptr->update()->deletes());
    REQUIRE(ptr->update()->deletes()->size() == push.update->deletes.size());

    for (uoffset_t i = 0; i < ptr->update()->deletes()->size(); i++)
    {
        CHECK(ptr->update()->deletes()->Get(i)->str() == push.update->deletes[i]);
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

    REQUIRE(ptr->ensures());
    REQUIRE(ptr->ensures()->size() == req.ensures.size());

    for (uoffset_t i = 0; i < ptr->ensures()->size(); i++)
    {
        CHECK(ptr->ensures()->Get(i)->pattern()->str() == req.ensures[i]->pattern);
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

TEST_CASE("KeepAlivePush")
{
    KeepAlivePushT req = {
        {},         // Native table
        2049        // crev
    };

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::KeepAlivePush>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->crev() == 2049);
}

TEST_CASE("Message")
{
    SUBCASE("hard-way")
    {
        flatbuffers::FlatBufferBuilder builder;

        auto msg = CreateMessage(builder, 
            MsgContent::LOGIN_REQUEST, 
            CreateLoginRequest(builder, 
                1, 
                builder.CreateString("jdoe"), 
                builder.CreateString("password")
            ).Union()
        );

        builder.Finish(msg);
        auto buf = builder.Release();

        auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
        CHECK(verifier.VerifyBuffer<Message>(nullptr));

        auto *ptr = ::GetRoot<nplex::msgs::Message>(buf.data());
        REQUIRE(ptr);
        CHECK(ptr->content_type() == MsgContent::LOGIN_REQUEST);

        auto *login = ptr->content_as_LOGIN_REQUEST();
        REQUIRE(login);
        CHECK(login->cid() == 1);
        CHECK(login->user()->str() == "jdoe");
        CHECK(login->password()->str() == "password");
    }

    SUBCASE("easy-way")
    {
        MessageT msg = make_message(
            LoginRequestT{
                {},         // Native table
                1,          // cid
                "jdoe",     // user
                "password"  // password
            });

        auto buf = serialize(msg);

        auto *ptr = ::GetRoot<nplex::msgs::Message>(buf.data());
        REQUIRE(ptr);
        CHECK(ptr->content_type() == MsgContent::LOGIN_REQUEST);

        auto *login = ptr->content_as_LOGIN_REQUEST();
        REQUIRE(login);
        CHECK(login->cid() == 1);
        CHECK(login->user()->str() == "jdoe");
        CHECK(login->password()->str() == "password");
    }
}
