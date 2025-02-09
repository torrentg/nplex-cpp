#include <chrono>
#include <cassert>
#include "nplex-cpp/exception.hpp"
#include "messages.hpp"
#include "cache.hpp"

namespace {

using namespace nplex;

gto::cstring create_cstring(const flatbuffers::Vector<std::uint8_t> *value) {
    return gto::cstring{reinterpret_cast<const char *>(value->data()), static_cast<std::size_t>(value->size())};
}

}; // unnamed namespace

nplex::meta_ptr nplex::cache_t::create_meta(const msgs::Update *updmsg)
{
    gto::cstring user;
    rev_t rev = updmsg->rev();
    auto user_it = m_users.find(updmsg->user()->c_str());

    if (user_it == m_users.end())
    {
        user = updmsg->user()->c_str();
        m_users.emplace(user, 1);
    }
    else
    {
        user = user_it->first;
        user_it->second++;
    }

    millis_t timestamp = std::chrono::milliseconds{updmsg->timestamp()};

    return std::make_shared<meta_t>(meta_t{rev, user, timestamp, updmsg->type(), 0});
}

void nplex::cache_t::release_meta(const meta_ptr &meta)
{
    if (meta->nrefs > 0)
        meta->nrefs--;

    if (meta->nrefs == 0)
    {
        auto it = m_users.find(meta->user);

        if (it != m_users.end())
        {
            if (it->second > 1)
                it->second--;
            else
                m_users.erase(it);
        }

        m_metas.erase(meta->rev);
    }
}

nplex::change_t nplex::cache_t::upsert_entry(const char *key, const value_ptr &value)
{
    assert(value && value->m_meta);

    if (!is_valid_key(key))
        throw nplex_exception("Trying to upsert an invalid key: {}", key);

    change_t change = {change_t::action_e::UPDATE, nullptr, nullptr, nullptr};
    auto it = m_data.find(key);

    if (it != m_data.end())
    {
        change.action = change_t::action_e::UPDATE;
        change.key = it->first;
        change.value = value;
        change.old_value = it->second;

        release_meta(it->second->m_meta);

        it->second = value;
    }
    else
    {
        nplex::key_t ckey = key;

        change.action = change_t::action_e::CREATE;
        change.key = ckey;
        change.value = value;
        change.old_value = nullptr;

        m_data[ckey] = value;
    }

    value->m_meta->nrefs++;

    return change;
}

nplex::change_t nplex::cache_t::delete_entry(const char *key)
{
    if (!is_valid_key(key))
        throw nplex_exception("Trying to delete an invalid key: {}", key);

    change_t change = {change_t::action_e::DELETE, nullptr, nullptr, nullptr};
    auto it = m_data.find(key);

    if (it == m_data.end()) {
        return change;
    }

    change.key = it->first;
    change.value = it->second;
    change.old_value = it->second;

    release_meta(it->second->m_meta);

    m_data.erase(it);

    return change;
}

void nplex::cache_t::load(const msgs::Snapshot *snapshot)
{
    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    m_rev = 0;
    m_data.clear();
    m_metas.clear();
    m_users.clear();

    if (!snapshot)
        return;

    auto updates = snapshot->updates();

    if (updates)
    {
        for (flatbuffers::uoffset_t i = 0; i < updates->size(); i++)
        {
            update(updates->Get(i));

            if (m_rev > snapshot->rev())
                throw nplex_exception("Snapshot at r{} contains entries at r{}", snapshot->rev(), m_rev);
        }
    }

    m_rev = snapshot->rev();
}

std::vector<change_t> nplex::cache_t::update(const msgs::Update *updmsg)
{
    if (!updmsg) {
        assert(false);
        return {};
    }

    std::vector<change_t> changes;
    auto upserts = updmsg->upserts();
    auto deletes = updmsg->deletes();

    changes.reserve((upserts ? upserts->size() : 0) + (deletes ? deletes->size() : 0));

    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    rev_t rev = updmsg->rev();

    if (rev <= m_rev)
        throw nplex_exception("Received an update to r{} when cache is at r{}", rev, m_rev);

    auto meta = create_meta(updmsg);

    m_rev = rev;

    if (upserts)
    {
        for (flatbuffers::uoffset_t i = 0; i < upserts->size(); i++)
        {
            auto keyval = upserts->Get(i);

            if (!keyval || !keyval->key() || !keyval->value())
                throw nplex_exception("Malformed update message at r{}", rev);

            auto key = keyval->key()->c_str();
            gto::cstring data = create_cstring(keyval->value());
            auto value = std::make_shared<value_t>(data, meta);

            auto change = upsert_entry(key, value);

            if (change.key)
                changes.push_back(change);
        }
    }

    if (deletes)
    {
        for (flatbuffers::uoffset_t i = 0; i < deletes->size(); i++)
        {
            auto key = deletes->Get(i);

            if (!key || !key->c_str())
                throw nplex_exception("Malformed update message at r{}", rev);

            auto change = delete_entry(key->c_str());

            if (change.key)
                changes.push_back(change);
        }
    }

    if (meta->nrefs)
        m_metas[rev] = meta;

    return changes;
}
