#include <cstring>
#include "match.h"
#include "cache.hpp"
#include "exception.hpp"
#include "transaction.hpp"

nplex::transaction_t::transaction_t(cache_ptr cache, isolation_e isolation, bool read_only) : 
    m_cache{cache}, m_isolation_level{isolation}, m_read_only{read_only}
{
    if (!m_cache)
        throw std::invalid_argument("Invalid database");

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    m_rev = m_cache->m_rev;
    m_state = state_e::OPEN;
}

nplex::value_ptr nplex::transaction_t::read(const key_t &key, bool check)
{
    if (!is_valid_key(key))
        throw std::invalid_argument("Invalid key");

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
            transaction_t::check(key.data(), CHECK_CREATE | CHECK_UPDATE | CHECK_DELETE);

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
        transaction_t::check(key.data(), CHECK_CREATE | CHECK_UPDATE | CHECK_DELETE);

    return it_cache->second;
}

bool nplex::transaction_t::upsert(const key_t &key, const std::string_view &data, bool force)
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

bool nplex::transaction_t::remove(const key_t &key)
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

std::size_t nplex::transaction_t::remove(const char *pattern)
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

std::size_t nplex::transaction_t::for_each(const char *pattern, callback_t callback)
{
    if (!callback)
        throw nplex_exception("Invalid argument");

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

bool nplex::transaction_t::check(const char *pattern, std::uint8_t actions)
{
    actions &= (CHECK_CREATE | CHECK_UPDATE | CHECK_DELETE);

    if (!pattern || !actions)
        return false;

    std::lock_guard<decltype(m_cache->m_mutex)> lock_cache(m_cache->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    auto it = m_checks.find(pattern);

    if (it == m_checks.end())
        m_checks.emplace(pattern, actions);
    else
        it->second |= actions;

    return true;
}

void nplex::transaction_t::update(const std::vector<change_t> &changes)
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

    if (m_dirty || m_checks.empty())
        return;

    // Checking validations
    for (const auto &change : changes)
    {
        for (const auto &item : m_checks)
        {
            if (!glob_match(change.key.data(), item.first.data()))
                continue;

            if (((item.second & CHECK_CREATE) && change.action == change_t::action_e::CREATE) ||
                ((item.second & CHECK_UPDATE) && change.action == change_t::action_e::UPDATE) ||
                ((item.second & CHECK_DELETE) && change.action == change_t::action_e::DELETE)) {
                m_dirty = true;
                break;
            }
        }
    }
}

void nplex::transaction_t::update_default(const std::vector<change_t> &changes)
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

void nplex::transaction_t::update_serializable(const std::vector<change_t> &changes)
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
