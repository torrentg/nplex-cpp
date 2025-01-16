#pragma once

#include <map>
#include <tuple>
#include <atomic>
#include "nplex-cpp/transaction.hpp"

namespace nplex {

// Forward declaration
struct cache_t;

using cache_ptr = std::shared_ptr<cache_t>;

/**
 * Internal class implementing the transaction_t interface.
 * Can be freely manipulated by client_impl_t.
 */
struct transaction_impl_t : public transaction_t
{
    enum class action_e : std::uint8_t {
        READ,                                   //!< Read a key-value.
        UPSERT,                                 //!< Update or insert a key-value.
        DELETE                                  //!< Remove a key-value.
    };

    using entry_t = std::tuple<action_e, value_ptr>;
    using items_t = std::map<key_t, entry_t, key_cmp_less_t>;
    using ensures_t = std::map<std::string, std::uint8_t>;

    rev_t m_rev;                                //!< Database revision at tx creation.
    cache_ptr m_cache;                          //!< Database content.
    items_t m_items;                            //!< Transaction items (depends on isolation level).
    ensures_t m_ensures;                        //!< Transaction ensures.
    isolation_e m_isolation_level;              //!< Transaction isolation level.
    std::atomic<std::uint32_t> m_type = 0;      //!< Transaction type (user-defined value).
    std::atomic<state_e> m_state;               //!< Transaction state.
    std::atomic<bool> m_dirty = false;          //!< Current tx conflicts with a commit.
    bool m_read_only = true;                    //!< Read-only flag.

    transaction_impl_t(cache_ptr cache, isolation_e isolation, bool read_only);
    nplex::value_ptr read(const char *key, bool check);
    bool upsert(const char *key, const std::string_view &data, bool force);
    bool remove(const key_t &key);
    std::size_t remove(const char *pattern);
    bool ensure(const char *pattern, std::uint8_t actions);
    std::size_t for_each(const char *pattern, const callback_t &callback);
    void update(const std::vector<change_t> &changes);
    void update_serializable(const std::vector<change_t> &changes);
    void update_default(const std::vector<change_t> &changes);
};

inline nplex::transaction_impl_t * get_impl(nplex::transaction_t *obj) {
    return reinterpret_cast<nplex::transaction_impl_t *>(obj);
}

inline const nplex::transaction_impl_t * get_impl(const nplex::transaction_t *obj) {
    return reinterpret_cast<const nplex::transaction_impl_t *>(obj);
}

/**
 * Create a new transaction.
 * Used internally by client_t to create a new transaction.
 * 
 * @param[in] cache The database to use.
 * @param[in] isolation The isolation level to use.
 * @param[in] read_only If true, the transaction is read-only.
 * 
 * @return A pointer to the new transaction.
 * 
 * @exception std::invalid_argument Thrown if data is invalid.
 */
std::shared_ptr<transaction_t> make_transaction(cache_ptr cache, transaction_t::isolation_e isolation, bool read_only);

} // namespace nplex
