#include <iostream>
#include <memory>
#include <vector>
#include <type_traits>
#include <doctest.h>
#include "messages.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace flatbuffers;

namespace
{
    template <class T>
    std::vector<std::unique_ptr<T>> make_vector_unique_ptr(const std::vector<T>& input) {
        std::vector<std::unique_ptr<T>> result;
        result.reserve(input.size());
        for (const auto& item : input) {
            result.push_back(std::make_unique<T>(item));
        }
        return result;
    }

    TransactionT make_transaction(size_t rev, const char *user, uint64_t timestamp, uint32_t type, std::vector<KeyValueT> upserts = {}, std::vector<std::string> deletes = {})
    {
        TransactionT tx;
        tx.rev = rev;
        tx.user = user;
        tx.timestamp = timestamp;
        tx.type = type;
        tx.upserts = make_vector_unique_ptr(upserts);
        tx.deletes = deletes;
        return tx;
    }

}; // unnamed namespace


TEST_CASE("serialize_transaction")
{
    TransactionT tx = make_transaction(
        1,          // rev
        "jdoe",     // user
        1234567890, // timestamp
        1,          // type
        {           // upserts
            { .key="key1", .value = {1, 2, 3}},
            { .key="key2", .value = {4, 5, 6}}
        },
        {}          // deletes
    );

    flatbuffers::FlatBufferBuilder builder(1024);
    auto offset = Transaction::Pack(builder, &tx);
    builder.Finish(offset);

    uint8_t *buf = builder.GetBufferPointer();
    auto len = builder.GetSize();

    flatbuffers::Verifier verifier(buf, len);
    REQUIRE(verifier.VerifyBuffer<nplex::msgs::Transaction>());

    auto *tx_ptr = ::flatbuffers::GetRoot<nplex::msgs::Transaction>(buf);

    CHECK(tx_ptr->rev() == 1);
    CHECK(tx_ptr->user()->str() == "jdoe");
    CHECK(tx_ptr->timestamp() == 1234567890);
    CHECK(tx_ptr->type() == 1);

    REQUIRE(tx_ptr->upserts());
    REQUIRE(tx_ptr->upserts()->size() == tx.upserts.size());

    for (flatbuffers::uoffset_t i = 0; i < tx_ptr->upserts()->size(); i++)
    {
        auto kv = tx_ptr->upserts()->Get(i);

        CHECK(kv->key()->str() == tx.upserts[i]->key);
        CHECK(kv->value()->size() == tx.upserts[i]->value.size());

        for (flatbuffers::uoffset_t j = 0; j < kv->value()->size(); j++) {
            CHECK(kv->value()->Get(j) == tx.upserts[i]->value[j]);
        }
    }

    CHECK(!tx_ptr->deletes());
}
