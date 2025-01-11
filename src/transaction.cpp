#include <map>
#include <tuple>
#include <atomic>
#include <cstring>
#include "match.h"
#include "cache.hpp"
#include "exception.hpp"
#include "transaction.hpp"

// =================================================================================================
// transaction_impl_t declaration
// =================================================================================================

namespace nplex {

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

} // namespace nplex

// =================================================================================================
// transaction_impl_t methods
// =================================================================================================

nplex::transaction_impl_t::transaction_impl_t(cache_ptr cache, isolation_e isolation, bool read_only) : 
    m_cache{std::move(cache)}, m_isolation_level{isolation}, m_read_only{read_only}
{
    if (!m_cache)
        throw std::invalid_argument("Invalid database");

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    m_rev = m_cache->m_rev;
    m_state = state_e::OPEN;
}

nplex::value_ptr nplex::transaction_impl_t::read(const char *key, bool check)
{
    if (!is_valid_key(key))
        throw nplex_exception("Trying to read an invalid key: {}", key);

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    // Search for the key in the transaction
    auto it_tx = m_items.find(key);

    // Case: key was previously read, upserted or deleted in this transaction
    if (it_tx != m_items.end())
    {
        if (std::get<action_e>(it_tx->second) == action_e::DELETE)
            return {};

        if (check)
            transaction_t::ensure(key, NPLEX_CREATE | NPLEX_UPDATE | NPLEX_DELETE);

        return std::get<value_ptr>(it_tx->second);
    }

    // Search for the key in the database
    auto it_cache = m_cache->m_data.find(key);

    // Case: key does not exist in the database
    if (it_cache == m_cache->m_data.end())
        return {};

    // Case: key exists in the database
    if (m_isolation_level != transaction_t::isolation_e::READ_COMMITTED)
        m_items.emplace(it_cache->first, std::make_tuple(action_e::READ, it_cache->second));

    if (check)
        transaction_t::ensure(key, NPLEX_CREATE | NPLEX_UPDATE | NPLEX_DELETE);

    return it_cache->second;
}

bool nplex::transaction_impl_t::upsert(const char *key, const std::string_view &data, bool force)
{
    if (!is_valid_key(key) || data.empty())
        throw std::invalid_argument("Invalid key or invalid data");

    auto value = std::make_shared<value_t>(
        gto::cstring{data.data(), data.size()}, 
        meta_ptr{}  // Metadata is empty because the current transaction was not committed
    );

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");
    if (m_read_only)
        throw nplex_exception("Transaction is read-only");

    // Search for the key in the transaction
    auto it_tx = m_items.find(key);

    // Case: key was previously read, upserted or deleted in this transaction
    if (it_tx != m_items.end())
    {
        // Case: Same value and not forced
        if (!force && 
            std::get<action_e>(it_tx->second) != action_e::DELETE && 
            std::get<value_ptr>(it_tx->second)->data() == value->data())
            return false;

        // Case: distinct value (or force mode)
        std::get<action_e>(it_tx->second) = action_e::UPSERT;
        std::get<value_ptr>(it_tx->second) = value;
        return true;
    }

    // Search for the key in the database
    auto it_cache = m_cache->m_data.find(key);

    // Case: key does not exist in the database
    if (it_cache == m_cache->m_data.end()) {
        m_items.emplace(key, std::make_tuple(action_e::UPSERT, value));
        return true;
    }

    // Case: key exists in database AND has the same value
    if (!force && it_cache->second->data() == value->data())
        return false;

    // Case: key exists in the database with distinct value (or force mode)
    m_items.emplace(it_cache->first, std::make_tuple(action_e::UPSERT, value));
    return true;
}

bool nplex::transaction_impl_t::remove(const key_t &key)
{
    if (!is_valid_key(key))
        throw std::invalid_argument("Invalid key");

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");
    if (m_read_only)
        throw nplex_exception("Transaction is read-only");

    // Search for the key in the transaction
    auto it_tx = m_items.find(key);

    // Case: key was previously read, upserted or deleted in this transaction
    if (it_tx != m_items.end())
    {
        // Case: Previously removed
        if (std::get<action_e>(it_tx->second) == action_e::DELETE)
            return false;

        // Case: Previously read or upserted
        std::get<action_e>(it_tx->second) = action_e::DELETE;
        std::get<value_ptr>(it_tx->second) = nullptr;
        return true;
    }

    // Search for the key in the database
    auto it_cache = m_cache->m_data.find(key);

    // Case: key does not exist in the database
    if (it_cache == m_cache->m_data.end())
        return false;

    // Case: key exists in the database
    m_items.emplace(it_cache->first, std::make_tuple(action_e::DELETE, nullptr));
    return true;
}

