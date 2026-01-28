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
            10,
            builder.CreateString("jdoe"), 
            builder.CreateString("password"));

        builder.Finish(req);
        auto buf = builder.Release();

        auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
        CHECK(verifier.VerifyBuffer<LoginRequest>(nullptr));

        auto *ptr = ::GetRoot<nplex::msgs::LoginRequest>(buf.data());
        REQUIRE(ptr);
        CHECK(ptr->cid() == 1);
        CHECK(ptr->api_version() == 10);
        CHECK(ptr->user()->str() == "jdoe");
        CHECK(ptr->password()->str() == "password");
    }

    SUBCASE("easy-way")
    {
        LoginRequestT req = {
            {},         // Native table
            1,          // cid
            10,         // api-version
            "jdoe",     // user
            "password"  // password
        };

        auto buf = serialize(req);
        auto *ptr = ::GetRoot<nplex::msgs::LoginRequest>(buf.data());

        REQUIRE(ptr);
        CHECK(ptr->cid() == 1);
        CHECK(ptr->api_version() == 10);
        CHECK(ptr->user()->str() == "jdoe");
        CHECK(ptr->password()->str() == "password");
    }
}

TEST_CASE("LoginResponse")
{
    LoginResponseT resp;

    resp.cid = 1;
    resp.code = LoginCode::AUTHORIZED;
    resp.rev0 = 456;
    resp.crev = 2048;
    resp.can_force = false;
    resp.keepalive = 3000;
    resp.permissions.push_back(std::make_unique<msgs::AclT>(AclT{{}, "/devices/**"s, 15}));
    resp.permissions.push_back(std::make_unique<msgs::AclT>(AclT{{}, "/user/jdoe/**"s, 6}));
    resp.permissions.push_back(std::make_unique<msgs::AclT>(AclT{{}, "/alarms/**"s, 7}));

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::LoginResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 1);
    CHECK(ptr->code() == LoginCode::AUTHORIZED);
    CHECK(ptr->rev0() == 456);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->can_force() == false);
    CHECK(ptr->keepalive() == 3000);
    CHECK(ptr->permissions());
    CHECK(ptr->permissions()->size() == 3);
    for (uoffset_t i = 0; i < ptr->permissions()->size(); i++) {
        auto perm = ptr->permissions()->Get(i);
        CHECK(perm->pattern()->str() == resp.permissions[i]->pattern);
        CHECK(perm->mode() == resp.permissions[i]->mode);
    }
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

TEST_CASE("SnapshotRequest")
{
    SnapshotRequestT req = {
        {},         // Native table
        3,          // cid
        1024        // rev
    };

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::SnapshotRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->rev() == 1024);
}

TEST_CASE("SnapshotResponse")
{
    SnapshotResponseT resp = make_snapshot_resp(
        3,          // cid
        2048,       // crev
        24,         // rev0
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
    auto *ptr = ::GetRoot<nplex::msgs::SnapshotResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->rev0() == 24);
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

TEST_CASE("UpdatesRequest")
{
    UpdatesRequestT req = {
        {},         // Native table
        4,          // cid
        1500        // rev
    };

    auto buf = serialize(req);
    auto *ptr = ::GetRoot<nplex::msgs::UpdatesRequest>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 4);
    CHECK(ptr->rev() == 1500);
}

TEST_CASE("UpdatesResponse")
{
    UpdatesResponseT resp = {
        {},         // Native table
        4,          // cid
        2048,       // crev
        24,         // rev0
        true        // accepted
    };

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::UpdatesResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 4);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->rev0() == 24);
    CHECK(ptr->accepted() == true);
}

