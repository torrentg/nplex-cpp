#include "nplex-cpp/client.hpp"
#include "client_impl.hpp"

nplex::client_t::client_t(const params_t &params)
{
    m_impl = std::make_unique<impl_t>(*this, params);

    thread_loop = std::thread([this]() {
        m_impl->run();
    });
}

nplex::client_t::state_e nplex::client_t::state() const
{ 
    return m_impl->state;
}

nplex::rev_t nplex::client_t::rev() const
{
    return m_impl->cache->m_rev;
}

void nplex::client_t::close()
{
    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    switch (m_impl->state)
    {
        case state_e::CLOSED:
            return;

        case state_e::CLOSING:
            break;

        default:
            m_impl->state = state_e::CLOSING;
            m_impl->commands.clear();
            if (m_impl->commands.push(close_cmd_t{}) == 1)
                uv_async_send(m_impl->async.get());
            break;
    }

    thread_loop.join();
}

nplex::tx_ptr nplex::client_t::create_tx(transaction_t::isolation_e isolation, bool read_only)
{
    std::lock_guard<decltype(m_mutex)> lock_impl(m_mutex);

    if (m_impl->state == state_e::CLOSED)
        throw nplex_exception("Client is closed");

    size_t num_txs = m_impl->transactions.size();
    if (num_txs >= m_impl->params.max_num_concurrent_tx)
        throw nplex_exception("Too many concurrent transactions (max={})", m_impl->params.max_num_concurrent_tx);

    std::lock_guard<decltype(m_impl->cache->m_mutex)> lock_cache(m_impl->cache->m_mutex);

    auto tx = std::make_shared<transaction_impl_t>(m_impl->cache, isolation, read_only);
    m_impl->transactions.insert(tx);

    return tx;
}

bool nplex::client_t::submit_tx(const tx_ptr &tx, bool force)
{
    if (!tx)
        throw std::invalid_argument("Transaction is empty");

    if (tx->state() != transaction_t::state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    if (m_impl->state != state_e::SYNCHRONIZED)
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

    if (m_impl->state == state_e::CLOSED)
        return false;

    auto it = m_impl->transactions.find(tx);

    if (it == m_impl->transactions.end())
        return false;

    m_impl->transactions.erase(it);
    return true;
}

uv_loop_t * nplex::client_t::loop() const
{
    return m_impl->loop.get();
}