std::size_t nplex::transaction_impl_t::remove(const char *pattern)
{
    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");
    if (m_read_only)
        throw nplex_exception("Transaction is read-only");

    auto ret = for_each(pattern, [this](const gto::cstring &key, [[maybe_unused]] const value_t &value) {
        remove(key);
        return true;
    });

    return ret;
}

bool nplex::transaction_impl_t::ensure(const char *pattern, std::uint8_t actions)
{
    actions &= (NPLEX_CREATE | NPLEX_UPDATE | NPLEX_DELETE);

    if (!pattern || !actions)
        return false;

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    auto it = m_ensures.find(pattern);

    if (it == m_ensures.end())
        m_ensures.emplace(pattern, actions);
    else
        it->second |= actions;

    return true;
}

std::size_t nplex::transaction_impl_t::for_each(const char *pattern, const callback_t &callback)
{
    if (!callback)
        throw std::invalid_argument("Invalid callback function");

    if (!pattern || *pattern == '\0')
        return 0;

    std::string_view prefix = std::string_view{pattern, strcspn(pattern, "*?")};
    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    std::size_t ret = 0;
    auto it_tx = (prefix.empty() ? m_items.begin() : m_items.lower_bound(prefix));
    auto it_tx_end = m_items.end();
    auto it_cache = (prefix.empty() ? m_cache->m_data.begin() : m_cache->m_data.lower_bound(prefix));
    auto it_cache_end = m_cache->m_data.end();

    while (it_tx != it_tx_end || it_cache != it_cache_end)
    {
        while (it_tx != it_tx_end)
        {
            if (!it_tx->first.starts_with(prefix))
                it_tx = it_tx_end;
            else if (glob_match(it_tx->first.data(), pattern))
                break;
            else
                it_tx++;
        }

        while (it_cache != it_cache_end)
        {
            auto xxx = it_cache->first;
            if (!it_cache->first.starts_with(prefix))
                it_cache = it_cache_end;
            else if (glob_match(it_cache->first.data(), pattern))
                break;
            else
                it_cache++;
        }

        if (it_tx != it_tx_end && it_cache != it_cache_end)
        {
            if (it_tx->first < it_cache->first)
            {
                if (std::get<action_e>(it_tx->second) != action_e::DELETE)
                {
                    if (ret++, !callback(it_tx->first, *std::get<value_ptr>(it_tx->second)))
                        break;
                }

                it_tx++;
            }
            else if (it_cache->first < it_tx->first)
            {
                if (ret++, !callback(it_cache->first, *it_cache->second))
                    break;

                it_cache++;
            }
            else
            {
                if (std::get<action_e>(it_tx->second) != action_e::DELETE)
                {
                    if (ret++, !callback(it_tx->first, *std::get<value_ptr>(it_tx->second)))
                        break;
                }

                it_tx++;
                it_cache++;
            }
        }
        else if (it_tx != it_tx_end)
        {
            if (std::get<action_e>(it_tx->second) != action_e::DELETE)
            {
                if (ret++, !callback(it_tx->first, *std::get<value_ptr>(it_tx->second)))
                    break;
            }

            it_tx++;
        }
        else if (it_cache != it_cache_end)
        {
            if (ret++, !callback(it_cache->first, *it_cache->second))
                break;

            it_cache++;
        }
    }

    return ret;
}

