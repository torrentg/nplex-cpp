#pragma once

#include <functional>
#include "types.hpp"

#define NPLEX_CREATE 1
#define NPLEX_UPDATE 2
#define NPLEX_DELETE 4

namespace nplex {

// Forward declaration
struct cache_t;

/**
 * Database access can only be done through a transaction, which allows you to operate
 * according to an isolation level.
 * 
 * Isolation levels:
 *
 *   read-committed: Read always most recent data (value = always last committed rev).
 *     - Pros: Minimal overhead; high performance.
 *     - Cons: Leads to non-repeatable reads and phantoms.
 *     - Note: Invalidated if any new transaction alters the c-ud.
 *  
 *   repeatable-reads: Read data will not change during the transaction (value = 1st read).
 *     - Pros: Prevents non-repeatable reads.
 *     - Cons: Does not prevent phantoms.
 *     - Note: Invalidated if any new transaction alters the crud keys.
 *  
 *   serializable: All read data will not change during the transaction (value = tx creation).
 *     - Pros: Prevents non-repeatable reads, and phantoms.
 *     - Cons: Higher overhead.
 *     - Note: Invalidated if any new transaction alters the crud keys.
 * 
 * Dirty flag:
 * 
 *   A transaction is dirty if it was invalidated by an external update (commit).
 *   This transaction will be rejected at submit time, except if you submit it using 
 *   the forced mode. A dirty transaction continue to be updated on each commit.
 * 
 * Forced mode:
 * 
 *   This mode is declared at submit time (@see client_t::submit_tx()).
 *   At submit time (client-side) and at commit time (server-side), the transaction is checked 
 *   for conflicts to ensure serialization. Some users (like admin) can force a transaction to 
 *   be committed even if it is dirty or conflicts with another commit. In this case, the 
 *   serialization guarantee is not granted. This mode is useful to apply emergency patches or 
 *   to fix data integrity issues.
 */
class transaction_t
{
  public:

    enum class state_e : std::uint8_t {
        OPEN,                                   //!< Transaction is ongoing (user is fetching data).
        SUBMITTED,                              //!< Transaction was submitted to the server.
        ACCEPTED,                               //!< Transaction was accepted by the server (pending to receive the commit).
        COMMITTED,                              //!< Transaction was committed.
        REJECTED,                               //!< Transaction was rejected by the server (commit was rejected).
        DISCARDED,                              //!< Transaction was discarded by the user.
        ABORTED                                 //!< Transaction was discarded by the server (ex. server disconnected).
    };

    enum class isolation_e : std::uint8_t {
        READ_COMMITTED,                         //!< Read always most recent data.
        REPEATABLE_READS,                       //!< Read data will not change during the transaction.
        SERIALIZABLE                            //!< All read data will not change during the transaction.
    };

    using cache_ptr = std::shared_ptr<cache_t>;
    using callback_t = std::function<bool(const gto::cstring &key, const value_t &value)>;
    friend struct transaction_impl_t;

  private:

    transaction_t() = default;
    // non-copyable class
    transaction_t(const transaction_t&) = delete;
    transaction_t& operator=(const transaction_t&) = delete;
    // non-movable class
    transaction_t(transaction_t&&) = delete;
    transaction_t& operator=(transaction_t&&) = delete;

  protected:

    /**
     * Updates the current transaction with the changes from a commit.
     * 
     * This method is called by the client on each update.
     * The transaction becomes dirty if there are update conflicts.
     * 
     * @param[in] changes List of update changes.
     */
    void update(const std::vector<change_t> &changes);

    // Only for debug purposes
    void dirty(bool dirty);
    void state(state_e state);

  public:

    /**
     * Create a new transaction.
     * 
     * @param[in] cache The database to use.
     * @param[in] isolation The isolation level to use.
     * @param[in] read_only If true, the transaction is read-only.
     * 
     * @return A pointer to the new transaction.
     * 
     * @exception std::invalid_argument Thrown if data is invalid.
     */
    static std::shared_ptr<transaction_t> create(cache_ptr cache, isolation_e isolation, bool read_only = false);

    isolation_e isolation() const;
    bool read_only() const;
    state_e state() const;
    bool dirty() const;
    std::uint32_t type() const;
    void type(std::uint32_t type);

    /**
     * Read a key-value pair.
     * 
     * The behavior depends on the isolation level:
     * - read-committed:
     *   - Returns the most recent value of the key.
     *   - Consecutive calls with the same key may return different values.
     * - repeatable-reads:
     *   - The value of the key will not change during the transaction.
     *   - Key value is fixed at the first read.
     *   - Consecutive calls with the same key will return the same value, 
     *     even if a commit modified the value in between.
     * - serializable:
     *   - The value of the key will not change during the transaction.
     *   - The value is fixed at the transaction creation revision.
     *   - Consecutive calls with the same key will return the same value, 
     *     even if a commit modified the value in between.
     * 
     * The 'check' flag is used to ensure that the key-value pair has not been updated or deleted
     * since it was read. This flag applies even in 'force' mode. Use this flag to enforce
     * serialization constraints on read keys. For example:
     *   - You want to ensure that x = a + b.
     *   - You read a and b, then update x.
     *   - If you don't mark 'a' and 'b' with 'check', a commit may have modified the value of 'a' or 'b' in between.
     *   - If you mark 'a' and 'b' with 'check', the commit will fail if 'a' or 'b' was modified in between.
     * 
     * @see ensure()
     * 
     * @param[in] key The key to read.
     * @param[in] check If true, checks at commit time that the key-value pair was not modified.
     *                  Is equivalent to call 'ensure(key, NPLEX_CREATE|NPLEX_UPDATE|NPLEX_DELETE)'.
     * 
     * @return The value associated with the key (empty if not found or previously deleted).
     *         If the value was previously upsert, then its metadata is empty.
     * 
     * @exception std::invalid_argument Invalid key.
     * @exception nplex_exception Transaction not open, or invalid key.
     */
    value_ptr read(const char *key, bool check = false);

