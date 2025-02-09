#include <doctest.h>
#include "client_internals.hpp"

using namespace std;
using namespace nplex;
using namespace nplex::msgs;
using namespace flatbuffers;

namespace {

    uv_buf_t create_network_msg(const output_msg_t *msg)
    {
        uv_buf_t ret = uv_buf_init(nullptr, 0);

        ret.base = (char *) malloc(msg->length());
        ret.len = msg->length();

        char *ptr = ret.base;
        memcpy(ptr, &msg->len, sizeof(msg->len));
        ptr += sizeof(msg->len);
        memcpy(ptr, &msg->metadata, sizeof(msg->metadata));
        ptr += sizeof(msg->metadata);
        memcpy(ptr, msg->content.data(), msg->content.size());
        ptr += msg->content.size();
        memcpy(ptr, &msg->checksum, sizeof(msg->checksum));

        return ret;
    }

} // unnamed namespace

TEST_CASE("output_msg")
{
    output_msg_t msg(create_login_msg(1024, "jdoe", "password"));
    uv_buf_t network_msg = create_network_msg(&msg);
    auto parsed_msg = parse_network_msg(network_msg.base, network_msg.len);

    REQUIRE(parsed_msg != nullptr);
    CHECK(parsed_msg->content_type() == MsgContent::LOGIN_REQUEST);
    CHECK(parsed_msg->content_as_LOGIN_REQUEST()->cid() == 1024);
    CHECK(parsed_msg->content_as_LOGIN_REQUEST()->user()->str() == "jdoe");
    CHECK(parsed_msg->content_as_LOGIN_REQUEST()->password()->str() == "password");

    free(network_msg.base);
}
