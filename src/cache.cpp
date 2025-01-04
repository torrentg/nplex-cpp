#include <chrono>
#include <cassert>
#include "exception.hpp"
#include "messages.hpp"
#include "cache.hpp"

// g++ -Wall -Wextra -Wshadow  -Wconversion -std=c++20 -I../deps -c cache.cpp

namespace {

using namespace nplex;

/**
 * Creates a transaction metadata object and inserts it into the cache.
 * Caution, internal function not guarded by the mutex.
 * 
 * @param[in] cache Cache to update.
 * @param[in] transaction Transaction to process.
 * @return The inserted metadata.
 */
meta_ptr create_meta(cache_t &cache, const msgs::Transaction *transaction)
{
    gto::cstring user;
    rev_t rev = transaction->rev();
    auto user_it = cache.m_users.find(transaction->user()->c_str());

    if (user_it == cache.m_users.end())
    {
        user = transaction->user()->c_str();
        cache.m_users.insert(user);
    }
    else
    {
        user = *user_it;
    }

    millis_t timestamp = std::chrono::milliseconds{transaction->timestamp()};
    auto meta = std::make_shared<meta_t>((meta_t){rev, user, timestamp, transaction->type()});
    cache.m_metas[rev] = meta;

    return meta;
}

/**
 * Apply a transaction to the cache without tracking changes.
 * Caution, internal function not guarded by the mutex.
 * 
 * @param[in] cache Cache to update.
 * @param[in] transaction Transaction to apply.
 * 
 * @exception nplex_exception Invalid transaction.
 */
void apply_tx(cache_t &cache, const msgs::Transaction *transaction)
{
    if (transaction->rev() <= cache.m_rev)
        throw nplex_exception("invalid transaction revision");

    rev_t rev = transaction->rev();
    auto meta = create_meta(cache, transaction);

    auto upserts = transaction->upserts();

    for (flatbuffers::uoffset_t i = 0; i < upserts->size(); i++)
    {
        auto keyval = upserts->Get(i);

        gto::cstring val{reinterpret_cast<const char *>(keyval->value()->data()), static_cast<size_t>(keyval->value()->size())};
        auto value = std::make_shared<value_t>(val, meta);

        auto it = cache.m_data.find(keyval->key()->c_str());
        if (it != cache.m_data.end())
            it->second = value;
        else
            cache.m_data[keyval->key()->c_str()] = value;
    }

    auto deletes = transaction->deletes();

    for (flatbuffers::uoffset_t i = 0; i < deletes->size(); i++)
        cache.m_data.erase(deletes->Get(i)->c_str());

    cache.m_rev = rev;
}

}; // unnamed namespace

void nplex::cache_t::restore(const msgs::Snapshot *snapshot)
{
    assert(snapshot);
    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    m_rev = 0;
    m_data.clear();
    m_metas.clear();
    m_users.clear();

    auto transactions = snapshot->transactions();

    for (flatbuffers::uoffset_t i = 0; i < transactions->size(); i++)
        apply_tx(*this, transactions->Get(i));

    m_rev = snapshot->rev();
}

std::vector<change_t> nplex::cache_t::update(const msgs::Transaction *transaction)
{
    assert(transaction);

    std::vector<change_t> changes;
    auto upserts = transaction->upserts();
    auto deletes = transaction->deletes();

    changes.reserve(upserts->size() + deletes->size());

    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    if (transaction->rev() <= m_rev)
        throw nplex_exception("invalid transaction revision");

    auto meta = create_meta(*this, transaction);

    m_rev = transaction->rev();

    for (flatbuffers::uoffset_t i = 0; i < upserts->size(); i++)
    {
        auto keyval = upserts->Get(i);

        gto::cstring val{reinterpret_cast<const char *>(keyval->value()->data()), static_cast<size_t>(keyval->value()->size())};
        auto value = std::make_shared<value_t>(val, meta);

        auto it = m_data.find(keyval->key()->c_str());

        if (it != m_data.end())
        {
            change_t change;

            change.action = change_t::action_e::UPDATE;
            change.key = it->first;
            change.old_value = it->second;
            change.value = value;
            changes.push_back(change);

            it->second = value;
        }
        else
        {
            change_t change;
            key_t key = keyval->key()->c_str();

            change.action = change_t::action_e::CREATE;
            change.key = key;
            change.value = value;
            changes.push_back(change);

            m_data[key] = value;
        }
    }

    for (flatbuffers::uoffset_t i = 0; i < deletes->size(); i++)
    {
        auto it = m_data.find(deletes->Get(i)->c_str());

        if (it != m_data.end())
        {
            change_t change;

            change.action = change_t::action_e::DELETE;
            change.key = it->first;
            change.value = it->second;
            change.old_value = it->second;
            changes.push_back(change);

            m_data.erase(it);
        }
    }

    return changes;
}
