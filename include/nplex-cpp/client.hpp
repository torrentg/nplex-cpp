#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <stop_token>
#include "types.hpp"
#include "exception.hpp"
#include "transaction.hpp"

namespace nplex {

// Forward declaration
class client;
using client_ptr = std::shared_ptr<client>;

/**
 * Configuration parameters for the Nplex client.
 */
struct params_t
{
    std::string servers;                //!< Comma‑separated server list (ex: "host1:port1, host2:port2").
    std::string user;                   //!< User name (as declared in the nplex.ini server file).
    std::string password;               //!< User password (as declared in the nplex.ini server file).
    std::uint32_t max_active_txs = 0;   //!< Maximum number of concurrent transactions (0 = unlimited).
    std::uint32_t max_msg_bytes = 0;    //!< Maximum message size (0 = unlimited).
    std::uint32_t max_unack_msgs = 0;   //!< Maximum number of output in-flight messages (0 = unlimited).
    std::uint32_t max_unack_bytes = 0;  //!< Maximum bytes of output in-flight messages (0 = unlimited).
    float timeout_factor = 2.5;         //!< Timeout factor (greater than 1.0, used to check for connection lost).
};

/**
 * Logger interface.
 * 
 * This interface provides a method to log nplex messages.
 * 
 * By default, the Nplex client will not log any message.
 * 
 * Important notes:
 *   - The log method is executed in the event loop thread.
 *   - Avoid using long-running tasks or blocking functions.
 *   - Exceptions thrown in the log method will terminate the client.
 */
class logger
{
  public:  // types

    enum log_level_e : std::uint8_t {
        DEBUG,                          //!< Debug messages.
        INFO,                           //!< Informational messages.
        WARN,                           //!< Warning messages.
        ERROR                           //!< Error messages.
    };

  public:  // methods

    explicit logger(log_level_e level = log_level_e::INFO) : m_log_level{level} {}
    virtual ~logger() = default;

    /**
     * Returns the log level.
     * 
     * @return Log level.
     */
    log_level_e log_level() const noexcept { return m_log_level.load(std::memory_order_relaxed); }

    /**
     * Sets the log level.
     * 
     * @param[in] level Log level.
     */
    void log_level(log_level_e level) noexcept { m_log_level.store(level); }

    /**
     * Function used by Nplex to trace messages.
     * 
     * This function is only called if the message severity is greater than 
     * or equal to the logger's severity level.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * @param[in] cli Nplex instance.
     * @param[in] severity Message severity.
     * @param[in] msg Message to log.
     */
    virtual void log([[maybe_unused]] client &cli, 
                     [[maybe_unused]] log_level_e severity,
                     [[maybe_unused]] const std::string &msg) {}

  protected:  // members

    std::atomic<log_level_e> m_log_level;
};

/**
 * Nplex lifecycle manager interface.
 * 
 * This interface provides callback methods to handle the Nplex lifecycle events.
 * 
 * Extending this interface allows you to:
 *   - Manage actions when the server is not available (ex: retry after 30 sec).
 *   - Handle connection lost events.
 * 
 * By default, the Nplex client will:
 *   - Close if the server is not available.
 *   - Close if the connection is lost.
 * 
 * Important notes:
 *   - All these methods are executed in the event loop thread.
 *   - Avoid using long-running tasks or blocking functions.
 *   - Exceptions thrown in these methods will terminate the client.
 */
class lifecycle_mngr
{
  public:

    lifecycle_mngr() = default;
    virtual ~lifecycle_mngr() = default;

    /**
     * Callback function that is invoked when an established connection to server is lost.
     * 
     * It returns true to try to reconnect, or false to close the nplex client.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * @param[in] cli Nplex instance.
     * @param[in] srv Server identifier (host:port). 
     * 
     * @return true = try to reconnect,
     *         false = close nplex. 
     */
    virtual bool on_connection_lost([[maybe_unused]] client &cli, 
                                    [[maybe_unused]] const std::string &srv) {
        return false;
    }

