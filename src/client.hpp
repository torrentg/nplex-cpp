#pragma once

#include <uv.h>
#include <thread>
#include "cqueue.hpp"
#include "mqueue.hpp"
#include "types.hpp"
#include "transaction.hpp"

namespace nplex {

// Forward declaration.
struct cache_t;

class params_t
{
  public:
    std::string hosts;                          //!< Comma-separated list of server:port.
    std::string user;                           //!< User name.
    std::string password;                       //!< User password.
    std::uint32_t timeout;                      //!< Connection timeout.
    std::uint32_t reconnect;                    //!< Reconnect interval.
    std::uint32_t max_retries;                  //!< Maximum number of retries.
    std::uint32_t max_pending;                  //!< Maximum number of pending commands.
    std::uint32_t max_transactions;             //!< Maximum number of transactions.
    std::uint32_t max_tx_size;                  //!< Maximum number of items per transaction.
    std::uint32_t max_tx_time;                  //!< Maximum time per transaction.
    std::uint32_t max_tx_pending;               //!< Maximum number of pending transactions.
    std::uint32_t max_tx_queue_size;            //!< Maximum size of the transaction queue.
    std::uint32_t max_tx_queue_time;            //!< Maximum time of the transaction queue.
};

struct load_cmd_t
{
    enum class mode_e : std::uint8_t {
        SNAPSHOT_AT_FIXED_REV,                  //!< Sends snapshot at a fixed revision. and subsequent commits.
        SNAPSHOT_AT_LAST_REV,                   //!< Sends snapshot at the last revision and subsequent commits.   
        ONLY_UPDATES_FROM_REV                   //!< Sends only updates from a revision.   
    };

    mode_e mode;                                //!< Data load mode.
    rev_t rev;                                  //!< Receives all commits greater than this value.
};

class command_t
{
    enum class type_e : std::uint8_t {
        LOAD,
        SUBMIT,
        PING,
        CLOSE,
    };
};

class event_t
{
    enum class type_e : std::uint8_t {
        SNAPSHOT,
        COMMIT,
        SUBMIT_RESPONSE,
        DISCONNECT
    };
};

/**
 * Async nplex client.
 * 
 * By default this class only updates the database values.
 * Extend this class to create a client fulfilling your needs.
 * Add your business members and override the virtual functions.
 * 
 * Nplex is a centralized key-value database.
 * Each client has an in-memory copy of the database that is updated via streaming.
 * Client accesses the in-memory data using a transaction granting the required isolation level. 
 * This transaction can be invalidated on new commits arrival.
 * To alter the database content, the client submits the transaction to the server, which commits them 
 * (and notifies to all clients) if data integrity is preserved, or rejects them if there is a conflict. 
 * 
 * PROS:
 *   - High performance (in-memory speed).
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
 *   - Attachs to the event loop (used to contact the server).
 *   - Starts a thread to execute the server events.
 *   - Connects to the nplex server (login).
 *   - Receives events from server (ex: snapshot, commits).
 *     - Updating the local database contents.
 *     - Updating ongoing transactions.
 *     - Notifying database changes to the user.
 *   - Manages connection-lost and reconnections.
 *   - Allows to read/update db content using transactions.
 * 
 * Nplex client grants:
 *   - Strictly ordered event processing.
 *   - Thread-safe access to the database.
 *   - Database access according to the isolation level.
 * 
 * @example:
 *
 *   TODO: Add example.
 */
class client_t
{
    using cache_ptr = std::shared_ptr<cache_t>;
    using tx_ptr = std::shared_ptr<transaction_t>;

    enum class state_e : std::uint8_t {
        CONNECTING,                                 //!< Connecting to the server.
        SYNCHRONIZING,                              //!< Initializing the cache.
        SYNCED,                                     //!< Client is synced with the server.
        RECONNECTING,                               //!< Reconnecting to the server.
        CLOSED                                      //!< Client is closed.
    };

    //TODO: create synced() method ?

  private:

    uv_loop_t *loop;                                //!< Event loop.
    uv_async_t async;                               //!< Signals that there are input commands.
    std::thread thread;                             //!< Worker thread, execute output commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    mqueue<event_t> events;                         //!< Events pending to be digested by the working thread.
    gto::cqueue<tx_ptr> ongoing_tx;                 //!< List of ongoing transactions (user working on it).
    gto::cqueue<tx_ptr> pending_tx;                 //!< List of pending transactions (awaiting server response).
    cache_ptr data;                                 //!< Database content.
    state_e state;                                  //!< Client state.
    bool can_force = false;                         //!< User can force transactions (this grant is given by server at login).

  public:

    /**
     * Client constructor.
     * 
     * @triggers on_connected() If the client is connected.
     * @triggers on_error() If an error occurs.
     * 
     * @param[in] params Connection parameters.
     * @param[in] loop Event loop (if NULL creates a new loop).
     * 
     * @exception std::invalid_argument Thrown if the parameters are invalid.
     */
    client_t(const params_t &params, uv_loop_t *loop = nullptr);