    /**
     * Update a key-value or insert it if not exists.
     * 
     * By default does nothing if the value is unchanged.
     * Use the 'force' flag to update the revision on the unchanged value case.
     * 
     * @param[in] key The key to update.
     * @param[in] data The new data associated with the key.
     * @param[in] force Update entry revision even if value is unchanged.
     * 
     * @return true if key created or updated,
     *         false if existing key has same value and force = false.
     * 
     * @exception std::invalid_argument Invalid key or data.
     * @exception nplex_exception Transaction is read-only, or not open.
     */
    bool upsert(const char *key, const std::string_view &data, bool force = false);

    /**
     * Remove a key-value pair.
     * 
     * Glob patterns are supported (ex: '/users/\*\/error', '/users/jdoe/\**').
     * When using a pattern, the operation is applied to all matching keys.
     * 
     * Caution, deletion using a pattern does not grant that all keys was removed at commit-time. 
     * For example, if you delete a range of values using a pattern, and then a commit adds 
     * a key satisfying the pattern, this new key continue to exists even if the current 
     * transaction is committed succesfully. Use ensure() to avoid these cases.
     * 
     * @param[in] pattern The key or pattern to remove.
     * 
     * @return Number of removals.
     * 
     * @exception std::invalid_argument Invalid key.
     * @exception nplex_exception Transaction is read-only, or not open, or invalid key.
     */
    bool remove(const key_t &key);
    std::size_t remove(const char *pattern);

    /**
     * Appends a validation to be done at commit-time.
     * 
     * Deleting, creating, or updating a set of key-values matching a pattern does not 
     * guarantee that you delete, create, or update all database entries matching this 
     * pattern at commit time. This is because an intermediate commit may add, delete, 
     * or update an entry satisfying that pattern. To avoid these cases, you can add a 
     * check to validate that the condition is fulfilled.
     * 
     * A condition is considered fulfilled when no one has performed any of the 
     * indicated actions from the time the conditions is established until the commit.
     * 
     * The transaction will be rejected if any of the conditions are not met.
     * Transaction ensures applies even in 'force' mode.
     * 
     * Glob patterns are supported (ex: '/users/\*\/error', '/users/jdoe/\**').
     * 
     * The behavior depends on the isolation level:
     * - read-committed:
     *   - Condition applies to the database state at check time.
     * - repeatable-reads:
     *   - Condition applies to the database state at check time.
     * - serializable:
     *   - Condition applies to the database state at transaction creation time.
     * 
     * @example: Verify that nobody modified or deleted a fixed key.
     *     tx.ensure("/users/jdoe/name", NPLEX_UPDATE | NPLEX_DELETE);
     * 
     * @example: Verify that nobody altered a set of keys.
     *     tx.ensure("/users/\**", NPLEX_CREATE | NPLEX_UPDATE | NPLEX_DELETE);
     *
     * @param[in] pattern The pattern to check.
     * @param[in] actions The actions to check (use NPLEX_CREATE, NPLEX_UPDATE, NPLEX_DELETE).
     * 
     * @return true if the condition was set, 
     *         false otherwise (invalid-pattern, unrecognized-action).
     * 
     * @exception nplex_exception Transaction is not open.
     */
    bool ensure(const char *pattern, std::uint8_t actions);

    /**
     * Executes the callback function for each key-value.
     * 
     * Each value is read according to the isolation level.
     * 
     * During iteration the database is locked (no external commits).
     * Iteration stops on the first false return value.
     * 
     * @note Don't block the database for a long time, as this function locks the database.
     * 
     * @example: Simple iteration.
     *  tx.for_each([](const gto::cstring &key, const value_t &value) {
     *      std::cout << key << " = " << value.data() << std::endl;
     *      return true;
     *  });
     * 
     * @example: Search for a value.
     *  tx.for_each("/users/\*\/name", [&id](const gto::cstring &key, const value_t &value) {
     *      if (value.data().contains("mr_hacker")) {
     *          id = key;
     *          return false;
     *      }
     *      return true;
     *  });
     * 
     * @example: Alter database content.
     *  tx.for_each("/users/\*\/name", [&tx](const gto::cstring &key, const value_t &value) {
     *      if (value.data().contains("mr_hacker"))
     *          tx.delete(key);
     *      return true;
     *  });
     * 
     * @param[in] pattern Key pattern.
     * @param[in] callback The callback function to execute. This function receives the key-value pair.
     *                     It must return true to continue the iteration, or false to stop it.
     * 
     * @return Number of iterated elements (0 if to < from).
     * 
     * @exception nplex_exception Transaction is not open, or empty callback function.
     * @exception nplex_exception Transaction is read-only and callback function calls upsert or delete.
     */
    std::size_t for_each(const callback_t &callback) { return for_each("**", callback); }
    std::size_t for_each(const char *pattern, const callback_t &callback);
};

}; // namespace nplex
