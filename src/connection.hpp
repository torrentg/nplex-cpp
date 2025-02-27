#pragma once

#include <uv.h>
#include "client_impl.hpp"
#include "addr.hpp"

#define ERR_CLOSED_BY_LOCAL 1000
#define ERR_CLOSED_BY_PEER 1001
#define ERR_MSG_ERROR 1002
#define ERR_MSG_UNEXPECTED 1003
#define ERR_MSG_SIZE 1004
#define ERR_ALREADY_CONNECTED 1005
#define ERR_KEEPALIVE 1006
#define ERR_AUTH 1007

namespace nplex {

/**
 * Internal class representing a connection to a server.
 * 
 * client_impl is accessed via tcp.loop->data.
 */
struct connection_t
{
    enum class state_e : std::uint8_t {
        CLOSED,
        CONNECTING,
        CONNECTED
    };

    uv_tcp_t tcp;
    addr_t addr;
    state_e state;
    char input_buffer[UINT16_MAX] = {0};
    std::string input_msg;
    int error = 0;

    struct {
        std::uint32_t max_unack_msgs = 0;
        std::uint32_t max_unack_bytes = 0;
        std::uint32_t max_msg_bytes = 0;
    } params;

    struct {
        std::uint32_t unack_msgs = 0;
        std::uint32_t unack_bytes = 0;
        std::size_t recv_msgs = 0;
        std::size_t recv_bytes = 0;
        std::size_t sent_msgs = 0;
        std::size_t sent_bytes = 0;
    } stats;


    connection_t(const addr_t &addr_, uv_loop_t *loop_, const params_t &params_);
    ~connection_t();

    void connect();
    void disconnect(int rc = 0);
    void send(flatbuffers::DetachedBuffer &&buf);
    std::string strerror() const;

    client_t::impl_t * client() const { return (client_t::impl_t *) tcp.loop->data; }
};

} // namespace nplex
