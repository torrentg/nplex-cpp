#include "nplex-cpp/client.hpp"
#include "client_impl.hpp"

nplex::client_t::client_t(const params_t &params)
{
    m_impl = std::make_unique<client_impl_t>(*this, params);

    m_impl->thread_loop = std::thread([this]() {
        m_impl->run();
    });
}

nplex::client_t::state_e nplex::client_t::state() const
{ 
    return m_impl->state;
}

nplex::rev_t nplex::client_t::rev() const
{
    std::lock_guard<decltype(m_impl->cache->m_mutex)> lock_cache(m_impl->cache->m_mutex);
    return m_impl->cache->m_rev;
}

void nplex::client_t::close()
{
    std::lock_guard<decltype(m_impl->m_mutex)> lock(m_impl->m_mutex);

    switch (m_impl->state)
    {
        case state_e::INITIALIZING:
            m_impl->state = state_e::CLOSED;
            return;

        case state_e::CLOSED:
            return;

        case state_e::CLOSING:
            break;

        default:
            m_impl->state = state_e::CLOSING;
            m_impl->commands.clear();
            m_impl->commands.push(close_cmd_t{});
            uv_async_send(m_impl->async.get());
            break;
    }

    m_impl->thread_loop.join();
}

nplex::tx_ptr nplex::client_t::create_tx(transaction_t::isolation_e isolation, bool read_only)
{
    std::lock_guard<decltype(m_impl->m_mutex)> lock(m_impl->m_mutex);

    if (m_impl->state == state_e::CLOSED)
        throw nplex_exception("Client is closed");

    size_t num_concurrent_tx = m_impl->ongoing_tx.size() + m_impl->pending_tx.size();
    if (num_concurrent_tx >= m_impl->params.max_num_concurrent_tx)
        throw nplex_exception("Too many concurrent transactions (max={})", m_impl->params.max_num_concurrent_tx);

    auto tx = std::make_shared<transaction_impl_t>(m_impl->cache, isolation, read_only);

    m_impl->ongoing_tx.insert(tx);

    return tx;
}

bool nplex::client_t::submit_tx(tx_ptr tx, bool force)
{
    if (!tx)
        throw std::invalid_argument("Transaction is empty");

    if (tx->state() != transaction_t::state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    std::lock_guard<decltype(m_impl->m_mutex)> lock(m_impl->m_mutex);

    if (m_impl->state != state_e::SYNCED)
        throw nplex_exception("Client not synced");

    auto it = m_impl->ongoing_tx.find(tx);
    if (it == m_impl->ongoing_tx.end())
        throw nplex_exception("Transaction not found");

    // TODO: check if there are actions to submit (not-only-reads)

    if (m_impl->commands.push(submit_cmd_t{tx, force}) == 1)
        uv_async_send(m_impl->async.get());

    // TODO: solve visibility error
    //tx->state(transaction_t::state_e::SUBMITTING);

    return true;
}

bool nplex::client_t::discard_tx(tx_ptr tx)
{
    std::lock_guard<decltype(m_impl->m_mutex)> lock(m_impl->m_mutex);

    if (m_impl->state == state_e::CLOSED)
        return false;

    return m_impl->ongoing_tx.erase(tx);
}
