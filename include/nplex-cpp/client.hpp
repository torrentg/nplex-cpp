#pragma once

#include <memory>
#include <utility>
#include "types.hpp"
#include "params.hpp"
#include "exception.hpp"
#include "transaction.hpp"

namespace nplex {

/**
 * Async nplex client.
 * 
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
 */
class client_t
{
    enum class state_e : std::uint8_t {
        CONNECTING,                             //!< Connecting to the server.
        SYNCHRONIZING,                          //!< Initializing the cache.
        SYNCED,                                 //!< Client is synced with the server.
        RECONNECTING,                           //!< Reconnecting to the server.
        CLOSED                                  //!< Client is closed.
    };

    enum class load_mode_e : std::uint8_t {
        SNAPSHOT_AT_FIXED_REV,                  //!< Sends snapshot at a fixed revision. and subsequent commits.
        SNAPSHOT_AT_LAST_REV,                   //!< Sends snapshot at the last revision and subsequent commits.   
        ONLY_UPDATES_FROM_REV                   //!< Sends only updates from a revision.   
    };

    using tx_ptr = std::shared_ptr<transaction_t>;
    using load_cmd_t = std::pair<load_mode_e, rev_t>;

  private:

    struct impl_t;
    std::unique_ptr<impl_t> m_impl;

  public:

    /**
     * Client constructor.
     * 
     * This method starts the event loop.
     * You can stop it by calling the close() method or destroying the object.
     * 
     * @triggers on_connected() If the client is connected.
     * @triggers on_error() If an error occurs.
     * 
     * @param[in] params Connection parameters.
     * 
     * @exception nplex_exception Thrown if the parameters are invalid.
     */
    client_t(const params_t &params);
    ~client_t();

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
     *         false otherwise (ex. dirty, read-only, no-alter).
     * 
     * @exception std::invalid_argument Transaction is empty (null).
     * @exception nplex_exception client-not-synced, tx-not-found, tx-not-open.
     */
    bool submit_tx(tx_ptr tx, bool force = false);

    //TODO: create a submit() method returning a future

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
     * Close the event loop and the client becomes invalid.
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
    virtual load_cmd_t on_connect([[maybe_unused]] const char *server, [[maybe_unused]] rev_t oldest_rev, [[maybe_unused]] rev_t newest_rev) {
        return {load_mode_e::SNAPSHOT_AT_LAST_REV, 0};
    }

    /**
     * Callback function that is called when the client successfully reconnects to the server.
     * 
     * This function handles the reconnection event and determines the load command
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
    virtual load_cmd_t on_reconnect([[maybe_unused]] const char *server, [[maybe_unused]] rev_t oldest_rev, [[maybe_unused]] rev_t newest_rev) {
        return {load_mode_e::ONLY_UPDATES_FROM_REV, rev()};
    }

    /**
     * Callback function that is called when the client is disconnected from the server.
     * 
     * This function handles the disconnection event, performing necessary cleanup and
     * attempting to reconnect if applicable.
     * 
     * @triggers on_reconnect() If the client reconnects (returns true).
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
     * When this method is called, the client is in the SYNCHRONIZING state and the 
     * local database was just reseted to the snapshot content.
     * 
     * Use this method to update your business objects.
     */
    virtual void on_snapshot() {}

    /**
     * Callback function that is called when an update is received from the server.
     * 
     * When this method is called, changes was already applied to the local database.
     * Use this method to update your business objects.
     *
     * @param[in] meta Transaction metadata.
     * @param[in] changes List of changes.
     * @param[in] tx User transaction that originated the changes (null if this update is not related to a user commit).
     */
    virtual void on_update([[maybe_unused]] const meta_ptr &meta, [[maybe_unused]] const std::vector<change_t> &changes, [[maybe_unused]] tx_ptr tx = nullptr) {}

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
    virtual void on_reject([[maybe_unused]] tx_ptr tx) {}

    /**
     * Callback function that is called when an error occurs.
     * 
     * This function handles error events, allowing the client to take appropriate actions,
     * such as logging the error or attempting recovery.
     * 
     * @param[in] msg The error message describing the issue.
     */
    virtual void on_error([[maybe_unused]] const char *msg) {}
};

}; // namespace nplex
