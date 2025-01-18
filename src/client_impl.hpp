#pragma once

#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <uv.h>
#include "cqueue.hpp"
#include "nplex-cpp/client.hpp"
#include "transaction_impl.hpp"
#include "mqueue.hpp"
#include "cache.hpp"
#include "addr.hpp"
#include "client_internals.hpp"

namespace nplex {

/**
 * Internal class hidding client_t implementation details.
 */
struct client_t::impl_t
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
    std::set<tx_impl_ptr, shared_ptr_less_t> ongoing_tx;  //!< List of ongoing transactions (user working on it).
    gto::cqueue<tx_impl_ptr> pending_tx;            //!< List of pending transactions (awaiting server response).
    cache_ptr cache;                                //!< Database content.
    std::atomic<client_t::state_e> state;           //!< Client state.
    bool can_force = false;                         //!< User can force transactions (set by server at login).
    std::string error;                              //!< Error message (empty if no error).

    impl_t(client_t &parent_, const params_t &params_);
    ~impl_t();

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
