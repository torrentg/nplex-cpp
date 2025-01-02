#pragma once

#include <map>
#include <set>
#include "types.hpp"

namespace nplex {

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
    using value_ptr = std::shared_ptr<value_t>;
    using meta_ptr = std::shared_ptr<meta_t>;

    rev_t m_rev = 0;
    std::mutex m_mutex{};
    std::map<key_t, value_ptr, key_cmp_less_t> m_data{};
    std::map<rev_t, meta_ptr> m_meta{};
    std::set<gto::cstring> m_users{};

    // void update(const snapshot_t &snapshot);
    // void commit(const transaction_t &transaction);
};

}; // namespace nplex
