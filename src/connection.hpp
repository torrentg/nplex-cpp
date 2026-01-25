#pragma once

#include <uv.h>
#include <memory>
#include "addr.hpp"

// Forward declaration
namespace flatbuffers {
    class DetachedBuffer;
}

namespace nplex {

// Forward declarations
struct params_t;
class client_t;
class connection_t;

/**
 * Connection memory layout.
 * 
 * These are the connection_t private members of connection_t that are 
 * accessed by public libuv callbacks.
 * 
 * m_tcp.loop->data points to the client.
 */
struct connection_s
{
    uv_tcp_t m_tcp = {};                            // Libuv tcp handle (must be first)
    addr_t m_addr;                                  // Remote address (server address)
    char m_input_buffer[UINT16_MAX] = {0};          // Input buffer used by read()
    std::string m_input_msg;                        // Current incoming message
    bool m_is_connected = false;                    // Connection established
    int m_error = 0;                                // Disconnection cause

    struct {
        std::size_t max_unack_msgs = 0;             // Max unacknowledged messages
        std::size_t max_unack_bytes = 0;            // Max unacknowledged bytes
        std::size_t max_msg_bytes = 0;              // Max message size (input and output)
    } m_params;

    struct {
        std::size_t unack_msgs = 0;                 // Unacknowledged messages
        std::size_t unack_bytes = 0;                // Unacknowledged bytes
        std::size_t recv_msgs = 0;                  // Total received messages
        std::size_t recv_bytes = 0;                 // Total received bytes
        std::size_t sent_msgs = 0;                  // Total sent messages
        std::size_t sent_bytes = 0;                 // Total sent bytes
    } m_stats;

    connection_s(const addr_t &addr, uv_loop_t *loop, const params_t &params);
    ~connection_s();
    connection_s(const connection_s &) = delete;
    connection_s & operator=(const connection_s &) = delete;

    bool is_closed() const;

    void connect();
    void disconnect(int rc = 0);
    void send(flatbuffers::DetachedBuffer &&buf);

    client_t::impl_t * client() const { return reinterpret_cast<client_t::impl_t *>(m_tcp.loop->data); }
    connection_t * connection() { return reinterpret_cast<connection_t *>(this); }
};

/**
 * Class representing a network connection.
 * 
 * This class knows nothing about bussiness logic.
 * Deals with the event loop, messages, etc.
 * 
 * Main dutties are:
 * - Send messages
 * - Receive messages
 * - Handle disconnections
 * - Manage libuv resources
 */class connection_t : private connection_s
{
  public:

    const addr_t & addr() const { return m_addr; }
    bool is_connected() const { return m_is_connected; }
    int error() const { return m_error; }

    using connection_s::connection_s;
    using connection_s::connect;
    using connection_s::is_closed;
    using connection_s::disconnect;
    using connection_s::send;
};

using connection_ptr = std::unique_ptr<connection_t>;

} // namespace nplex
