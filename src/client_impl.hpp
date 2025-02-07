#pragma once

#include <set>
#include <vector>
#include <atomic>
#include <uv.h>
#include "nplex-cpp/client.hpp"
#include "transaction_impl.hpp"
#include "mqueue.hpp"
#include "cache.hpp"
#include "connection.hpp"
#include "client_internals.hpp"

namespace nplex {

/**
 * Internal class hidding client_t implementation details.
 * 
 * All methods in this class are executed in the event-loop.
 */
class client_t::impl_t
{
  private:

    using connection_ptr = std::unique_ptr<connection_t>;

    client_t &parent;                               //!< Parent client.
    std::vector<connection_ptr> connections;        //!< Server connections.
    connection_t *m_con = nullptr;                  //!< Current connection.
    std::size_t correlation = 0;                    //!< Last correlation id.
    bool can_force = false;                         //!< User can force transactions (set by server at login).
    std::atomic<client_t::state_e> m_state;         //!< Client state.

  public:

    std::string error;                              //!< Error message (empty if no error).
    std::unique_ptr<uv_loop_t> loop;                //!< Event loop.
    std::unique_ptr<uv_async_t> async;              //!< Signals that there are input commands.
    std::unique_ptr<uv_timer_t> timer;              //!< Connection-lost timer. 
    std::set<tx_impl_ptr, shared_ptr_less_t> transactions;  //!< List of current transactions.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    cache_ptr cache;                                //!< Database content.
    params_t params;                                //!< Client params.

    impl_t(client_t &parent_, const params_t &params_);
    ~impl_t();

    client_t::state_e state() const { return m_state; }

    void run() noexcept;
    void process_commands();
    void report_server_activity();

    void connect();
    void on_keepalive_timeout();
    void on_connection_established(connection_t *con);
    void on_connection_closed(connection_t *con);
    void on_msg_received(connection_t *con, const msgs::Message *msg);
    void on_msg_delivered(connection_t *con, const msgs::Message *msg);

  private:

    void abort(const std::string &msg);
    void close_timer();

    void process_submit_cmd(const nplex::submit_cmd_t &cmd);
    void process_close_cmd(const nplex::close_cmd_t &cmd);
    void process_ping_cmd(const nplex::ping_cmd_t &cmd);

    void process_login_resp(connection_t *con, const nplex::msgs::LoginResponse *resp);
    void process_load_resp(const nplex::msgs::LoadResponse *resp);
    void process_submit_resp(const nplex::msgs::SubmitResponse *resp);
    void process_update_push(const nplex::msgs::UpdatePush *resp);
    void process_keepalive_push(const nplex::msgs::KeepAlivePush *resp);
    void process_ping_resp(const nplex::msgs::PingResponse *resp);
};

} // namespace nplex
