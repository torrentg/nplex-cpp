#pragma once

#include <set>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <exception>
#include <condition_variable>
#include <uv.h>
#include "cqueue.hpp"
#include "nplex-cpp/client.hpp"
#include "transaction_impl.hpp"
#include "cache.hpp"
#include "connection.hpp"
#include "client_internals.hpp"

namespace nplex {

struct acl_t
{
    std::uint8_t mode;          // Attributes (1=CREATE, 2=READ, 4=UPDATE, 8=DELETE).
    std::string pattern;        // Pattern (glob).
};

enum class client_state_e : std::uint8_t {
    STARTUP,                    //!< Initial connection + snapshot load (fail -> exception).
    CONNECTED,                  //!< Client is connected and operational.
    RECONNECTING,               //!< Connection lost and trying to reconnect.
    CLOSED                      //!< Client is closing or closed (event loop is terminated).
};

struct shared_ptr_less_t
{
    using is_transparent = std::true_type;

    template<typename T1, typename T2>
    bool operator()(const std::shared_ptr<T1> &lhs, const std::shared_ptr<T2> &rhs) const { 
        return (static_cast<void*>(lhs.get()) < static_cast<void *>(rhs.get()));
    }
};

struct submit_cmd_t {
    tx_impl_ptr tx;
    bool force = false;
};

struct close_cmd_t {
    // no members
};

struct ping_cmd_t {
    std::string payload;
};

using command_t = std::variant<submit_cmd_t, close_cmd_t, ping_cmd_t>;

/**
 * Internal class hiding client_t implementation details.
 * 
 * Most methods run on the event loop. 
 * Those that don’t are protected by a mutex. 
 * For the remaining ones, execution on the event loop is enforced with an assert.
 */
class client_t::impl_t
{
  public:

    std::set<tx_impl_ptr, shared_ptr_less_t> transactions;  //!< List of current transactions.
    cache_ptr cache;                                //!< Database content.

    impl_t(const params_t &params, rev_t rev0, listener_t &listener, client_t &parent);
    ~impl_t();

    bool is_connected() const { return (m_state == client_state_e::CONNECTED); } 
    bool is_closed() const { return (m_state == client_state_e::CLOSED); }
    const params_t & params() const { return m_params; }

    template<typename... Args>
    void log(listener_t::log_level_e severity, fmt::format_string<Args...> fmt_str, Args&&... args) {
        if (static_cast<int>(m_listener.log_level()) <= static_cast<int>(severity))
            m_listener.log(m_parent, severity, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void log_debug(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(listener_t::log_level_e::DEBUG, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_info(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(listener_t::log_level_e::INFO, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_warn(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(listener_t::log_level_e::WARN, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_error(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(listener_t::log_level_e::ERROR, fmt_str, std::forward<Args>(args)...);
    }

    void run() noexcept;
    void wait_for_startup();
    void process_commands();
    void report_server_activity();
    void abort(const std::string &msg);
    void push_command(const command_t &cmd);
    void try_to_connect();

    void on_connection_lost();
    void on_connection_established(connection_t *con);
    void on_connection_closed(connection_t *con);
    void on_msg_received(connection_t *con, const msgs::Message *msg);
    void on_msg_delivered(connection_t *con, const msgs::Message *msg);

  private:  // members

    client_t &m_parent;                             //!< Parent client.
    listener_t &m_listener;                         //!< Listener.
    params_t m_params;                              //!< Client params.
    rev_t m_rev0;                                   //!< Initial revision.

    std::vector<connection_ptr> m_connections;      //!< Server connections (maybe not established).
    connection_t *m_con = nullptr;                  //!< Current connection (established).
    std::size_t m_correlation = 0;                  //!< Last correlation id.
    std::size_t m_data_cid = 0;                     //!< Correlation id of last snapshot/updates sent.
    bool m_can_force = false;                       //!< User can force transactions (set by server at login).
    std::vector<acl_t> m_permissions;               //!< User permissions (set by server at login).

    std::mutex m_mutex;                             //!< Mutex to protect m_commands and m_async and m_cv.
    std::condition_variable m_cv;                   //!< Condition variable to wait for changes in state.
    std::atomic<client_state_e> m_state;            //!< Client state.
    std::exception_ptr m_error;                     //!< Stored exception (if any).
    gto::cqueue<command_t> m_commands;              //!< Async commands pending to be digested by the event loop.

    std::thread::id m_loop_thread_id;               //!< Thread id of the event loop thread.
    std::unique_ptr<uv_loop_t> m_loop;              //!< Event loop.
    std::unique_ptr<uv_async_t> m_async;            //!< Used to notify new pending commands
    std::unique_ptr<uv_signal_t> m_signal_sigint;   //!< SIGINT handler (Ctrl-C).
    std::unique_ptr<uv_timer_t> m_timer_con_lost;   //!< Connection-lost timer.
    std::unique_ptr<uv_timer_t> m_timer_reconnect;  //!< Reconnect timer.

  private:  // methods

    void set_state(client_state_e state);
    void send(flatbuffers::DetachedBuffer &&buf);
    void schedule_reconnect(std::uint32_t millis);

    void process_submit_cmd(const submit_cmd_t &cmd);
    void process_close_cmd(const close_cmd_t &cmd);
    void process_ping_cmd(const ping_cmd_t &cmd);

    void process_login_resp(connection_t *con, const msgs::LoginResponse *resp);
    void process_snapshot_resp(const msgs::SnapshotResponse *resp);
    void process_updates_resp(const msgs::UpdatesResponse *resp);
    void process_submit_resp(const msgs::SubmitResponse *resp);
    void process_updates_push(const msgs::UpdatesPush *resp);
    void process_keepalive_push(const msgs::KeepAlivePush *resp);
    void process_ping_resp(const msgs::PingResponse *resp);
    void process_update(const msgs::Update *upd);
};

} // namespace nplex
