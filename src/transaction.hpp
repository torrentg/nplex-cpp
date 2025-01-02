#pragma once

#include <map>
#include <tuple>
#include <functional>
#include <string_view>
#include "types.hpp"

namespace nplex {

// Forward declaration.
struct cache_t;

/**
 * Database access can only be done through a transaction, which allows you to operate
 * according to an isolation level.
 * 
 * Transactions are updated on every database commit. Use the dirty() flag to check for
 * conflicts between the isolation level and the database update.
 * 
 * Isolation levels:
 *
 *   read-committed: Read always most recent data (value = always last rev).
 *     - Pros: Minimal overhead; high performance.
 *     - Cons: Leads to non-repeatable reads, and phantoms.
 *     - Note: Invalidated if any new transaction alters the c-ud and 'checked'' keys.
 *  
 *   repeatable-reads: Read data will not change during the transaction (value = 1st read).
 *     - Pros: Prevents non-repeatable reads.
 *     - Cons: Does not prevent phantoms.
 *     - Note: Invalidated if any new transaction alters the crud keys.
 *  
 *   serializable: All read data will not change during the transaction (value = tx creation).
 *     - Pros: Prevents non-repeatable reads, and phantoms.
 *     - Cons: Higher overhead.
 *     - Note: Invalidated if any new transaction alters the crud keys, or crud a modified key.
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

    enum class state_e {
        ONGOING,                                //!< Transaction is ongoing (user is fetching data).
        SUBMITTED,                              //!< Transaction was submitted to the server.
        ACCEPTED,                               //!< Transaction was accepted by the server (pending to receive the commit).
        COMMITTED,                              //!< Transaction was committed.
        REJECTED,                               //!< Transaction was rejected by the server.
        DISCARDED                               //!< Transaction was discarded by the user.
    };

    enum class action_e {
        CREATE,                                 //!< Create a key-value.
        READ,                                   //!< Read a key-value.
        UPDATE,                                 //!< Update a key-value.
        DELETE,                                 //!< Remove a key-value.
        CHECK                                   //!< At commit-time, checks that the key-value was not modified.
    };

    enum class isolation_e {
        READ_COMMITTED,                         //!< Read always most recent data.
        REPEATABLE_READS,                       //!< Read data will not change during the transaction.
        SERIALIZABLE                            //!< All read data will not change during the transaction.
    };

    using callback_t = std::function<bool(const gto::cstring &key, const value_t &value)>;

  private:

    using cache_ptr = std::shared_ptr<cache_t>;
    using entry_t = std::tuple<value_t, action_e>;
    using items_t = std::map<key_t, entry_t, key_cmp_less_t>;

    rev_t m_rev;                                //!< Database revision at tx creation.
    std::mutex m_mutex;                         //!< Lock for the transaction.
    cache_ptr m_data;                           //!< Database content.
    items_t m_items;                            //!< Transaction items (depends on isolation level).
    uint32_t m_type = 0;                        //! Transaction type (user-defined value).
    isolation_e m_isolation_level;              //!< Transaction isolation level.
    state_e m_state;                            //!< Transaction state.
    bool m_dirty = false;                       //!< Current tx conflicts with a commit.
    bool m_read_only = true;                    //!< Read-only flag.

  public:

    /**
     * Create a new transaction.
     * 
     * @param[in] data The database to use.
     * @param[in] isolation The isolation level to use.
     * @param[in] read_only If true, the transaction is read-only.
     * 
     * @exception std::invalid_argument Thrown if data is invalid.
     */
    transaction_t(cache_ptr data, isolation_e isolation, bool read_only);

    state_e state() const { return m_state; }
    isolation_e isolation() const { return m_isolation_level; }
    bool read_only() const { return m_read_only; }
    bool dirty() const { return m_dirty; }
    uint32_t type() const { return m_type; }
    void type(uint32_t type) { this->m_type = type; }

