#include <cstring>
#include "match.h"
#include "nplex-cpp/exception.hpp"
#include "utils.hpp"
#include "messages.hpp"
#include "client_impl.hpp"
#include "transaction_impl.hpp"

std::atomic<std::uint64_t> nplex::transaction_impl::seq_id{1};

// ==========================================================
// Internal (static) functions
// ==========================================================

template<typename... Args>
void log(std::weak_ptr<nplex::client_impl> client, nplex::logger::log_level_e severity, fmt::format_string<Args...> fmt_str, Args&&... args) {
    if (auto cli = client.lock())
        cli->log(severity, fmt_str, std::forward<Args>(args)...);
}

template<typename... Args>
void log_trace(std::weak_ptr<nplex::client_impl> client, fmt::format_string<Args...> fmt_str, Args&&... args) {
    log(client, nplex::logger::log_level_e::TRACE, fmt_str, std::forward<Args>(args)...);
}

template<typename... Args>
void log_debug(std::weak_ptr<nplex::client_impl> client, fmt::format_string<Args...> fmt_str, Args&&... args) {
    log(client, nplex::logger::log_level_e::DEBUG, fmt_str, std::forward<Args>(args)...);
}

template<typename... Args>
void log_info(std::weak_ptr<nplex::client_impl> client, fmt::format_string<Args...> fmt_str, Args&&... args) {
    log(client, nplex::logger::log_level_e::INFO, fmt_str, std::forward<Args>(args)...);
}

template<typename... Args>
void log_warn(std::weak_ptr<nplex::client_impl> client, fmt::format_string<Args...> fmt_str, Args&&... args) {
    log(client, nplex::logger::log_level_e::WARN, fmt_str, std::forward<Args>(args)...);
}

template<typename... Args>
void log_error(std::weak_ptr<nplex::client_impl> client, fmt::format_string<Args...> fmt_str, Args&&... args) {
    log(client, nplex::logger::log_level_e::ERROR, fmt_str, std::forward<Args>(args)...);
}

// ==========================================================
// transaction_impl methods
// ==========================================================

nplex::transaction_impl::transaction_impl(client_impl_ptr client, store_ptr store, isolation_e isolation, bool read_only) : 
    m_id{seq_id++}, m_client{std::move(client)}, m_store{std::move(store)}, m_isolation_level{isolation}, 
    m_state{state_e::OPEN}, m_read_only{read_only}
{
    if (!m_store)
        throw std::invalid_argument("Invalid database");

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);
    m_rev_creation = m_store->m_rev;

    log_debug(m_client, "Tx {} created, isolation={}, read_only={}", m_id, to_str(isolation), read_only);
}

nplex::transaction_impl::~transaction_impl()
{
    discard();
    log_debug(m_client, "Tx {} destroyed", m_id);
}

void nplex::transaction_impl::discard()
{
    set_state(state_e::DISCARDED);

    try {
        m_promise.set_exception(
            std::make_exception_ptr(nplex_exception("Transaction was discarded"))
        );
    }
    catch (...) {
        // do nothing
    }
}

void nplex::transaction_impl::set_state(state_e state)
{
    if (is_closed() || state == m_state)
        return;

    log_debug(m_client, "Tx {} changed from {} to {}", m_id, to_str(m_state), to_str(state));

    m_state = state;

    if (!is_closed())
        return;

    if (auto client = m_client.lock())
        client->remove_tx(this);
}

nplex::rev_t nplex::transaction_impl::rev() const
{
    switch(m_isolation_level)
    {
        case isolation_e::REPEATABLE_READ:
        case isolation_e::READ_COMMITTED: {
            std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);
            return m_store->m_rev;
        }
        case isolation_e::SERIALIZABLE:
            return m_rev_creation;
        default:
            throw std::logic_error("Invalid isolation level");
    }
}

nplex::value_ptr nplex::transaction_impl::read(const char *key, bool check)
{
    if (!is_valid_key(key))
        throw nplex_exception("Trying to read an invalid key: {}", key);

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

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
            ensure(key);

        return std::get<value_ptr>(it_tx->second);
    }

    // Search for the key in the database
    auto it_store = m_store->m_data.find(key);

    // Case: key does not exist in the database
    if (it_store == m_store->m_data.end())
        return {};

    // Case: key exists in the database
    if (m_isolation_level != transaction::isolation_e::READ_COMMITTED)
        m_items.emplace(it_store->first, std::make_tuple(action_e::READ, it_store->second));

    if (check)
        ensure(key);

    return it_store->second;
}

