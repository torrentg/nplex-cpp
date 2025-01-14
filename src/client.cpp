#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <variant>
#include <cassert>
#include <uv.h>
#include "cqueue.hpp"
#include "nplex-cpp/client.hpp"
#include "messages.hpp"
#include "mqueue.hpp"
#include "cache.hpp"

#define UNUSED(x) (void)(x)

// =================================================================================================
// client_t::impl_t declaration
// =================================================================================================

namespace nplex {

using cache_ptr = std::shared_ptr<cache_t>;
using tx_ptr = std::shared_ptr<transaction_t>;

struct connect_cmd_t {
    std::string server;      // host:port
};

struct load_cmd_t {
    msgs::LoadMode load_mode;
    rev_t rev;
};

struct submit_cmd_t {
    tx_ptr tx;
    bool force;
};

struct close_cmd_t {
    bool close_immediate;
};

struct ping_cmd_t {
    std::string payload;
};

using command_t = std::variant<connect_cmd_t, load_cmd_t, submit_cmd_t, close_cmd_t, ping_cmd_t>;

struct client_impl_t
{
    std::size_t correlation = 0;                    //!< Last correlation id.
    std::mutex m_mutex;                             //!< Mutex to protect the client state.
    params_t params;                                //!< Client params.
    std::unique_ptr<uv_loop_t> loop;                //!< Event loop.
    std::unique_ptr<uv_async_t> async;              //!< Signals that there are input commands.
    std::thread thread_loop;                        //!< Event loop thread, process input commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    std::set<tx_ptr> ongoing_tx;                    //!< List of ongoing transactions (user working on it).
    gto::cqueue<tx_ptr> pending_tx;                 //!< List of pending transactions (awaiting server response).
    cache_ptr cache;                                //!< Database content.
    std::atomic<client_t::state_e> state;           //!< Client state.
    bool can_force = false;                         //!< User can force transactions (given by server at login).
    std::string error;                              //!< Error message (empty if no error).

    client_impl_t(const params_t &params);
    void run() noexcept;
    void connect(const nplex::connect_cmd_t &cmd);
};

} // namespace nplex

// =================================================================================================
// libuv functions
// =================================================================================================

static void cb_process_async(uv_async_t *handle)
{
    nplex::client_impl_t *impl = (nplex::client_impl_t *) handle->data;
    assert(impl);

    nplex::command_t cmd;
    while (!impl->commands.try_pop(cmd))
    {
        // TODO: process command
    }
}

static void cb_close_handle(uv_handle_t *handle, void *arg)
{
    UNUSED(arg);

    if (uv_is_closing(handle))
        return;

    switch(handle->type)
    {
        case UV_ASYNC:
            uv_close(handle, NULL);
            break;
        case UV_TIMER:
            uv_close(handle, (uv_close_cb) free);
            break;
        case UV_TCP:
            uv_close(handle, (uv_close_cb) free);
            break;
        default:
            uv_close(handle, NULL);
    }
}

// =================================================================================================
// impl_t methods
// =================================================================================================

nplex::client_impl_t::client_impl_t(const params_t &params_) : params(params_), commands(params.max_num_queued_commands)
{
    state = client_t::state_e::INITIALIZING;

    loop = std::make_unique<uv_loop_t>();
    async = std::make_unique<uv_async_t>();

    if (params.servers.empty())
        throw nplex_exception("Invalid params: no servers");
}

