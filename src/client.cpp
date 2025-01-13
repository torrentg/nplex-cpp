#include <uv.h>
#include <mutex>
#include <thread>
#include <atomic>
#include "cqueue.hpp"
#include "nplex-cpp/client.hpp"
#include "cache.hpp"
#include "mqueue.hpp"

#define UNUSED(x) (void)(x)

// =================================================================================================
// client_t::impl_t declaration
// =================================================================================================

namespace nplex {

class command_t
{
    enum class type_e : std::uint8_t {
        CONNECT,
        LOAD,
        SUBMIT,
        CLOSE,
        PING
    };
};

class event_t
{
    enum class type_e : std::uint8_t {
        CONNECT_RESPONSE,
        LOAD_RESPONSE,
        UPDATE_PUSH,
        SUBMIT_RESPONSE,
        KEEPALIVE_PUSH,
        PING_RESPONSE,
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
    std::thread thread_loop;                        //!< Event loop thread, process input commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
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

nplex::client_t::client_t(const params_t &params) : m_impl(std::make_unique<impl_t>())
{
    m_impl->params = params;
    m_impl->state = state_e::CONNECTING;

    //TODO: create a new event loop if loop is null
    //TODO: attach client to the event loop
    //TODO: send connection request to the server

    m_impl->thread_loop = std::thread([this] {
        return;
    });

    m_impl->thread_loop.join();
}

nplex::client_t::state_e nplex::client_t::state() const { 
    return m_impl->state;
}

nplex::rev_t nplex::client_t::rev() const {
    std::lock_guard<decltype(m_impl->m_mutex)> lock_client(m_impl->m_mutex);
    std::lock_guard<decltype(m_impl->cache->m_mutex)> lock_cache(m_impl->cache->m_mutex);
    return m_impl->cache->m_rev;
}

nplex::client_t::~client_t()
{
    close(true);
    //TODO: block until all resources was freed
}

#if 0
void * run_event_loop(nplex::client_t::impl_t *impl)
{
    int rc = 0;
    intptr_t ret = 1;

    if (!impl || !impl->params || impl->loop)
        return NULL;

    params = impl->params;
    logger = impl->logger;
    storage = impl->storage;
    messaging = impl->messaging;
    replicator = impl->replicator;

    pthread_mutex_lock(&impl->mutex);

    if ((impl->loop = (uv_loop_t *) calloc(1, sizeof(uv_loop_t))) == NULL) {
        LOG(RAFT_LOG_ERROR, "Out of memory");
        goto RAFT_RUN_END;
    }

    if ((impl->async_stop = (uv_async_t *) calloc(1, sizeof(uv_async_t))) == NULL) {
        LOG(RAFT_LOG_ERROR, "Out of memory");
        goto RAFT_RUN_END;
    }

    if ((rc = uv_loop_init(impl->loop)) != 0) {
        LOG(RAFT_LOG_ERROR, "%s", uv_strerror(rc));
        goto RAFT_RUN_END;
    }

    impl->loop->data = impl;

    if ((rc = uv_async_init(impl->loop, impl->async_stop, stop_event_loop)) != 0) {
        LOG(RAFT_LOG_ERROR, "%s", uv_strerror(rc));
        goto RAFT_RUN_END;
    }

    if (!rft_create_tcp_server(impl->loop, impl->params->servers + impl->params->server_id)) {
        LOG(RAFT_LOG_ERROR, "Error creating server");
        goto RAFT_RUN_END;
    }

    pthread_mutex_unlock(&impl->mutex);

    LOG(RAFT_LOG_INFO, "Raft started");

    if (impl->startup)
        impl->startup();

    uv_run(impl->loop, UV_RUN_DEFAULT);

    rft_shutdown(impl);

    uv_run(impl->loop, UV_RUN_DEFAULT);

    LOG(RAFT_LOG_INFO, "Raft stopped");

    ret = 0;

    pthread_mutex_lock(&impl->mutex);

RAFT_RUN_END:
    if (impl->loop) {
        uv_walk(impl->loop, close_handle, NULL);
        while (uv_run(impl->loop, UV_RUN_NOWAIT));
        uv_loop_close(impl->loop);
    }

    free(impl->async_stop);
    impl->async_stop = NULL;

    free(impl->loop);
    impl->loop = NULL;

    pthread_mutex_unlock(&impl->mutex);

    return (void *) ret;
}
#endif