    /**
     * Callback function that is invoked when nplex is not able to connect to ANY server.
     * 
     * It returns the number of milliseconds to wait before attempting a reconnection. 
     * If the return value is negative terminates the client.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * @param[in] cli Nplex instance.
     * 
     * @return Number of milliseconds to wait before attempting a reconnection.
     *         If negative, terminates the client.
     */
    virtual std::int32_t on_connection_failed([[maybe_unused]] client &cli) {
        return -1;
    }
};

/**
 * Nplex reactor interface.
 * 
 * This interface provides callback methods to handle the Nplex data events.
 * 
 * Extending this interface allows you to:
 *   - Handle snapshot (initial data loading).
 *   - Handle each new update.
 * 
 * Important notes:
 *   - All these methods are executed in the event loop thread.
 *   - Avoid using long-running tasks or blocking functions.
 *   - Exceptions thrown in these methods will terminate the client.
 */
class reactor
{
  public:

    reactor() = default;
    virtual ~reactor() = default;

    /**
     * Callback function that is called when a snapshot is received from the server.
     * 
     * When this method is called, the local database was just populated with the 
     * snapshot content. The user can now read the database content using a transaction 
     * (READ_COMMITTED level suffices).
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * Use this method to update your business objects.
     * 
     * @param[in] cli Nplex instance.
     */
    virtual void on_snapshot([[maybe_unused]] client &cli) {}

    /**
     * Callback function that is called when an update is received from the server.
     * 
     * When this method is called, changes were already applied to the local database.
     * You can use this method to update your business objects or trigger actions.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     *
     * @param[in] cli Nplex instance.
     * @param[in] meta Transaction metadata.
     * @param[in] changes List of changes.
     */
    virtual void on_update([[maybe_unused]] client &cli, 
                           [[maybe_unused]] const meta_ptr &meta, 
                           [[maybe_unused]] const std::vector<change_t> &changes) {}
};

/**
 * Nplex client.
 * 
 * Nplex is a centralized key-value database.
 * Each client has an in-memory copy of the database that is updated via streaming.
 * 
 * Client access data using a transaction granting the required isolation level. 
 * This transaction can be invalidated on new commits arrival.
 * 
 * To alter the database content, the client submits the transaction to the server, 
 * which commits them (and notifies to all clients) if data integrity is preserved, 
 * or rejects them if there is a conflict. 
 * 
 * PROS:
 *   - High performance (in-memory + async).
 *   - Serialization is granted (data consistency, integrity).
 * CONS:
 *   - Transaction validation delayed until submit and commit stages.
 *   - A transaction can be rejected if conflicts with an intermediate commit.
 *   - Table-locking or row-locking not available.
 *   - Significant network traffic.
 * 
 * To reduce the network traffic the client can be configured to operate only on a 
 * subset of key-values. In this case, it only receives updates for the selected keys.
 * 
 * Nplex client grants:
 *   - Strictly ordered event processing.
 *   - Thread-safe access to the database.
 *   - Data access according to the tx isolation level.
 */
class client : public std::enable_shared_from_this<client>
{
  public:  // types

    using millis = std::chrono::milliseconds;

  public:  // methods

    virtual ~client() = default;

    /**
     * Client factory.
     * 
     * @param[in] params Connection parameters.
     * 
     * @exception nplex::nplex_exception Invalid parameters.
     */
    [[nodiscard]] static client_ptr create(const params_t &params);

    /**
     * Constant methods.
     */
    [[nodiscard]] virtual bool is_usable() const = 0;  //!< Initial data loaded (perhaps out-of-sync or connection-lost).
    [[nodiscard]] virtual bool is_synced() const = 0;  //!< Client is connected to server and synced.
    [[nodiscard]] virtual bool is_closed() const = 0;  //!< Client was closed.

    /**
     * Register a logger to trace nplex messages.
     * 
     * By default, the Nplex client will not log any message.
     * 
     * @param[in] log Logger to trace nplex messages.
     * 
     * @return Reference to the client.
     * @exception nplex_exception Nplex client already connected.
     */
    virtual client & set_logger(const std::shared_ptr<logger> &log) = 0;