bool nplex::transaction_impl::upsert(const char *key, const std::string_view &data, bool force)
{
    if (!is_valid_key(key) || data.empty())
        throw std::invalid_argument("Invalid key or invalid data");

    if (m_read_only)
        throw nplex_exception("Transaction is read-only");

    auto value = std::make_shared<value_t>(
        gto::cstring{data.data(), data.size()}, 
        meta_ptr{}  // Metadata is empty because the current transaction was not committed
    );

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    // Search for the key in the transaction
    auto it_tx = m_items.find(key);

    // Case: key was previously read, upserted or deleted in this transaction
    if (it_tx != m_items.end())
    {
        // TODO: check for permission (update)

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
    auto it_store = m_store->m_data.find(key);

    // Case: key does not exist in the database
    if (it_store == m_store->m_data.end()) {
        // TODO: check for permission (create)
        m_items.emplace(key, std::make_tuple(action_e::UPSERT, value));
        return true;
    }

    // TODO: check for permission (update)

    // Case: key exists in database AND has the same value
    if (!force && it_store->second->data() == value->data())
        return false;

    // Case: key exists in the database with distinct value (or force mode)
    m_items.emplace(it_store->first, std::make_tuple(action_e::UPSERT, value));
    return true;
}

bool nplex::transaction_impl::remove(const key_t &key)
{
    if (!is_valid_key(key))
        throw std::invalid_argument("Invalid key");

    if (m_read_only)
        throw nplex_exception("Transaction is read-only");

    // TODO: check permissions (delete)

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

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
    auto it_store = m_store->m_data.find(key);

    // Case: key does not exist in the database
    if (it_store == m_store->m_data.end())
        return false;

    // Case: key exists in the database
    m_items.emplace(it_store->first, std::make_tuple(action_e::DELETE, nullptr));
    return true;
}

std::size_t nplex::transaction_impl::remove(const char *pattern)
{
    if (m_read_only)
        throw nplex_exception("Transaction is read-only");

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    auto ret = for_each(pattern, [this](const key_t &key, [[maybe_unused]] const value_t &value) {
        remove(key);
        return true;
    });

    return ret;
}

bool nplex::transaction_impl::ensure(const char *pattern)
{
    if (!pattern)
        return false;

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    if (!m_ensures.contains(pattern))
        m_ensures.emplace(pattern);

    return true;
}

std::size_t nplex::transaction_impl::for_each(const char *pattern, const callback_t &callback)
{
    if (!callback || !pattern || *pattern == '\0')
        return 0;

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    // We need to store the results in a vector because the callback can 
    // modify the transaction (eg. upsert or delete), and this can 
    // invalidate the iterators.

    auto kvs = for_each_internal(pattern);
    size_t ret = 0;

    for (const auto &kv : kvs)
        if (ret++, !callback(kv.first, *kv.second))
            break;

    return ret;
}

/**
 * Internal method.
 * State is not checked and it is not guarded with a mutex.
 */
std::vector<std::pair<nplex::key_t, nplex::value_ptr>> nplex::transaction_impl::for_each_internal(const char *pattern)
{
    if (!pattern || *pattern == '\0')
        pattern = "**";

    std::vector<std::pair<key_t, value_ptr>> ret;
    std::vector<std::pair<key_t, value_ptr>> pinned;
    std::string_view prefix = std::string_view{pattern, strcspn(pattern, "*?")};
    auto it_tx = (prefix.empty() ? m_items.begin() : m_items.lower_bound(prefix));
    auto it_tx_end = m_items.end();
    auto it_store = (prefix.empty() ? m_store->m_data.begin() : m_store->m_data.lower_bound(prefix));
    auto it_store_end = m_store->m_data.end();

    while (it_tx != it_tx_end || it_store != it_store_end)
    {
        // Move iterators to the next matching key
        while (it_tx != it_tx_end)
        {
            if (!it_tx->first.starts_with(prefix))
                it_tx = it_tx_end;
            else if (glob_match(it_tx->first.data(), pattern))
                break;
            else
                it_tx++;
        }

        // Move iterators to the next matching key
        while (it_store != it_store_end)
        {
            if (!it_store->first.starts_with(prefix))
                it_store = it_store_end;
            else if (glob_match(it_store->first.data(), pattern))
                break;
            else
                it_store++;
        }

        if (it_tx != it_tx_end && it_store != it_store_end)
        {
            if (it_tx->first < it_store->first) // key in transaction
            {
                if (std::get<action_e>(it_tx->second) != action_e::DELETE)
                    ret.emplace_back(it_tx->first, std::get<value_ptr>(it_tx->second));

                it_tx++;
            }
            else if (it_tx->first > it_store->first)  // key only in store
            {
                ret.emplace_back(it_store->first, it_store->second);

                if (m_isolation_level != transaction::isolation_e::READ_COMMITTED)
                    pinned.emplace_back(it_store->first, it_store->second);

                it_store++;
            }
            else  // key in both places
            {
                if (std::get<action_e>(it_tx->second) != action_e::DELETE)
                    ret.emplace_back(it_tx->first, std::get<value_ptr>(it_tx->second));

                it_tx++;
                it_store++;
            }
        }
        else if (it_tx != it_tx_end)  // key only in transaction
        {
            if (std::get<action_e>(it_tx->second) != action_e::DELETE)
                ret.emplace_back(it_tx->first, std::get<value_ptr>(it_tx->second));

            it_tx++;
        }
        else if (it_store != it_store_end)  // key only in store
        {
            ret.emplace_back(it_store->first, it_store->second);

            if (m_isolation_level != transaction::isolation_e::READ_COMMITTED)
                pinned.emplace_back(it_store->first, it_store->second);

            it_store++;
        }
    }

    // for repeatable read and serializable isolation levels
    // mark pinned values as read in the transaction
    for (const auto &[key, value] : pinned)
        m_items.emplace(key, std::make_tuple(action_e::READ, value));

    return ret;
}

void nplex::transaction_impl::update(const std::vector<change_t> &changes)
{
    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

    if (m_state != state_e::OPEN) {
        assert(false);
        return;
    }

    log_debug(m_client, "Updating transaction {}", m_id);

    switch (m_isolation_level)
    {
        case isolation_e::SERIALIZABLE:
            update_serializable(changes);
            break;
        case isolation_e::READ_COMMITTED:
        case isolation_e::REPEATABLE_READ:
            update_default(changes);
            break;
    }

    if (m_dirty || m_ensures.empty())
        return;

    // Checking validations
    for (const auto &change : changes)
    {
        for (const auto &item : m_ensures)
        {
            if (!glob_match(change.key.data(), item.data()))
                continue;

            m_dirty = true;
            break;
        }
    }
}

void nplex::transaction_impl::update_default(const std::vector<change_t> &changes)
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

void nplex::transaction_impl::update_serializable(const std::vector<change_t> &changes)
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
                    m_items.emplace(change.key, std::make_tuple(action_e::READ, change.new_value));
                    break;
                case change_t::action_e::UPDATE:
                case change_t::action_e::DELETE:
                    m_items.emplace(change.key, std::make_tuple(action_e::READ, change.old_value));
                    break;
            }

            continue;
        }

        // Case: key exists in the transaction
        m_dirty = true;
    }
}

