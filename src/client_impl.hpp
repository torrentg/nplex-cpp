#pragma once

#include <set>
#include <vector>
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
#include "connection.hpp"
#include "client_internals.hpp"

namespace nplex {

/**
 * Internal class hidding client_t implementation details.
 */
struct client_t::impl_t
{
    using connection_ptr = std::unique_ptr<connection_t>;

    client_t &parent;                               //!< Parent client.
    std::vector<connection_ptr> connections;        //!< Server connections.
    connection_t *con = nullptr;                    //!< Current connection.
    std::size_t correlation = 0;                    //!< Last correlation id.
    params_t params;                                //!< Client params.
    std::mutex m_mutex;                             //!< Mutex to protect the client state.
    std::unique_ptr<uv_loop_t> loop;                //!< Event loop.
    std::unique_ptr<uv_async_t> async;              //!< Signals that there are input commands.
    std::thread thread_loop;                        //!< Event loop thread, process input commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    std::set<tx_impl_ptr, shared_ptr_less_t> transactions;  //!< List of current transactions.
    cache_ptr cache;                                //!< Database content.
    std::atomic<client_t::state_e> state;           //!< Client state.
    bool can_force = false;                         //!< User can force transactions (set by server at login).
    std::string error;                              //!< Error message (empty if no error).

    impl_t(client_t &parent_, const params_t &params_);
    ~impl_t();

    void run() noexcept;
    void process_commands();
    void process_recv_msg(const nplex::msgs::Message *msg);

    // TODO: args = con_ptr + cause (libuv rc) + print message
    void on_connection_established(const addr_t &addr);
    void on_connection_closed(const addr_t &addr);

  private:

    void close();
    void connect();
    void do_login();
    void disconnect();

    void process_submit_cmd(const nplex::submit_cmd_t &cmd);
    void process_close_cmd(const nplex::close_cmd_t &cmd);
    void process_ping_cmd(const nplex::ping_cmd_t &cmd);

    void process_login_resp(const nplex::msgs::LoginResponse *resp);
    void process_load_resp(const nplex::msgs::LoadResponse *resp);
    void process_submit_resp(const nplex::msgs::SubmitResponse *resp);
    void process_update_push(const nplex::msgs::UpdatePush *resp);
    void process_keepalive_push(const nplex::msgs::KeepAlivePush *resp);
    void process_ping_resp(const nplex::msgs::PingResponse *resp);
};

} // namespace nplex
