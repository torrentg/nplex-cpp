#include <mutex>
#include <thread>
#include <atomic>
#include "cache.hpp"
#include "cqueue.hpp"
#include "mqueue.hpp"
#include "client.hpp"

#define UNUSED(x) (void)(x)

// =================================================================================================
// client_t::impl_t declaration
// =================================================================================================

namespace nplex {

class command_t
{
    enum class type_e : std::uint8_t {
        LOAD,
        SUBMIT,
        PING,
        CLOSE,
    };
};

class event_t
{
    enum class type_e : std::uint8_t {
        SNAPSHOT,
        COMMIT,
        SUBMIT_RESPONSE,
        DISCONNECT
    };
};

struct nplex::client_t::impl_t
{
    using cache_ptr = std::shared_ptr<cache_t>;
    using tx_ptr = std::shared_ptr<transaction_t>;

    std::mutex m_mutex;                             //!< Mutex to protect the client state.
    params_t params;                                //!< Client params.
    uv_loop_t *loop;                                //!< Event loop.
    uv_async_t async;                               //!< Signals that there are input commands.
    std::thread thread;                             //!< Worker thread, execute output commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    mqueue<event_t> events;                         //!< Events pending to be digested by the working thread.
    gto::cqueue<tx_ptr> ongoing_tx;                 //!< List of ongoing transactions (user working on it).
    gto::cqueue<tx_ptr> pending_tx;                 //!< List of pending transactions (awaiting server response).
    cache_ptr cache;                                //!< Database content.
    std::atomic<state_e> state;                     //!< Client state.
    bool can_force = false;                         //!< User can force transactions (given by server at login).
};

} // namespace nplex

// =================================================================================================
// client_t methods
// =================================================================================================

nplex::client_t::client_t(const params_t &params, uv_loop_t *loop) : m_impl(std::make_unique<impl_t>())
{
    UNUSED(loop);

    m_impl->params = params;
    m_impl->state = state_e::CONNECTING;

    //TODO: create a new event loop if loop is null
    //TODO: attach client to the event loop
    //TODO: send connection request to the server
}

nplex::client_t::state_e nplex::client_t::state() const { 
    return m_impl->state;
}

nplex::rev_t nplex::client_t::rev() const {
    std::lock_guard<decltype(m_impl->m_mutex)> lock_client(m_impl->m_mutex);
    std::lock_guard<decltype(m_impl->cache->m_mutex)> lock_cache(m_impl->cache->m_mutex);
    return m_impl->cache->m_rev;
}
