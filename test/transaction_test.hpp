#pragma once

#include <doctest.h>
#include "messages_test.hpp"
#include "exception.hpp"
#include "cache.hpp"
#include "transaction.hpp"

namespace  nplex {

    using cache_ptr = std::shared_ptr<cache_t>;

namespace tests {

    /**
     * Creates a basic cache initialized with some values.
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
     * @param[in] rev The cache revision.
     * 
     * @return Initialized cache.
     */

    inline cache_ptr make_basic_cache()
    {
        flatbuffers::DetachedBuffer buf;
        cache_ptr cache = std::make_shared<cache_t>();
        const msgs::Update *ptr = nullptr;

        auto transaction1 = make_update(1, "jdoe", 1234567890, 15,
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

        buf = serialize(transaction1);
        ptr = flatbuffers::GetRoot<nplex::msgs::Update>(buf.data());

        REQUIRE_NOTHROW(cache->update(ptr));
        CHECK(cache->m_rev == 1);

        auto transaction2 = make_update(2, "ljohnson", 1234567895, 7,
            {
                { .key = "key3", .value = {32}},
                { .key = "key4", .value = {42}},
                { .key = "key7", .value = {72}},
                { .key = "key8", .value = {82}}
            },
            { "key5", "key6" }
        );

        buf = serialize(transaction2);
        ptr = flatbuffers::GetRoot<nplex::msgs::Update>(buf.data());

        REQUIRE_NOTHROW(cache->update(ptr));
        CHECK(cache->m_rev == 2);

        return cache;
    }

}; // namespace test
}; // namespace nplex