    /**
     * Create a new key-value.
     * 
     * If the key-value already exists, the value will be updated.
     * 
     * @param[in] key The key to create or override.
     * @param[in] value The value associated with the key.
     * 
     * @return true if the operation is successful,
     *         false if the transaction is read-only, or if the key or value is invalid.
     */
    bool create(const key_t &key, const std::string_view &value);

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
     * @see check()
     * 
     * @param[in] key The key to read.
     * @param[in] check If true, checks at commit time that the key-value pair was not modified.
     * 
     * @return The value associated with the key.
     * @exception std::out_of_range Thrown if the key is not found.
     */
    value_t read(const key_t &key, bool check = false);

    /**
     * Update a key-value pair.
     * 
     * By default does nothing if the value is unchanged. 
     * Use 'force' flag to update the revision.
     * 
     * @param[in] key The key to update.
     * @param[in] value The new value associated with the key.
     * @param[in] force Update revision even if value is unchanged.
     * 
     * @return true if the operation is successful,
     *         false if the transaction is read-only, or the key does not exist, or if the key or value is invalid.
     */
    bool update(const key_t &key, const std::string_view &value, bool force = false);

    /**
     * Remove a key-value pair.
     * 
     * Glob patterns are supported (ex: '/users\/*\/error', '/users/jdoe\/**').
     * When using a pattern, the operation is applied to all matching keys.
     * 
     * Caution, deletion using a pattern does not grant that all keys was removed at commit-time. 
     * For example, if you delete a range of values using a pattern, and then a commit adds 
     * a key satisfying the pattern, this new key continue to exists even if the current 
     * transaction is committed succesfully.
     * 
     * @see check()
     * 
     * @param[in] key The key or pattern to remove.
     * 
     * @return true if the operation is successful,
     *         false if the transaction is read-only, or the key does not exist.
     */
    bool remove(const key_t &key);
    bool remove(const char *pattern);

    /**
     * Condition to be checked at commit-time.
     * The transaction will be rejected if the condition is not satisfied.
     * Transaction condition checks applies even in 'force' mode.
     * 
     * Glob patterns are supported (ex: '/users\/*\/error', '/users/jdoe\/**').
     * 
     * Deleting, creating, or updating a set of key-values matching a pattern does not 
     * guarantee that you delete, create, or update all database entries matching this 
     * pattern at commit time. This is because an intermediate commit may add, delete, 
     * or update an entry satisfying that pattern. To avoid these cases, you can add a 
     * check to validate that the condition is fulfilled.
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
     *     tx.check("/users/jdoe/name", UPDATE | DELETE);
     * 
     * @example: Verify that nobody altered a set of keys.
     *     tx.check("/users/**", CREATE | UPDATE | DELETE);
     *
     * @param[in] pattern The pattern to check.
     * @param[in] actions The actions to check (1=CREATE, 2=UPDATE, 4=DELETE).
     * 
     * @return true if the condition was set, 
     *         false otherwise (invalid-pattern, unrecognized-action).
     */
    bool check(const char *pattern, uint8_t actions);

    /**
     * Executes the callback function for each key-value.
     * 
     * Each value is read according to the isolation level.
     * 
     * During iteration the database is locked (no external commits).
     * Iteration stops on the first false return value.
     * Iteration does not fails on addition or removal.
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
     *  tx.for_each("/users\/*\/name", "/path\/*\/name", [&id](const gto::cstring &key, const value_t &value) {
     *      if (value.data().contains("mr_hacker")) {
     *          id = key;
     *          return false;
     *      }
     *      return true;
     *  });
     * 
     * @example: Alter database content.
     *  tx.for_each("/users\/*\/name", "/path\/*\/name", [&tx](const gto::cstring &key, const value_t &value) {
     *      if (value.data().contains("mr_hacker"))
     *          tx.delete(key);
     *      return true;
     *  });
     * 
     * @param[in] from Key pattern (included).
     * @param[in] to Key pattern (excluded).
     * @param[in] callback The callback function to execute. This function receives the key-value pair.
     *                     It must return true to continue the iteration, or false to stop it.
     * 
     * @return Number of iterated elements.
     */
    size_t for_each(callback_t callback);
    size_t for_each(const std::string_view &from, const std::string_view &to, callback_t callback);
};

}; // namespace nplex
