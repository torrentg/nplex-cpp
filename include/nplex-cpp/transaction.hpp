#pragma once

#include <functional>
#include <future>
#include "types.hpp"

namespace nplex {

/**
 * Database access can only be done through a transaction, which allows you to operate
 * on data according to an isolation level.
 * 
 * Transaction is an interface where implementation details are hidden from the user.
 * 
 * Transaction is not thread-safe. You should not share a transaction between threads. 
 * If you need to share a transaction, you should use a mutex to protect it.
 * 
 * Transactions can only be created by the client. 
 * @see client::create_tx().
 * 
 * There is no rollback method. If you want to discard a transaction, just destroy it 
 * without submitting it.
 * 
 * Transactions are automatically updated on each database commit. If a transaction is 
 * invalidated by an external commit, it is marked as dirty. Destroy unused transactions 
 * to reduce resources consumption and to avoid confusion.
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
 *   A transaction is dirty if it was invalidated by an external commit.
 *   This transaction will be rejected at submit time, except if you submit it using 
 *   the forced mode. A dirty transaction continue to be updated on each commit.
 * 
 * Forced mode:
 * 
 *   This mode is declared at submit time.
 *   At submit time (client-side) and at commit time (server-side), the transaction is checked 
 *   for conflicts to ensure serialization. Some users (like admin) can force a transaction to 
 *   be committed even if it is dirty or conflicts with another commit. In this case, the 
 *   serialization guarantee is not granted. This mode is useful to apply emergency patches or 
 *   to fix data integrity issues.
 */
class transaction
{
  public:

    enum class state_e : std::uint8_t {
        OPEN,                       //!< Transaction is ongoing (user is fetching data).
        SUBMITTED,                  //!< Transaction was submitted to the server.
        ACCEPTED,                   //!< Transaction was accepted by the server (pending to receive the commit).
        REJECTED,                   //!< Transaction was rejected by the server (commit was rejected).
        COMMITTED,                  //!< Transaction was committed.
        DISCARDED,                  //!< Transaction was discarded by the user.
        ABORTED,                    //!< Transaction was aborted (ex. server disconnected)
    };

    enum class isolation_e : std::uint8_t {
        READ_COMMITTED,             //!< Read always most recent data.
        REPEATABLE_READ,            //!< Read data will not change during the transaction.
        SERIALIZABLE                //!< All read data will not change during the transaction.
    };

    enum class submit_e : std::uint8_t {
        NO_MODIFICATIONS,           //!< There are no modifications to commit (eg. read-only tx).
        COMMITTED,                  //!< Transaction was committed to the server.
        TRY_LATER,                  //!< Server too busy. Try again later.
        REJECTED_PERMISSION,        //!< Commit was rejected due to insufficient permissions.
        REJECTED_OLD_REVISION,      //!< Commit was rejected due to an old revision (eg. long-running tx).
        REJECTED_INTEGRITY,         //!< Commit was rejected due to integrity constraints (eg. dirty tx or another tx modified the same key while this tx was being sent).
        REJECTED_ENSURE,            //!< Commit was rejected due to unsatisfied ensure constraints (eg. another tx altered an ensured key).
        REJECTED_OTHER              //!< Commit was rejected due to other reasons.
    };

    using callback_t = std::function<bool(const key_t &key, const value_t &value)>;

    virtual ~transaction() = default;
    virtual isolation_e isolation() const = 0;
    virtual bool is_read_only() const = 0;
    virtual bool is_dirty() const = 0;
    virtual state_e state() const = 0;

    bool is_closed() const { 
        auto s = state();
        return (s == state_e::REJECTED || 
                s == state_e::COMMITTED || 
                s == state_e::DISCARDED || 
                s == state_e::ABORTED);
    }

    /**
     * Get/set user-defined type of the transaction.
     * 
     * You can use this type to categorize transactions. For example, 
     * you can set the type to '1' for user transactions, and '2' for admin 
     * transactions. This value is not used by the Nplex client or server, 
     * it is only for user reference. By default, its value is '0'.
     * 
     * @param[in] type User-defined type of the transaction.
     */
    virtual std::uint32_t type() const = 0;
    virtual void type(std::uint32_t type) = 0;

    /**
     * Database revision. It value depends on the isolation level:
     * - read-committed: Current database revision.
     * - repeatable-reads: Current database revision.
     * - serializable: Database revision at transaction creation time.
     * 
     * @return Database revision.
     */
    virtual rev_t rev() const = 0;

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
     *                  Is equivalent to call 'ensure(key)'.
     * 
     * @return The value associated with the key,
     *         empty if not found or previously deleted.
     *         If the value was previously upsert, then its metadata is empty.
     * 
     * @exception nplex_exception tx-not-open, or invalid-key.
     */
    virtual value_ptr read(const char *key, bool check = false) = 0;

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
     * @exception nplex_exception tx-read-only, or tx-not-open, or invalid-key, or no-permissions.
     */
    virtual bool upsert(const char *key, const std::string_view &data, bool force = false) = 0;

