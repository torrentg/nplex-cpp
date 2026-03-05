#pragma once

#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include "nplex-cpp/types.hpp"

namespace nplex {

// Forward declarations
namespace msgs {
    struct Snapshot;
    struct Update;
}

/**
 * In-memory database content.
 * 
 * This is an internal class whose contents are updated by the server.
 * The user has not direct access to this class.
 * 
 * The server streams commits to maintain the clients' store in sync.
 * This structure holds the in-memory representation of the database,
 * including the current revision, data and transactions metadata.
 * 
 * @note This class is not copyable nor movable due to mutex.
 * @note This class is not thread-safe, the user must lock the mutex.
 */
struct store_t
{
    rev_t m_rev = 0;
    mutable std::recursive_mutex m_mutex;
    std::map<key_t, value_ptr, gto::cstring_compare> m_data;
    std::map<rev_t, meta_ptr> m_metas;
    std::map<gto::cstring, std::uint32_t, gto::cstring_compare> m_users; // value=num_refs

    /**
     * Return the current revision of the database.
     * 
     * Invoke this function from transactions (from client you can use safely m_rev).
     * 
     * @return Current revision.
     */
    rev_t rev() const;

    /**
     * Load the database content from a snapshot.
     * 
     * On exception, the database is left in an inconsistent state.
     * 
     * @param[in] snapshot Content to load (nullptr reset store).
     * 
     * @exception nplex_exception Invalid snapshot.
     */
    void load(const msgs::Snapshot *snapshot = nullptr);

    /**
     * Apply a commit to the database.
     * 
     * On exception, the database is left in an inconsistent state.
     * 
     * @param[in] updmsg Update to apply.
     * 
     * @return List of applied changes and the associated metadata.
     * 
     * @exception nplex_exception Invalid update (ex: update.rev < store.rev, or invalid-key).
     */
    std::pair<std::vector<change_t>, meta_ptr> update(const msgs::Update *updmsg);

  private:

    /**
     * Creates a transaction metadata object.
     * Updates metas and users store objects.
     * Caution, internal function not guarded by the mutex.
     * 
     * @param[in] updmsg Update to process.
     * 
     * @return The inserted metadata.
     */
    meta_ptr create_meta(const msgs::Update *updmsg);

    /**
     * Release a metadata decreasing the ref counters and removing 
     * entries in metas and users if required.
     * 
     * @param[in] meta Metadata to release.
     */
    void release_meta(const meta_ptr &meta);

    /**
     * Upsert an entry updating content accordingly.
     * 
     * @param[in] key Key to insert or update.
     * @param[in] value Value to set.
     * 
     * @return The change done.
     */
    change_t upsert_entry(const char *key, const value_ptr &value);

    /**
     * Delete an entry updating content accordingly.
     * 
     * @param[in] key Key to delete.
     * 
     * @return The change done (empty key if nothing removed).
     */
    change_t delete_entry(const char *key);

};

using store_ptr = std::shared_ptr<store_t>;

} // namespace nplex
