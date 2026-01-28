#include "nplex-cpp/client.hpp"
#include "nplex-cpp/exception.hpp"
#include "addr.hpp"
#include "client_impl.hpp"

// ==========================================================
// Internal (static) functions
// ==========================================================

static void check_params(const nplex::params_t &params)
{
    if (params.user.empty())
        throw nplex::invalid_config("User not found");

    if (params.password.empty())
        throw nplex::invalid_config("Password not found");

    if (params.servers.empty())
        throw nplex::invalid_config("Servers not found");

    try {
        for (auto &str : params.servers)
            nplex::addr_t{str};
    }
    catch(const nplex::nplex_exception &e) {
        throw nplex::invalid_config(e.what());
    }

    if (params.timeout_factor <= 1.0)
        throw nplex::invalid_config("Timeout factor <= 1.0");
}

static const char * to_str(nplex::transaction_t::isolation_e isolation)
{
    switch(isolation)
    {
        case nplex::transaction_t::isolation_e::READ_COMMITTED: return "READ_COMMITTED";
        case nplex::transaction_t::isolation_e::REPEATABLE_READ: return "REPEATABLE_READ";
        case nplex::transaction_t::isolation_e::SERIALIZABLE: return "SERIALIZABLE";
        default: return "UNKNOWN";
    }
}

// ==========================================================
// client_t definitions
// ==========================================================

// default listener initialization
nplex::listener_t nplex::client_t::default_listener{};

nplex::client_t::client_t(const params_t &params, listener_t &listener)
{
    check_params(params);

    m_impl = std::make_unique<impl_t>(params, listener, *this);

    // Starts the event loop
    thread_loop = std::thread([this]() {
        try {
            m_impl->run();
        }
        catch(const std::exception &e) {
            m_impl->log_error("{}", e.what());
        }
        catch(...) {
            m_impl->log_error("Unknown exception in the event loop");
        }
    });

    m_impl->wait_for_startup();

    try {
        m_impl->wait_for_startup();
    }
    catch(...) {
        close();
        throw;
    }
}

nplex::client_t::~client_t()
{
    close();
}

nplex::client_t::state_e nplex::client_t::state() const
{ 
    return m_impl->state();
}

nplex::rev_t nplex::client_t::rev() const
{
    return m_impl->cache->m_rev;
}

void nplex::client_t::close()
{
    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    if (!m_impl->is_closed())
    {
        m_impl->commands.clear();
        m_impl->commands.push(close_cmd_t{});
        uv_async_send(m_impl->async.get());
    }

    join();
}

void nplex::client_t::join()
{
    if (thread_loop.joinable())
        thread_loop.join();
}

nplex::tx_ptr nplex::client_t::create_tx(transaction_t::isolation_e isolation, bool read_only)
{
    std::lock_guard<decltype(m_mutex)> lock_impl(m_mutex);

    if (m_impl->is_closed())
        throw nplex_exception("Client is closed");

    size_t num_txs = m_impl->transactions.size();
    if (num_txs >= m_impl->params().max_active_txs)
        throw nplex_exception("Too many concurrent transactions (max={})", m_impl->params().max_active_txs);

    std::lock_guard<decltype(m_impl->cache->m_mutex)> lock_cache(m_impl->cache->m_mutex);

    auto tx = std::make_shared<transaction_impl_t>(m_impl->cache, isolation, read_only);
    m_impl->transactions.insert(tx);

    m_impl->log_debug("Transaction created, isolation={}, read_only={}", ::to_str(isolation), read_only);

    return tx;
}

bool nplex::client_t::submit_tx(const tx_ptr &tx, bool force)
{
    if (!tx)
        throw std::invalid_argument("Transaction is empty");

    if (tx->state() != transaction_t::state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    if (m_impl->is_closed())
        throw nplex_exception("Client is closed");

    if (m_impl->state() != state_e::SYNCHRONIZED)
        throw nplex_exception("Client not synced");

    //TODO: caution, m_impl->transactions is not thread-safe

    auto it = m_impl->transactions.find(tx);
    if (it == m_impl->transactions.end())
        throw nplex_exception("Transaction not found");

    // TODO: check if there are actions to submit (not-only-reads)

    if (m_impl->commands.push(submit_cmd_t{dynamic_pointer_cast<transaction_impl_t>(tx), force}) == 1)
        uv_async_send(m_impl->async.get());

    // TODO: solve visibility error
    //tx->state(transaction_t::state_e::SUBMITTING);

    return true;
}

bool nplex::client_t::discard_tx(const tx_ptr &tx)
{
    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    // TODO: consider the state == CLOSED
    // TODO: send async command to discard tx

    UNUSED(tx);
    return true;
}
