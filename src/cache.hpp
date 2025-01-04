#pragma once

#include <map>
#include <set>
#include <mutex>
#include <vector>
#include "types.hpp"

namespace nplex {

// Forward declarations
namespace msgs {
    class Snapshot;
    class Transaction;
}

/**
 * In-memory database content.
 * 
 * This is an internal class whose contents are updated by the server.
 * 
 * The server streams commits to maintain the clients' cache in sync.
 * This structure holds the in-memory representation of the database,
 * including the current revision, data and transactions metadata.
 */
struct cache_t
{
    rev_t m_rev = 0;
    std::recursive_mutex m_mutex{};
    std::map<key_t, value_ptr, key_cmp_less_t> m_data{};
    std::map<rev_t, meta_ptr> m_metas{};
    std::set<gto::cstring> m_users{};

    /**
     * Restore the database content from a snapshot.
     * 
     * On exception, the database is left in an inconsistent state.
     * 
     * @param[in] snapshot Content to restore.
     * 
     * @exception nplex_exception Invalid snapshot.
     */
    void restore(const msgs::Snapshot *snapshot);

    /**
     * Apply a commit to the database.
     * 
     * On exception, the database is left in an inconsistent state.
     * 
     * @param[in] transaction Transaction to apply.
     * 
     * @return List of applied changes.
     * 
     * @exception nplex_exception Invalid transaction.
     */
    std::vector<change_t> update(const msgs::Transaction *transaction);
};

}; // namespace nplex
