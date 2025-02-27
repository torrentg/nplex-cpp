#pragma once

#include <mutex>
#include <thread>
#include <memory>
#include <utility>
#include "types.hpp"
#include "params.hpp"
#include "exception.hpp"
#include "transaction.hpp"

namespace nplex {

// Forward declaration
class client_t;

/**
 * Nplex listener interface.
 * 
 * This interface provides callback methods to handle the Nplex events.
 * 
 * Extending this interface allows you to:
 *   - Manage actions when the server is not available (ex: retry after 30 sec).
 *   - Handle connection lost events.
 *   - Handle initial data loading.
 *   - Handle each new update.
 * 
 * By default, the Nplex client will:
 *   - Close if the server is not available.
 *   - Close if the connection is lost.
 *   - Do nothing on initial data loading.
 *   - Do nothing on each new update.
 * 
 * Important notes:
 *   - All these methods are executed in the event loop thread.
 *   - Avoid using long-running tasks or blocking functions (write to disk, network request, etc).
 *   - Exceptions thrown in these methods will terminate the client.
 */
class listener_t
{
  public:

    enum log_level_e : std::uint8_t {
        DEBUG,                   //!< Debug messages.
        INFO,                    //!< Informational messages.
        WARN,                    //!< Warning messages.
        ERROR                    //!< Error messages.
    };

    enum class load_mode_e : std::uint8_t {
        SNAPSHOT_AT_FIXED_REV,   //!< Requests snapshot at a fixed revision and subsequent commits.
        SNAPSHOT_AT_LAST_REV,    //!< Requests snapshot at the last revision and subsequent commits.
        ONLY_UPDATES_FROM_REV    //!< Requests only updates from a revision.
    };

    using load_cmd_t = std::pair<load_mode_e, rev_t>;

    listener_t(log_level_e level = log_level_e::INFO) : m_log_level{level} {}
    virtual ~listener_t() {}

    /**
     * Callback function that is called when the client successfully logs in to the server.
     * 
     * This function handles the connection event and determines the initial load command
     * to send to the server.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * @triggers on_snapshot() If snapshot was requested.
     * @triggers on_update() On every new commit.
     * 
     * @param[in] client Nplex instance.
     * @param[in] server Server identifier (host:port).
     * @param[in] min_rev Oldest revision available on the server.
     * @param[in] max_rev Newest revision available on the server.
     * 
     * @return The load command to send to the server.
     */
    virtual load_cmd_t on_connected([[maybe_unused]] client_t &client, 
                                    [[maybe_unused]] const std::string &server, 
                                    [[maybe_unused]] rev_t min_rev, 
                                    [[maybe_unused]] rev_t max_rev) {
        return {load_mode_e::SNAPSHOT_AT_LAST_REV, 0};
    }

    /**
     * Callback function that is invoked when connection attempt fails or when an established 
     * connection is lost.
     * 
     * This function is responsible for handling the disconnection event and determining
     * the reconnection strategy. It returns the number of milliseconds to wait before
     * attempting a reconnection. If the return value is negative terminates the client.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * @triggers on_connected() If reconnection attempt success.
     * @triggers on_connection_lost() If reconnection attempt fails.
     * @triggers on_closed() If the client is closed (negative value).
     * 
     * @param[in] client Nplex instance.
     * @param[in] server Server identifier (host:port). 
     *                   Can be empty if there is no previous connection (ex: failed retry).
     * 
     * @return The number of milliseconds to wait before trying a reconnection, 
     *         a negative value close nplex.
     */
    virtual std::int32_t on_connection_lost([[maybe_unused]] client_t &client, 
                                            [[maybe_unused]] const std::string &server) {
        return -1;
    }

    /**
     * Callback function that is called when the client is closed.
     * 
     * After this function is called, the client is no longer valid.
     * 
     * This method can block if required (write to disk, etc).
     *  
     * @param[in] client Nplex instance.
     */
    virtual void on_closed([[maybe_unused]] client_t &client) {}

    /**
     * Callback function that is called when a snapshot is received from the server.
     * 
     * When this method is called, the client is in the SYNCHRONIZING state and the 
     * local database was just reseted to the snapshot content.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * Use this method to update your business objects.
     * 
     * @param[in] client Nplex instance.
     */
    virtual void on_snapshot([[maybe_unused]] client_t &client) {}

    /**
     * Callback function that is called when an update is received from the server.
     * 
     * When this method is called, changes was already applied to the local database.
     * Use this method to update your business objects.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     *
     * @param[in] client Nplex instance.
     * @param[in] meta Transaction metadata.
     * @param[in] changes List of changes.
     */
    virtual void on_update([[maybe_unused]] client_t &client, 
                           [[maybe_unused]] const meta_ptr &meta, 
                           [[maybe_unused]] const std::vector<change_t> &changes) {}

    /**
     * Function used by Nplex to trace messages.
     * 
     * This function is executed in the event loop thread. Do not block it.
     * If an exception is thrown, the client will terminate.
     * 
     * @param[in] client Nplex instance.
     * @param[in] severity Message severity.
     * @param[in] msg Message to log.
     */
    virtual void log([[maybe_unused]] client_t &client, 
                     [[maybe_unused]] log_level_e severity,
                     [[maybe_unused]] const std::string &msg) {}

