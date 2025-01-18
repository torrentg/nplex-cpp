#pragma once

#include <set>
#include <array>
#include <mutex>
#include <thread>
#include <atomic>
#include <variant>
#include <uv.h>
#include "cqueue.hpp"
#include "nplex-cpp/client.hpp"
#include "transaction_impl.hpp"
#include "messages.hpp"
#include "mqueue.hpp"
#include "cache.hpp"
#include "addr.hpp"

namespace nplex {

using cache_ptr = std::shared_ptr<cache_t>;
using tx_ptr = std::shared_ptr<transaction_t>;

struct output_msg_t
{
    uv_write_t req;
    std::array<uv_buf_t, 4> buf;
    flatbuffers::DetachedBuffer content;
    std::uint32_t metadata;     // 0=none, 1=lz4 (big-endian)
    std::uint32_t checksum;     // CRC32 of len + metadata + content (big-endian)
    std::uint32_t len;          // Total message length (including len, metadata, content, checksum) (big-endian)

    output_msg_t(flatbuffers::DetachedBuffer &&content_);
};

struct connect_cmd_t {
    std::string server;      // host:port
};

struct load_cmd_t {
    msgs::LoadMode load_mode;
    rev_t rev;
};

struct submit_cmd_t {
    tx_ptr tx;
    bool force;
};

struct close_cmd_t {};

struct ping_cmd_t {
    std::string payload;
};

using command_t = std::variant<connect_cmd_t, load_cmd_t, submit_cmd_t, close_cmd_t, ping_cmd_t>;

struct client_impl_t
{
    client_t &parent;                               //!< Parent client.
    addr_t server_addr;                             //!< Server address.
    std::size_t correlation = 0;                    //!< Last correlation id.
    params_t params;                                //!< Client params.
    std::mutex m_mutex;                             //!< Mutex to protect the client state.
    std::unique_ptr<uv_loop_t> loop;                //!< Event loop.
    std::unique_ptr<uv_async_t> async;              //!< Signals that there are input commands.
    std::unique_ptr<uv_tcp_t> con;                  //!< Connection to the server.
    char input_buffer[UINT16_MAX];                  //!< Input buffer.
    std::thread thread_loop;                        //!< Event loop thread, process input commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    gto::cqueue<std::unique_ptr<output_msg_t>> msgs;
    std::set<tx_ptr> ongoing_tx;                    //!< List of ongoing transactions (user working on it).
    gto::cqueue<tx_ptr> pending_tx;                 //!< List of pending transactions (awaiting server response).
    cache_ptr cache;                                //!< Database content.
    std::atomic<client_t::state_e> state;           //!< Client state.
    bool can_force = false;                         //!< User can force transactions (given by server at login).
    std::string error;                              //!< Error message (empty if no error).

    client_impl_t(client_t &parent_, const params_t &params_);
    ~client_impl_t();

    void run() noexcept;
    void disconnect();
    void send(flatbuffers::DetachedBuffer &&buf);
    void connect(const nplex::connect_cmd_t &cmd);
    void load(const nplex::load_cmd_t &cmd);
    void submit(const nplex::submit_cmd_t &cmd);
    void close(const nplex::close_cmd_t &cmd);
    void ping(const nplex::ping_cmd_t &cmd);
};

} // namespace nplex
