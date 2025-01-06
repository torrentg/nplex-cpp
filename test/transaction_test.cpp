#include "transaction_test.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace nplex::tests;
using namespace flatbuffers;

TEST_CASE("transaction_test")
{
    cache_ptr cache = make_basic_cache();

    SUBCASE("read_committed")
    {
        auto tx = std::make_unique<transaction_t>(cache, transaction_t::isolation_e::READ_COMMITTED);

        CHECK(tx->isolation() == transaction_t::isolation_e::READ_COMMITTED);
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
        CHECK(!tx->read("key_unknow"));
    }
}