void nplex::client_impl_t::run() noexcept
{
    std::unique_lock<decltype(m_mutex)> guard(m_mutex);

    if (state != client_t::state_e::INITIALIZING)
        return;

    assert(loop);
    if (!loop || uv_loop_init(loop.get()) != 0) {
        error = "Error initializing event loop (uv_loop_init)";
        return;
    }

    loop->data = this;

    assert(async);
    if (!async || uv_async_init(loop.get(), async.get(), cb_process_async) != 0) {
        error = "Error initializing event loop (uv_async_init)";
        return;
    }

    try
    {
        guard.unlock();
        connect(connect_cmd_t{params.servers});
        uv_run(loop.get(), UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        error = e.what();
    }
    catch(...) {
        error = "Unknown error in the event loop";
    }

    guard.unlock();
    uv_walk(loop.get(), cb_close_handle, NULL);
    while (uv_run(loop.get(), UV_RUN_NOWAIT));
    uv_loop_close(loop.get());

    return;
}

void nplex::client_impl_t::connect(const nplex::connect_cmd_t &cmd)
{
    state = client_t::state_e::CONNECTING;

    struct sockaddr_storage addr_in;

    std::memset(&addr_in, 0, sizeof(addr_in));

    UNUSED(cmd);
#if 0

    int rc = 0;
    tcp_server_t *server = NULL;

    switch(addr->family)
    {
        case AF_INET:
            if ((rc = uv_ip4_addr(addr->host, addr->port, (struct sockaddr_in*) &addr_in)) != 0) {
                LOG(RAFT_LOG_ERROR, "Invalid server address: %s", addr);
                return false;
            }
            break;
        case AF_INET6:
            if ((rc = uv_ip6_addr(addr->host, addr->port, (struct sockaddr_in6*) &addr_in)) != 0) {
                LOG(RAFT_LOG_ERROR, "Invalid server address: %s", addr);
                return false;
            }
            break;
        case AF_UNSPEC: {
            uv_getaddrinfo_t req = {0};
            struct addrinfo hints = { .ai_socktype = SOCK_STREAM, .ai_protocol = IPPROTO_TCP };

            if ((rc = uv_getaddrinfo(loop, &req, NULL, addr->host, NULL, &hints)) != 0) {
                LOG(RAFT_LOG_ERROR, "Invalid server address: %s", addr);
                return false;
            }

            if (req.addrinfo->ai_family != AF_INET && req.addrinfo->ai_family != AF_INET6) {
                LOG(RAFT_LOG_ERROR, "Invalid address family: %s", addr);
                uv_freeaddrinfo(req.addrinfo);
                return false;
            }

            memmove(&addr_in, req.addrinfo->ai_addr, req.addrinfo->ai_addrlen);
            uv_freeaddrinfo(req.addrinfo);
            break;
        }
        default:
            LOG(RAFT_LOG_ERROR, "Unrecognized address family: %s", addr);
            return false;
    }

    if ((server = (tcp_server_t *) calloc(1, sizeof(tcp_server_t))) == NULL)
        return false;

    server->type = RFT_SERVER;
    server->address = addr;

    if ((rc = uv_tcp_init(loop, (uv_tcp_t *) server)) != 0) {
        LOG(RAFT_LOG_ERROR, uv_strerror(rc));
        goto CREATE_TCP_SERVER_ERR;
    }

    if ((rc = uv_tcp_bind((uv_tcp_t *) server, (struct sockaddr*) &addr_in, 0)) != 0) {
        LOG(RAFT_LOG_ERROR, uv_strerror(rc));
        goto CREATE_TCP_SERVER_ERR;
    }

    if ((rc = uv_listen((uv_stream_t*) server, MAX_QUEUED_CONNECTIONS, on_new_tcp_connection)) != 0) {
        LOG(RAFT_LOG_ERROR, uv_strerror(rc));
        goto CREATE_TCP_SERVER_ERR;
    }

    if (addr->port == 0)
    {
        // port = 0 means random port
        // we get the assigned port
        int len = sizeof(addr_in);
        uv_tcp_getsockname((uv_tcp_t *) server, (struct sockaddr *) &addr_in, &len);
        addr->port = ntohs(((struct sockaddr_in *) &addr_in)->sin_port);
    }

    server->state = RFT_CON_ACCEPTED;

    LOG(RAFT_LOG_INFO, "Listening at %s:%d", addr->host, (int) addr->port);

    return true;

CREATE_TCP_SERVER_ERR:
    uv_close((uv_handle_t *) server, (uv_close_cb) rft_free_connection);
    return false;
#endif
}

// =================================================================================================
// client_t methods
// =================================================================================================

nplex::client_t::client_t(const params_t &params)
{
    m_impl = std::make_unique<client_impl_t>(params);

    m_impl->thread_loop = std::thread([impl = m_impl.get()]() {
        impl->run();
    });
}

nplex::client_t::state_e nplex::client_t::state() const { 
    return m_impl->state;
}

nplex::rev_t nplex::client_t::rev() const {
    std::lock_guard<decltype(m_impl->cache->m_mutex)> lock_cache(m_impl->cache->m_mutex);
    return m_impl->cache->m_rev;
}

void nplex::client_t::close(bool immediate)
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
            if (immediate)
                m_impl->commands.clear();
            m_impl->commands.push(close_cmd_t{immediate});
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

    auto tx = transaction_t::create(m_impl->cache, isolation, read_only);

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

    if (m_impl->state == state_e::CLOSED || m_impl->state == state_e::CLOSING)
        throw nplex_exception("Client is closed");

    auto it = m_impl->ongoing_tx.find(tx);
    if (it == m_impl->ongoing_tx.end())
        throw nplex_exception("Transaction not found");

    if (m_impl->commands.try_push(submit_cmd_t{tx, force}) == false)
        throw nplex_exception("Too many pending commands");

    tx->state(transaction_t::state_e::SUBMITTING);

    uv_async_send(m_impl->async.get());

    return true;
}

bool nplex::client_t::discard_tx(tx_ptr tx)
{
    std::lock_guard<decltype(m_impl->m_mutex)> lock(m_impl->m_mutex);

    if (m_impl->state == state_e::CLOSED)
        return false;

    return m_impl->ongoing_tx.erase(tx);
}
