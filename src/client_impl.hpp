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
#include "connection.hpp"
#include "loggable.hpp"
#include "params.hpp"

namespace nplex {

// Forward declarations
struct store_t;
struct user_t;

using store_ptr = std::shared_ptr<store_t>;
using user_ptr = std::shared_ptr<user_t>;
using const_user_ptr = std::shared_ptr<const user_t>;
using clock = std::chrono::steady_clock;
using usec = std::chrono::microseconds;

struct command_t {
    virtual ~command_t() = default;
};

struct request_t : public command_t {
    virtual ~request_t() override = default;
    virtual void cancel() = 0;
    std::size_t cid = 0;
    clock::time_point t0 = clock::now();
};

struct close_cmd_t : public command_t {
    virtual ~close_cmd_t() override = default;
};

struct submit_req_t : public request_t {
    submit_req_t(const tx_impl_ptr &t, bool f) : tx(t), force(f) {}
    virtual ~submit_req_t() override = default;
    void cancel() override {
        tx->set_submit_result(std::make_exception_ptr(nplex_exception("request canceled")));
    }
    tx_impl_ptr tx;
    bool force = false;
    rev_t erev = 0;
};

struct ping_req_t : public request_t {
    ping_req_t(const std::string &str) : payload(str) {}
    virtual ~ping_req_t() override = default;
    void cancel() override {
        promise.set_exception(std::make_exception_ptr(nplex_exception("request canceled")));
    }
    std::string payload;
    std::promise<usec> promise;
};

struct sessions_req_t : public request_t {
    sessions_req_t(bool stream) : enable_stream(stream) {}
    virtual ~sessions_req_t() override = default;
    void cancel() override {
        promise.set_exception(std::make_exception_ptr(nplex_exception("request canceled")));
    }
    bool enable_stream = false;
    std::promise<std::vector<session_t>> promise;
};

struct tx_comparator
{
    using is_transparent = std::true_type;
    
    template<typename T, typename U>
    bool operator()(const T &a, const U &b) const
    {
        auto get_ptr = [](const auto &x) -> const transaction_impl* {
            if constexpr (std::is_pointer_v<std::decay_t<decltype(x)>>) {
                return x;
            } else {
                return x.get();
            }
        };

        return get_ptr(a) < get_ptr(b);
    }
};

using command_ptr = std::unique_ptr<command_t>;
using request_ptr = std::unique_ptr<request_t>;
using submit_ptr = std::unique_ptr<submit_req_t>;

/**
 * Client implementation.
 * 
 * Most methods run on the event loop. Those that don’t are protected by a mutex. 
 * For the remaining ones, execution on the event loop is enforced with an assert.
 */
class client_impl final : public client, public loggable, public std::enable_shared_from_this<client_impl>
{
  public:  // types

    enum class state_e : std::uint8_t {
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

    bool is_populated() const override { return m_initialized.load(); }
    bool is_synced() const override { return m_state.load() == state_e::SYNCED; }
    bool is_closed() const override { return m_state.load() == state_e::CLOSED; }
    bool is_running() const { return m_running.load(); }

    const_user_ptr user() const { return m_user.load(); }
    logger_ptr logger() const { return m_logger; }
    store_ptr store() const { return m_store; }

    void run(std::stop_token st) noexcept override;
    bool wait_for_populated(millis_t timeout = millis_t::max()) override;
    bool wait_for_synced(millis_t timeout = millis_t::max()) override;
    tx_ptr create_tx(transaction::isolation_e isolation, bool read_only) override;
    std::future<std::vector<session_t>> fetch_sessions(bool enable_stream) override;
    std::future<usec> ping(const std::string &payload) override;
    void close() override { push_command(std::make_unique<close_cmd_t>()); }

    // submits command to the event loop thread
    void push_command(command_ptr &&cmd);

    // used by connections and libuv callbacks
    void on_connection_lost();
    void on_connection_established(connection *con);
    void on_connection_closed(connection *con);
    void on_msg_received(connection *con, const msgs::Message *msg);
    void on_msg_delivered(connection *con, const msgs::Message *msg);
    void abort(const std::string &msg);
    void report_server_activity();
    void process_commands();
    void try_to_connect();

  private:  // members

    using set_tx_t = std::set<tx_impl_ptr, tx_comparator>;

    std::atomic<user_ptr> m_user;                   //!< User data.
    store_ptr m_store;                              //!< Database content.
    set_tx_t m_transactions;                        //!< List of current transactions.
    client_params_t m_params;                       //!< Client params.
    rev_t m_rev0 = 0;                               //!< Initial revision.
    
    std::shared_ptr<reactor> m_reactor;             //!< Client reactor.
    std::shared_ptr<manager> m_manager;             //!< Lifecycle manager.

    std::vector<connection_ptr> m_connections;      //!< Server connections (maybe not established).
    connection *m_con = nullptr;                    //!< Current connection (established).
    std::size_t m_correlation = 0;                  //!< Last correlation id.
    std::size_t m_data_cid = 0;                     //!< Correlation id of last snapshot/updates sent.
    std::size_t m_sessions_cid = 0;                 //!< Correlation id of last sessions stream request (0 = disabled).

    std::mutex m_mutex_commands;                    //!< Mutex to protect m_commands, m_async, m_cv.
    std::mutex m_mutex_transactions;                //!< Mutex to protect m_transactions.
    std::atomic<bool> m_initialized = false;        //!< Data was initialized with a snapshot.
    std::atomic<bool> m_running = false;            //!< Client is running.
    std::condition_variable m_cv;                   //!< Condition variable to wait for changes in state.
    std::atomic<state_e> m_state;                   //!< Client state.
    std::exception_ptr m_error;                     //!< Stored exception (if any).
    gto::cqueue<command_ptr> m_commands;            //!< Async commands pending to be digested by the event loop.
    gto::cqueue<request_ptr> m_requests;            //!< Requests pending to receive a response from the server.
    gto::cqueue<submit_ptr> m_accepted;             //!< Accepted submits pending to receive its update from the server.

    std::thread::id m_loop_thread_id;               //!< Thread id of the event loop thread.
    uv_loop_t m_loop;                               //!< Event loop.
    uv_timer_t m_timer_con_lost;                    //!< Connection-lost timer.
    uv_timer_t m_timer_reconnect;                   //!< Reconnect timer.
    uv_signal_t m_signal_sigint;                    //!< SIGINT handler (Ctrl-C).
    uv_async_t m_async_command;                     //!< Used to notify new pending commands

  private:  // methods

    void set_state(state_e state);
    void send(flatbuffers::DetachedBuffer &&buf);
    void schedule_reconnect(std::uint32_t delay_ms);
    void purge_unused_txs();
    void cancel_requests();

    void process_close_cmd(command_ptr &&cmd);
    void process_submit_cmd(command_ptr &&cmd);
    void process_ping_cmd(command_ptr &&cmd);
    void process_sessions_cmd(command_ptr &&cmd);

    void process_login_resp(connection *con, const msgs::LoginResponse *resp);
    void process_snapshot_resp(const msgs::SnapshotResponse *resp);
    void process_updates_resp(const msgs::UpdatesResponse *resp);
    void process_submit_resp(const msgs::SubmitResponse *resp);
    void process_sessions_resp(const msgs::SessionsResponse *resp);
    void process_updates_push(const msgs::UpdatesPush *resp);
    void process_sessions_push(const msgs::SessionsPush *resp);
    void process_keepalive_push(const msgs::KeepAlivePush *resp);
    void process_ping_resp(const msgs::PingResponse *resp);
    void process_update(const msgs::Update *upd);
};

} // namespace nplex