void nplex::transaction_impl_t::update(const std::vector<change_t> &changes)
{
    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN) {
        assert(false);
        return;
    }

    switch (m_isolation_level)
    {
        case isolation_e::SERIALIZABLE:
            update_serializable(changes);
            break;
        case isolation_e::READ_COMMITTED:
        case isolation_e::REPEATABLE_READS:
            update_default(changes);
            break;
    }

    if (m_dirty || m_ensures.empty())
        return;

    // Checking validations
    for (const auto &change : changes)
    {
        for (const auto &ensure : m_ensures)
        {
            if (!glob_match(change.key.data(), ensure.first.data()))
                continue;

            if (((ensure.second & NPLEX_CREATE) && change.action == change_t::action_e::CREATE) ||
                ((ensure.second & NPLEX_UPDATE) && change.action == change_t::action_e::UPDATE) ||
                ((ensure.second & NPLEX_DELETE) && change.action == change_t::action_e::DELETE)) {
                m_dirty = true;
                break;
            }
        }
    }
}

void nplex::transaction_impl_t::update_default(const std::vector<change_t> &changes)
{
    if (m_dirty)
        return;

    for (const auto &change : changes)
    {
        auto it_tx = m_items.find(change.key);

        // Case: key does not exist in the transaction
        if (it_tx == m_items.end())
            continue;

        // Case: key exists in the transaction
        m_dirty = true;
        break;
    }
}

void nplex::transaction_impl_t::update_serializable(const std::vector<change_t> &changes)
{
    for (const auto &change : changes)
    {
        auto it_tx = m_items.find(change.key);

        // Case: key does not exist in the transaction
        if (it_tx == m_items.end())
        {
            switch (change.action)
            {
                case change_t::action_e::CREATE:
                    m_items.emplace(change.key, std::make_tuple(action_e::READ, change.value));
                    break;
                case change_t::action_e::UPDATE:
                    m_items.emplace(change.key, std::make_tuple(action_e::READ, change.old_value));
                    break;
                case change_t::action_e::DELETE:
                    m_items.emplace(change.key, std::make_tuple(action_e::READ, change.value));
                    break;
            }

            continue;
        }

        // Case: key exists in the transaction
        m_dirty = true;
    }
}

// =================================================================================================
// transaction_t methods
// =================================================================================================

namespace {

    inline nplex::transaction_impl_t * get_impl(nplex::transaction_t *obj) {
        return reinterpret_cast<nplex::transaction_impl_t *>(obj);
    }

    inline const nplex::transaction_impl_t * get_impl(const nplex::transaction_t *obj) {
        return reinterpret_cast<const nplex::transaction_impl_t *>(obj);
    }

} // unnamed namespace

std::shared_ptr<nplex::transaction_t> nplex::transaction_t::create(cache_ptr cache, isolation_e isolation, bool read_only)
{
    auto impl = std::make_shared<transaction_impl_t>(cache, isolation, read_only);
    return std::reinterpret_pointer_cast<transaction_t>(impl);
}

nplex::transaction_t::isolation_e nplex::transaction_t::isolation() const { return get_impl(this)->m_isolation_level; }
bool nplex::transaction_t::read_only() const { return get_impl(this)->m_read_only; }
nplex::transaction_t::state_e nplex::transaction_t::state() const { return get_impl(this)->m_state; }
void nplex::transaction_t::state(state_e state) { get_impl(this)->m_state = state; }
bool nplex::transaction_t::dirty() const { return get_impl(this)->m_dirty; }
void nplex::transaction_t::dirty(bool dirty) { get_impl(this)->m_dirty = dirty; }
std::uint32_t nplex::transaction_t::type() const { return get_impl(this)->m_type; }
void nplex::transaction_t::type(std::uint32_t type) { get_impl(this)->m_type = type; }
nplex::value_ptr nplex::transaction_t::read(const char *key, bool check) { return get_impl(this)->read(key, check); }
bool nplex::transaction_t::upsert(const char *key, const std::string_view &data, bool force) { return get_impl(this)->upsert(key, data, force); }
bool nplex::transaction_t::remove(const key_t &key) { return get_impl(this)->remove(key); }
std::size_t nplex::transaction_t::remove(const char *pattern) { return get_impl(this)->remove(pattern); }
bool nplex::transaction_t::ensure(const char *pattern, std::uint8_t actions) { return get_impl(this)->ensure(pattern, actions); }
std::size_t nplex::transaction_t::for_each(const char *pattern, const callback_t &callback) { return get_impl(this)->for_each(pattern, callback); }
void nplex::transaction_t::update(const std::vector<change_t> &changes) { get_impl(this)->update(changes); }