    /**
     * Returns the log level.
     * 
     * You don't need to override this method.
     * 
     * @return Log level.
     */
    log_level_e log_level() const noexcept { return m_log_level; }

  protected:

    log_level_e m_log_level;
};

/**
 * Nplex client.
 * 
 * Nplex is a centralized key-value database.
 * Each client has an in-memory copy of the database that is updated via streaming.
 * Client access the in-memory data using a transaction granting the required isolation level. 
 * This transaction can be invalidated on new commits arrival.
 * To alter the database content, the client submits the transaction to the server, which commits them 
 * (and notifies to all clients) if data integrity is preserved, or rejects them if there is a conflict. 
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
 * On client startup:
 *   - Starts the event loop.
 *   - Connects to the nplex server (login).
 *   - Receives events from server (ex: snapshot, commits).
 *     - Updates the local database contents.
 *     - Updates ongoing transactions.
 *     - Notify database changes to the user.
 *   - Manages connection-lost and reconnections.
 *   - Allows to read/update db content using transactions.
 * 
 * Nplex client grants:
 *   - Strictly ordered event processing.
 *   - Thread-safe access to the database.
 *   - Database access according to the isolation level.
 */
class client_t
{
  private:

    static listener_t default_listener;

  public:

    class impl_t;

    enum class state_e : std::uint8_t {
        CONNECTING,                             //!< Connecting to the server.
        SYNCHRONIZING,                          //!< Initializing the cache (load or crev != update.rev).
        SYNCHRONIZED,                           //!< Client is synced with the server.
        DISCONNECTED,                           //!< Client is disconnected.
        CLOSED                                  //!< Client is closed.
    };

    /**
     * Client constructor.
     * 
     * This method blocks until connection is established.
     * On connection failure returns an exception.
     * 
     * This method starts a thread running the event loop.
     * Methods to terminate the event loop:
     *   - Awaiting until termination using the join() method.
     *   - Destroying the client_t object.
     *   - Calling the close() method.
     * 
     * @triggers on_connection_lost() Unable to connect to cluster.
     * @triggers on_connected() When the client connects to server.
     * @triggers on_error() If an error occurs (ex: authentication error).
     * 
     * @param[in] params Connection parameters.
     * @param[in] listener Listener to handle client events.
     * 
     * @exception nplex::invalid_config Invalid param (no-servers, no-user, no-password, etc).
     * @exception nplex::connection_failed Unable to connect to nplex cluster.
     */
    client_t(const params_t &params, listener_t &listener = default_listener);
    virtual ~client_t();

    /**
     * Returns current client state.
     * 
     * @return Current state.
     */
    state_e state() const;

    /**
     * Returns the local database revision.
     * 
     * @return Data revision.
     */
    rev_t rev() const;

    /**
     * Create a new transaction.
     * 
     * It will be automatically updated on each commit.
     * @see transaction_t::is_dirty() to check if the transaction is still valid.
     * 
     * @param[in] isolation Isolation level.
     * @param[in] read_only Read-only flag.
     * @return The transaction.
     * 
     * @exception nplex_exception max-tx-exceeded.
     * @exception nplex::connection_failed Connection not available.
     */
    tx_ptr create_tx(transaction_t::isolation_e isolation = transaction_t::isolation_e::READ_COMMITTED, bool read_only = false);

    /**
     * Submit a transaction to the server.
     * 
     * On error, the transaction is rejected and the on_rejected() callback is called.
     * Response will be notified via on_committed() or on_rejected().
     * 
     * @triggers on_committed()
     * @triggers on_rejected()
     * 
     * @param[in] tx Transaction to submit.
     * @param[in] force Force to accept values even if the transaction is dirty.
     * 
     * @return true if the transaction was submitted, 
     *         false otherwise (ex. dirty, read-only, no-alter).
     * 
     * @exception std::invalid_argument Transaction is empty (null).
     * @exception nplex_exception client-not-synced, tx-not-found, tx-not-open, max-queued-commands.
     * @exception nplex::connection_failed Connection not available.
     */
    bool submit_tx(const tx_ptr &tx, bool force = false);

    //TODO: create a submit() method returning a future

    /**
     * Remove transaction from updates.
     * 
     * Subsequent commits will not update the transaction.
     * This transaction can no longer be submitted.
     * 
     * You are not forced to call discard() to release a transaction, you 
     * can simply remove it using tx.reset() or destroying the tx object. 
     * On database updates, unused transactions are automatically removed.
     * 
     * @param[in] tx Transaction to remove from ongoing transactions list.
     * 
     * @return true if the transaction was removed, 
     *         false otherwise (tx-not-found, invalid-state).
     */
    bool discard_tx(const tx_ptr &tx);

    /**
     * Sends a delayed command to disconnect the client.
     * 
     * Close the event loop and the client becomes invalid.
     * All pending commands and pending responses are discarded.
     * 
     * This is a blocking command, it waits for the event loop to finish.
     * 
     * @triggers on_closed().
     */
    void close();

    /**
     * Waits for the event loop to finish.
     * 
     * This method is blocking and waits for the event loop to finish.
     */
    void join();

  private:

    std::unique_ptr<impl_t> m_impl;
    std::thread thread_loop;
    std::mutex m_mutex;
};

} // namespace nplex