    /**
     * Remove a key-value pair.
     * 
     * Glob patterns are supported (ex: '/users/\*\/error', '/users/jdoe/\**').
     * When using a pattern, the operation is applied to all matching keys.
     * 
     * Caution: Deletion using a pattern does not guarantee that all keys will be removed 
     * at commit time. For example, if you delete a range of values using a pattern, and 
     * then a commit adds a key satisfying the pattern, this new key continue to exists 
     * even if the current transaction is committed succesfully. Use ensure() to avoid 
     * these cases.
     * 
     * @param[in] pattern The key or pattern to remove.
     * 
     * @return Number of removals.
     * 
     * @exception nplex_exception tx-read-only, or tx-not-open, or invalid-key, or no-permissions.
     */
    virtual bool remove(const key_t &key) = 0;
    virtual std::size_t remove(const char *pattern) = 0;

    /**
     * Appends a validation to be done at commit-time.
     * 
     * Deleting, creating, or updating a set of key-values matching a pattern does not 
     * guarantee that you delete, create, or update all database entries matching this 
     * pattern at commit time. This is because an intermediate commit may add, delete, 
     * or update an entry satisfying that pattern. To avoid these cases, you can add a 
     * check to validate keys matching a pattern that the condition is fulfilled.
     * 
     * An ensure is a condition verified at commit time to check that keys satisfying a 
     * pattern have not been modified by an intermediate transaction.
     * 
     * The transaction will be rejected by server if any of the ensures are not met.
     * Ensures applies even in 'force' mode.
     * 
     * Glob patterns are supported (ex: '/users/\*\/error', '/users/jdoe/\**').
     * 
     * The behavior depends on the isolation level:
     * - read-committed:
     *   - Condition applies to the database state at transaction submit time.
     * - repeatable-reads:
     *   - Condition applies to the database state at transaction submit time.
     * - serializable:
     *   - Condition applies to the database state at transaction creation time.
     * 
     * @example: Verify that nobody modified a fixed key.
     *     tx.ensure("/users/jdoe/name");
     * 
     * @example: Verify that nobody altered a set of keys.
     *     tx.ensure("/users/\**");
     *
     * @param[in] pattern The pattern to check.
     * 
     * @return true if the condition was set, 
     *         false otherwise (invalid-pattern).
     * 
     * @exception nplex_exception tx-not-open.
     */
    virtual bool ensure(const char *pattern) = 0;

    /**
     * Executes the callback function for each key-value.
     * 
     * All keys matching the pattern are read according to the isolation level.
     * 
     * During iteration the database is locked (no external commits).
     * Iteration stops on the first false callback return value.
     * 
     * @note Don't block on callback, as this function locks the database.
     * 
     * @example: Simple iteration.
     *  tx.for_each([](const key_t &key, const value_t &value) {
     *      std::cout << key << " = " << value.data() << std::endl;
     *      return true;
     *  });
     * 
     * @example: Search for a value.
     *  tx.for_each("/users/\*\/name", [&id](const key_t &key, const value_t &value) {
     *      if (value.data().contains("mr_hacker")) {
     *          id = key;
     *          return false;
     *      }
     *      return true;
     *  });
     * 
     * @example: Alter database content.
     *  tx.for_each("/users/\*\/name", [&tx](const key_t &key, const value_t &value) {
     *      if (value.data().contains("mr_hacker"))
     *          tx.remove(key);
     *      return true;
     *  });
     * 
     * @param[in] pattern Key pattern.
     * @param[in] callback The callback function to execute. This function receives the key-value pair.
     *                     It must return true to continue the iteration, or false to stop it.
     * 
     * @return Number of iterated elements (0 if no matches or empty callback).
     * 
     * @exception nplex_exception tx-read-only (if callback calls upsert or delete), 
     *                            no-permissions (if callback calls upsert or delete).
     */
    virtual std::size_t for_each(const callback_t &callback) { return for_each("**", callback); }
    virtual std::size_t for_each(const char *pattern, const callback_t &callback) = 0;

    /**
     * Submit transaction to the server to be committed.
     * 
     * @note This method is thread-safe, it can be called from a callback.
     * 
     * @param[in] force Override data integrity (only allowed if user has sufficient privileges).
     * 
     * @return Future with the transaction return code, or the exception if there is a problem.
     * 
     * @exception nplex_exception not-connected, no-permissions, tx-dirty, etc.
     */
    virtual std::future<submit_e> submit(bool force = false) = 0;

    /**
     * Discard the transaction.
     * 
     * Mark this transaction as discarded. The transaction is removed from the 
     * client transaction list and is not updated on each commit.
     * 
     * This method is thread-safe, it can be called from a callback.
     */
    virtual void discard() = 0;

  protected:

    transaction() = default;

  private:

    // non-copyable class
    transaction(const transaction&) = delete;
    transaction& operator=(const transaction&) = delete;
    // non-movable class
    transaction(transaction&&) = delete;
    transaction& operator=(transaction&&) = delete;
};

using tx_ptr = std::shared_ptr<transaction>;

} // namespace nplex