    /**
     * Register a lifecycle manager to handle the nplex lifecycle events.
     * 
     * By default, the Nplex client will:
     *  - Try to connect to the first available server.
     *  - If no server is available, it will close.
     *  - After connection, if the connection is lost, it will close.
     * 
     * @param[in] mngr Lifecycle manager to handle nplex lifecycle events.
     * 
     * @return Reference to the client.
     * @exception nplex_exception Nplex client already connected.
     */
    virtual client & set_lifecycle_mngr(const std::shared_ptr<lifecycle_mngr> &mngr) = 0;

    /**
     * Register the reactors to handle the data events.
     * 
     * Allows to manage the database in a reactive way.
     * By default, the client does nothing when receiving a snapshot and updates.
     * By registering a reactor, you can handle these events and implement your 
     * custom logic.
     * 
     * @param[in] rct Reactor to handle data events.
     * 
     * @return Reference to the client.
     * @exception nplex_exception Nplex client already connected.
     */
    virtual client & set_reactor(const std::shared_ptr<reactor> &rct) = 0;

    /**
     * Set the initial revision to load.
     * 
     * By default, the client loads the latest revision available on the server.
     * 
     * @param[in] rev Initial revision.
     * 
     * @return Reference to the client.
     * @exception nplex_exception Nplex client receiving updates.
     */
    virtual client & set_initial_rev(rev_t rev) = 0;

    /**
     * Run the client event loop.
     * 
     * This method blocks until:
     *  - the stop_token is triggered, or
     *  - close() method is called, or
     *  - client is unable to connect or reconnect to any server, or
     *  - an error occurs.
     * 
     * IMPORTANT: Ensure that the client shared_ptr remains alive 
     * for the entire duration of run(). If you call run() from 
     * another thread, keep a shared_ptr copy in that thread.
     * 
     * Example:
     *   auto cli = client::create(params);
     *   std::thread t([cli]() mutable { cli->run(stop_token); });
     *   // The lambda captures a copy of the shared_ptr
     * 
     * @param[in] st Stop token to interrupt the event loop.
     */
    virtual void run(std::stop_token st) noexcept = 0;

    /**
     * This method blocks until the client reaches the target state.
     * This method is thread-safe.
     * 
     * @param[in] timeout Maximum time to wait for the target state. 
     *                    If the timeout is reached, returns false.
     * 
     * @return true if the target state is reached, 
     *         false if the timeout is reached.
     * 
     * @exception nplex_exception If closed, or closed while waiting for.
     */
    virtual bool wait_for_usable(millis timeout = millis::max()) = 0;
    virtual bool wait_for_synced(millis timeout = millis::max()) = 0;

    /**
     * Create a new transaction.
     * 
     * It will be automatically updated on each commit.
     * The transaction can be invalidated if it conflicts with commit. 
     * In this case, the transaction is marked as dirty (invalidated) and 
     * the user must discard it and create a new one, or force the commit
     * with the dirty flag if allowed (not recommended, use with care).
     * 
     * Use transaction methods to read/alter the local data. Use the tx::submit() 
     * method to try to commit the transaction, or tx::discard() to abort it.
     * 
     * @note This method is thread-safe, it can be called from a callback.
     * 
     * @param[in] isolation Isolation level.
     * @param[in] read_only Read-only flag.
     * 
     * @return The transaction.
     * 
     * @exception nplex_exception not-connected, max-tx-exceeded.
     */
    [[nodiscard]] virtual tx_ptr create_tx(transaction::isolation_e isolation = transaction::isolation_e::READ_COMMITTED, bool read_only = false) = 0;

    // TODO: create ping() method.

    /**
     * Sends a delayed command to close/finish the client.
     * 
     * The event loop is stopped and the run() task finishes.
     * All pending commands and pending responses are discarded.
     * 
     * @note This method is async and thread-safe, it can be called from a callback.
     */
    virtual void close() = 0;

  protected:  // methods

    client() = default;

    // Prevent copying and moving
    client(const client&) = delete;
    client& operator=(const client&) = delete;
    client(client&&) = delete;
    client& operator=(client&&) = delete;
};

} // namespace nplex