std::future<nplex::transaction::submit_e> nplex::transaction_impl::submit(bool force)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state != state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    if (m_read_only) {
        m_promise.set_value(submit_e::NO_MODIFICATIONS);
        set_state(state_e::DISCARDED);
        return m_promise.get_future();
    }

    if (!force)
    {
        bool has_modifications = false;

        for (const auto &[key, entry] : m_items) {
            if (std::get<action_e>(entry) != action_e::READ) {
                has_modifications = true;
                break;
            }
        }

        if (!has_modifications) {
            m_promise.set_value(submit_e::NO_MODIFICATIONS);
            set_state(state_e::DISCARDED);
            return m_promise.get_future();
        }

        if (m_dirty) {
            m_promise.set_value(submit_e::REJECTED_INTEGRITY);
            set_state(state_e::REJECTED);
            return m_promise.get_future();
        }
    }

    if (auto client = m_client.lock())
    {
        if (force && !client->can_force()) {
            m_promise.set_value(submit_e::REJECTED_PERMISSION);
            set_state(state_e::REJECTED);
            return m_promise.get_future();
        }

        set_state(state_e::SUBMITTED);
        client->push_command(std::make_unique<submit_req_t>(shared_from_this(), force));
        return m_promise.get_future();
    }

    throw nplex_exception("Client is no longer available");
}

void nplex::transaction_impl::set_submit_result(std::exception_ptr eptr)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state != state_e::SUBMITTED)
        return;

    set_state(state_e::ABORTED);
    m_promise.set_exception(eptr);
}

void nplex::transaction_impl::set_submit_result(msgs::SubmitCode code)
{
    if (m_state != state_e::SUBMITTED)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    switch (code)
    {
        case msgs::SubmitCode::ACCEPTED:
            set_state(state_e::ACCEPTED);
            break;
        case msgs::SubmitCode::TRY_LATER:
            set_state(state_e::OPEN);
            m_promise.set_value(submit_e::TRY_LATER);
            m_promise = std::promise<submit_e>{};
            break;
        case msgs::SubmitCode::NO_MODIFICATIONS:
            set_state(state_e::DISCARDED);
            m_promise.set_value(submit_e::NO_MODIFICATIONS);
            break;
        case msgs::SubmitCode::REJECTED_PERMISSION:
            set_state(state_e::REJECTED);
            m_promise.set_value(submit_e::REJECTED_PERMISSION);
            break;
        case msgs::SubmitCode::REJECTED_OLD_REVISION:
            set_state(state_e::REJECTED);
            m_promise.set_value(submit_e::REJECTED_OLD_REVISION);
            break;
        case msgs::SubmitCode::REJECTED_INTEGRITY:
            set_state(state_e::REJECTED);
            m_promise.set_value(submit_e::REJECTED_INTEGRITY);
            break;
        case msgs::SubmitCode::REJECTED_ENSURE:
            set_state(state_e::REJECTED);
            m_promise.set_value(submit_e::REJECTED_ENSURE);
            break;
        default:
            set_state(state_e::REJECTED);
            m_promise.set_value(submit_e::REJECTED_OTHER);
            break;
    }
}

void nplex::transaction_impl::confirm_commit([[maybe_unused]] rev_t rev)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state != state_e::ACCEPTED)
        return;
    
    set_state(state_e::COMMITTED);
    m_promise.set_value(submit_e::COMMITTED);
}
