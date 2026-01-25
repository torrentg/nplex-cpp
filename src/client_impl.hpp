#pragma once

#include <set>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <uv.h>
#include "nplex-cpp/client.hpp"
#include "transaction_impl.hpp"
#include "mqueue.hpp"
#include "cache.hpp"
#include "connection.hpp"
#include "client_internals.hpp"

namespace nplex {

struct acl_t
{
    std::uint8_t mode;      // Attributes (1=CREATE, 2=READ, 4=UPDATE, 8=DELETE).
    std::string pattern;    // Pattern (glob).
};

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
    std::uint32_t num_logins = 0;                   //!< Number of successful logins.
    bool can_force = false;                         //!< User can force transactions (set by server at login).
    std::vector<acl_t> permissions;                 //!< User permissions (set by server at login).

  public:

    std::mutex m_mutex;                             //!< Mutex to protect the state.
    std::condition_variable m_cv;                   //!< Condition variable to wait for changes in state.
    std::atomic<client_t::state_e> m_state;         //!< Client state.
    std::string error;                              //!< Error message (empty if no error).

    listener_t &listener;                           //!< Listener.

    std::unique_ptr<uv_loop_t> loop;                //!< Event loop.
    std::unique_ptr<uv_async_t> async;              //!< Signals that there are input commands.
    uv_timer_t *timer_keepalive = nullptr;          //!< Connection-lost timer.
    std::set<tx_impl_ptr, shared_ptr_less_t> transactions;  //!< List of current transactions.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    cache_ptr cache;                                //!< Database content.
    params_t params;                                //!< Client params.

    impl_t(const params_t &params_, listener_t &listener_, client_t &parent_);
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
    void set_state(client_t::state_e state);

    void process_submit_cmd(const submit_cmd_t &cmd);
    void process_close_cmd(const close_cmd_t &cmd);
    void process_ping_cmd(const ping_cmd_t &cmd);

    void process_login_resp(connection_t *con, const msgs::LoginResponse *resp);
    void process_load_resp(const msgs::LoadResponse *resp);
    void process_submit_resp(const msgs::SubmitResponse *resp);
    void process_changes_push(const msgs::ChangesPush *resp);
    void process_keepalive_push(const msgs::KeepAlivePush *resp);
    void process_ping_resp(const msgs::PingResponse *resp);
    void process_update(const msgs::Update *upd);
};

} // namespace nplex