    /**
     * Create a new transaction.
     * 
     * It will be automatically updated on each commit.
     * @see transaction_t::is_dirty() to check if the transaction is still valid.
     * 
     * @param[in] isolation Isolation level.
     * @param[in] read_only Read-only flag.
     * @return The transaction.
     */
    tx_ptr create_tx(transaction_t::isolation_e isolation = transaction_t::isolation_e::READ_COMMITTED, bool read_only = false);

    /**
     * Submit a transaction to the server.
     * 
     * Response will be notified via on_committed() or on_rejected().
     * 
     * @triggers on_committed()
     * @triggers on_rejected()
     * 
     * @param[in] tx Transaction to submit.
     * @param[in] force Force to accept values even if the transaction is dirty.
     * 
     * @return true if the transaction was submitted, 
     *         false otherwise (ex. dirty, tx-not-found, invalid-state, read-only, no-alter).
     */
    bool submit_tx(tx_ptr tx, bool force = false);

    /**
     * Remove transaction from updates.
     * 
     * Subsequent commits will not update the transaction.
     * This transaction can no longer be submitted.
     * 
     * @param[in] tx Transaction to remove from ongoing transactions list.
     * 
     * @return true if the transaction was removed, false otherwise (tx-not-found, invalid-state).
     */
    bool discard_tx(tx_ptr tx);

    /**
     * Sends a delayed command to disconnect the client.
     * 
     * Close the event loop (if owns it), the worker thread, and the client becomes invalid.
     * 
     * @triggers on_disconnected().
     * @triggers on_closed().
     * 
     * @param[in] immediate If true, all pending commands are discarded, 
     *                      otherwise process pending commands and disconnects.
     */
    void close(bool immediate = false);

  protected:

    /**
     * Callback function that is called when the client successfully connects to the server.
     * 
     * This function handles the connection event and determines the initial load command
     * to send to the server.
     * 
     * @triggers on_snapshot() If snapshot was requested.
     * @triggers on_commit() On every new commit.
     * 
     * @param[in] server Server identifier (host:port).
     * @param[in] oldest_rev Oldest revision available on the server.
     * @param[in] newest_rev Newest revision available on the server.
     * 
     * @return The load command to send to the server.
     */
    virtual load_cmd_t on_connect([[maybe_unused]] const char *server, rev_t oldest_rev, rev_t newest_rev) {
        return {load_cmd_t::mode_e::SNAPSHOT_AT_LAST_REV, 0};
    }

    // TODO: create on_reconnect() method ?

    /**
     * Callback function that is called when the client is disconnected from the server.
     * 
     * This function handles the disconnection event, performing necessary cleanup and
     * attempting to reconnect if applicable.
     * 
     * @triggers on_connect() If the client reconnects (returns true).
     * @triggers on_close() If the client is closed (returns false).
     * 
     * @param[in] server Server identifier (host:port).
     * @param[in] cause Disconnection cause.
     * 
     * @return true if the client should try to reconnect, false the client is closed.
     */
    virtual bool on_disconnect([[maybe_unused]] const char *server, [[maybe_unused]] const char *cause) {
        return true;
    }

    /**
     * Callback function that is called when the client is closed.
     * 
     * This function handles the event when the client is closed, performing necessary cleanup.
     * After this function is called, the client is no longer valid.
     */
    virtual void on_close() {}

    /**
     * Callback function that is called when a snapshot is received from the server.
     * 
     * This function handles the snapshot event, updating the local in-memory database
     * with the snapshot data.
     * 
     * Local database just reseted to snapshot contents.
     */
    virtual void on_snapshot() {}

    /**
     * Callback function that is called when a commit is received from the server.
     * 
     * This function handles the commit event, updating the local in-memory database
     * with the commit data.
     * 
     * Local database just applied changes and its revisions is the commit revision.
     * 
     * @param[in] meta Transaction metadata.
     * @param[in] changes List of changes.
     */
    virtual void on_commit(const meta_ptr &meta, const std::vector<change_t> &changes) {}

    /**
     * Callback function that is called when a transaction is rejected by the server.
     * 
     * This function handles the event when a transaction is rejected, allowing the client
     * to take appropriate actions, such as logging the rejection or retrying the transaction.
     * 
     * If you need to resubmit the transaction, creates a new one.
     * 
     * @param[in] tx The transaction that was rejected.
     */
    virtual void on_reject(tx_ptr tx) {}

    /**
     * Callback function that is called when an error occurs.
     * 
     * This function handles error events, allowing the client to take appropriate actions,
     * such as logging the error or attempting recovery.
     * 
     * @param[in] msg The error message describing the issue.
     */
    virtual void on_error(const char *msg) {}
};

}; // namespace nplex
