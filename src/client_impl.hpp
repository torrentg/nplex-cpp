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
#include "store.hpp"
#include "connection.hpp"
#include "messaging.hpp"
#include "params.hpp"

namespace nplex {

struct acl_t
{
    std::uint8_t mode;          // Attributes (1=CREATE, 2=READ, 4=UPDATE, 8=DELETE).
    std::string pattern;        // Pattern (glob).
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
 * Client implementation.
 * 
 * Most methods run on the event loop. Those that don’t are protected by a mutex. 
 * For the remaining ones, execution on the event loop is enforced with an assert.
 */
class client_impl final : public client
{
  public:  // types

    enum state_e : std::uint8_t {
        OFFLINE,                //!< Not connected to any server.
        CONNECTING,             //!< Trying to connect to a server.
        AUTHENTICATED,          //!< Successfully authenticated in a server.
        LOADING_SNAPSHOT,       //!< Loading snapshot from server.
        INITIALIZED,            //!< Snapshot loaded at initial revision.
        SYNCING,                //!< Syncing with server (receiving updates from initial rev to current rev).
        SYNCED,                 //!< Synced with server, waiting for new updates.
        CLOSED                  //!< Database not available.
    };

  public:  // methods

    client_impl(const client_params_t &params);
    ~client_impl() override;

    client & set_logger(const std::shared_ptr<logger> &log) override;
    client & set_reactor(const std::shared_ptr<reactor> &rct) override;
    client & set_manager(const std::shared_ptr<manager> &mngr) override;
    client & set_initial_rev(rev_t rev) override;

    bool is_usable() const override { return m_initialized.load(); }
    bool is_synced() const override { return m_state.load() == state_e::SYNCED; }
    bool is_closed() const override { return m_state.load() == state_e::CLOSED; }
    bool is_running() const { return m_loop_thread_id != std::thread::id{}; }

    void run(std::stop_token st) noexcept override;
    bool wait_for_usable(millis timeout = millis::max()) override;
    bool wait_for_synced(millis timeout = millis::max()) override;
    tx_ptr create_tx(transaction::isolation_e isolation, bool read_only) override;
    void close() override { push_command(close_cmd_t{}); }

    void process_commands();
    void report_server_activity();
    void abort(const std::string &msg);
    void push_command(const command_t &cmd) noexcept;
    void try_to_connect();

    void on_connection_lost();
    void on_connection_established(connection *con);
    void on_connection_closed(connection *con);
    void on_msg_received(connection *con, const msgs::Message *msg);
    void on_msg_delivered(connection *con, const msgs::Message *msg);

  protected:  // methods

    template<typename... Args>
    void log(logger::log_level_e severity, fmt::format_string<Args...> fmt_str, Args&&... args) {
        if (m_logger && static_cast<int>(m_logger->log_level()) <= static_cast<int>(severity))
            m_logger->log(*this, severity, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void log_trace(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::TRACE, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_debug(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::DEBUG, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_info(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::INFO, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_warn(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::WARN, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_error(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::ERROR, fmt_str, std::forward<Args>(args)...);
    }

  private:  // members

    store_ptr m_store;                              //!< Database content.
    std::set<tx_impl_ptr> m_transactions;           //!< List of current transactions.
    client_params_t m_params;                       //!< Client params.
    rev_t m_rev0 = 0;                               //!< Initial revision.
    
    std::shared_ptr<logger> m_logger;               //!< Client logger.
    std::shared_ptr<reactor> m_reactor;             //!< Client reactor.
    std::shared_ptr<manager> m_manager;             //!< Lifecycle manager.

    std::vector<connection_ptr> m_connections;      //!< Server connections (maybe not established).
    connection *m_con = nullptr;                    //!< Current connection (established).
    std::size_t m_correlation = 0;                  //!< Last correlation id.
    std::size_t m_data_cid = 0;                     //!< Correlation id of last snapshot/updates sent.
    bool m_can_force = false;                       //!< User can force transactions (set by server at login).
    std::vector<acl_t> m_permissions;               //!< User permissions (set by server at login).

    std::mutex m_mutex;                             //!< Mutex to protect m_commands and m_async and m_cv.
    std::atomic<bool> m_initialized = false;        //!< Data was initialized with a snapshot.
    std::condition_variable m_cv;                   //!< Condition variable to wait for changes in state.
    std::atomic<state_e> m_state;                   //!< Client state.
    std::exception_ptr m_error;                     //!< Stored exception (if any).
    gto::cqueue<command_t> m_commands;              //!< Async commands pending to be digested by the event loop.

    std::thread::id m_loop_thread_id;               //!< Thread id of the event loop thread.
    std::unique_ptr<uv_loop_t> m_loop;              //!< Event loop.
    std::unique_ptr<uv_async_t> m_async_command;    //!< Used to notify new pending commands
    std::unique_ptr<uv_signal_t> m_signal_sigint;   //!< SIGINT handler (Ctrl-C).
    std::unique_ptr<uv_timer_t> m_timer_con_lost;   //!< Connection-lost timer.
    std::unique_ptr<uv_timer_t> m_timer_reconnect;  //!< Reconnect timer.

  private:  // methods

    void set_state(state_e state);
    void send(flatbuffers::DetachedBuffer &&buf);
    void schedule_reconnect(std::uint32_t delay_ms);

    void process_submit_cmd(const submit_cmd_t &cmd);
    void process_close_cmd(const close_cmd_t &cmd);
    void process_ping_cmd(const ping_cmd_t &cmd);

    void process_login_resp(connection *con, const msgs::LoginResponse *resp);
    void process_snapshot_resp(const msgs::SnapshotResponse *resp);
    void process_updates_resp(const msgs::UpdatesResponse *resp);
    void process_submit_resp(const msgs::SubmitResponse *resp);
    void process_updates_push(const msgs::UpdatesPush *resp);
    void process_keepalive_push(const msgs::KeepAlivePush *resp);
    void process_ping_resp(const msgs::PingResponse *resp);
    void process_update(const msgs::Update *upd);
};

} // namespace nplex