TEST_CASE("UpdatesPush")
{
    UpdatesPushT push = make_updates_push(
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
    auto *ptr = ::GetRoot<nplex::msgs::UpdatesPush>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 3);
    CHECK(ptr->crev() == 2048);

    REQUIRE(ptr->updates());
    CHECK(ptr->updates()->size() == 1);

    auto update = ptr->updates()->Get(0);
    CHECK(update->rev() == push.updates[0]->rev);
    CHECK(update->user()->str() == push.updates[0]->user);
    CHECK(update->timestamp() == push.updates[0]->timestamp);
    CHECK(update->type() == push.updates[0]->type);

    REQUIRE(update->upserts());
    REQUIRE(update->upserts()->size() == push.updates[0]->upserts.size());

    for (uoffset_t i = 0; i < update->upserts()->size(); i++)
    {
        auto kv = update->upserts()->Get(i);

        CHECK(kv->key()->str() == push.updates[0]->upserts[i]->key);
        CHECK(kv->value()->size() == push.updates[0]->upserts[i]->value.size());

        for (uoffset_t j = 0; j < kv->value()->size(); j++)
        {
            CHECK(kv->value()->Get(j) == push.updates[0]->upserts[i]->value[j]);
        }
    }

    REQUIRE(update->deletes());
    REQUIRE(update->deletes()->size() == push.updates[0]->deletes.size());

    for (uoffset_t i = 0; i < update->deletes()->size(); i++)
    {
        CHECK(update->deletes()->Get(i)->str() == push.updates[0]->deletes[i]);
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
            "/devices/*",
            "/user/**"
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
        CHECK(ptr->ensures()->Get(i)->str() == req.ensures[i]);
    }
}

TEST_CASE("SubmitRequest-hard")
{
    flatbuffers::FlatBufferBuilder builder;
    const char data1[] = {1, 2, 3};
    const char data2[] = {4, 5, 6};

    // create vector of upserts
    std::vector<flatbuffers::Offset<msgs::KeyValue>> upserts;
    upserts.push_back(
        CreateKeyValue(
            builder, 
            builder.CreateString("key1"), 
            builder.CreateVector((uint8_t *) data1, sizeof(data1))
        )
    );
    upserts.push_back(
        CreateKeyValue(
            builder, 
            builder.CreateString("key2"), 
            builder.CreateVector((uint8_t *) data2, sizeof(data2))
        )
    );

    // create vector of deletes
    std::vector<flatbuffers::Offset<flatbuffers::String>> deletes;
    deletes.push_back(builder.CreateString("key3"));
    deletes.push_back(builder.CreateString("key4"));

    // create vector of ensures
    std::vector<flatbuffers::Offset<flatbuffers::String>> ensures;
    ensures.push_back(builder.CreateString("/devices/*"));
    ensures.push_back(builder.CreateString("/user/**"));

    auto msg = CreateMessage(builder, 
        MsgContent::SUBMIT_REQUEST, 
        CreateSubmitRequest(builder, 
            4,          // cid
            2048,       // crev
            1,          // type
            builder.CreateVector(upserts),
            builder.CreateVector(deletes),
            builder.CreateVector(ensures),
            true
        ).Union()
    );

    builder.Finish(msg);
    auto buf = builder.Release();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    CHECK(verifier.VerifyBuffer<Message>(nullptr));

    auto *ptr = ::GetRoot<nplex::msgs::Message>(buf.data());
    REQUIRE(ptr);
    CHECK(ptr->content_type() == MsgContent::SUBMIT_REQUEST);

    auto *submit = ptr->content_as_SUBMIT_REQUEST();
    REQUIRE(submit);
    CHECK(submit->cid() == 4);
    CHECK(submit->crev() == 2048);
    CHECK(submit->type() == 1);
    REQUIRE(submit->upserts());
    CHECK(submit->upserts()->size() == 2);
    REQUIRE(submit->deletes());
    CHECK(submit->deletes()->size() == 2);
    REQUIRE(submit->ensures());
    CHECK(submit->ensures()->size() == 2);
    CHECK(submit->force());
}

TEST_CASE("SubmitResponse")
{
    SubmitResponseT resp = {
        {},         // Native table
        4,          // cid
        2048,       // crev
        msgs::SubmitCode::ACCEPTED,
        2049,       // erev
    };

    auto buf = serialize(resp);
    auto *ptr = ::GetRoot<nplex::msgs::SubmitResponse>(buf.data());

    REQUIRE(ptr);
    CHECK(ptr->cid() == 4);
    CHECK(ptr->crev() == 2048);
    CHECK(ptr->code() == msgs::SubmitCode::ACCEPTED);
    CHECK(ptr->erev() == 2049);
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
                10, 
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
        CHECK(login->api_version() == 10);
        CHECK(login->user()->str() == "jdoe");
        CHECK(login->password()->str() == "password");
    }

    SUBCASE("easy-way")
    {
        MessageT msg = make_message(
            LoginRequestT{
                {},         // Native table
                1,          // cid
                10,         // api-version
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
        CHECK(login->api_version() == 10);
        CHECK(login->user()->str() == "jdoe");
        CHECK(login->password()->str() == "password");
    }
}
