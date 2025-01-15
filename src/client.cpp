#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <variant>
#include <cassert>
#include <arpa/inet.h>
#include <uv.h>
#include "cppcrc.h"
#include "cqueue.hpp"
#include "nplex-cpp/client.hpp"
#include "messages.hpp"
#include "mqueue.hpp"
#include "cache.hpp"
#include "addr.hpp"

#define UNUSED(x) (void)(x)

/**
 * Notes on this compilation unit:
 * 
 * When we use the libuv library we apply C conventions (instead of C++):
 *   - calloc/free are used instead of new/delete.
 *   - pointers are casted to the correct type.
 *   - pointers to functions.
 */

namespace {

struct output_msg_t
{
    uv_write_t req;
    uv_buf_t buf[4];
    flatbuffers::DetachedBuffer content;
    std::uint32_t metadata;     // 0=none, 1=lz4 (big-endian)
    std::uint32_t checksum;     // CRC32 of len + metadata + content (big-endian)
    std::uint32_t len;          // Total message length (including len, metadata, content, checksum) (big-endian)

    output_msg_t(flatbuffers::DetachedBuffer &&content_)
    {
        content = std::move(content_);

        len = (std::uint32_t)(content.size() + sizeof(len) + sizeof(metadata) + sizeof(checksum));
        len = htonl(len);

        metadata = htonl(0); // not-compressed

        checksum = CRC32::CRC32::calc(reinterpret_cast<const uint8_t *>(&len), sizeof(len));
        checksum = crc_utils::reverse(checksum ^ 0xFFFFFFFF);
        checksum = CRC32::CRC32::calc(reinterpret_cast<const uint8_t *>(&metadata), sizeof(metadata), checksum);
        checksum = crc_utils::reverse(checksum ^ 0xFFFFFFFF);
        checksum = CRC32::CRC32::calc(reinterpret_cast<const uint8_t *>(content.data()), content.size(), checksum);
        checksum = htonl(checksum);

        buf[0] = uv_buf_init((char *) &len, sizeof(len));
        buf[1] = uv_buf_init((char *) &metadata, sizeof(metadata));
        buf[2] = uv_buf_init((char *) content.data(), (unsigned int) content.size());
        buf[3] = uv_buf_init((char *) &checksum, sizeof(checksum));
    }
};

} // unnamed namespace

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
    addr_t server_addr;                             //!< Server address.
    std::size_t correlation = 0;                    //!< Last correlation id.
    std::mutex m_mutex;                             //!< Mutex to protect the client state.
    params_t params;                                //!< Client params.
    std::unique_ptr<uv_loop_t> loop;                //!< Event loop.
    std::unique_ptr<uv_async_t> async;              //!< Signals that there are input commands.
    std::thread thread_loop;                        //!< Event loop thread, process input commands.
    mqueue<command_t> commands;                     //!< Commands pending to be digested by the event loop.
    //gto::cqueue<std::unique_ptr<output_msg_t>> output_msgs;
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

static void cb_on_tcp_connect(uv_connect_t *req, int status)
{
    uv_tcp_t *con = (uv_tcp_t *) req->handle;
    nplex::client_impl_t *impl = (nplex::client_impl_t *) con->loop->data;

    free(req);

    if (status < 0) {
        std::string msg = fmt::format("Failed to connect to {}: {}", impl->server_addr.str(), uv_strerror(status));
        //TODO: call client_t::on_disconnect()
        return;
    }

    // TODO
    // uv_read_start((uv_stream_t *) con, cb_on_tcp_alloc, cb_on_tcp_read);

    // impl->state = nplex::client_t::state_e::LOGGING_IN;


    // // send login message
    // rft_tcp_msg_t *msg = con->output_queue_front;

    // while (msg)
    // {
    //     uv_write((uv_write_t *) msg, (uv_stream_t *) con, msg->buf, 2, on_tcp_write);
    //     msg = msg->next;
    // }
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
    int rc = 0;
    struct sockaddr_storage addr_in;

    // TODO: Check if the client is already connected
    // TODO: manage on-fly messages (clear + reject tx)

    state = client_t::state_e::CONNECTING;
    server_addr = addr_t{cmd.server};

    std::memset(&addr_in, 0, sizeof(addr_in));

    switch(server_addr.family())
    {
        case AF_INET:
            if ((rc = uv_ip4_addr(server_addr.host().c_str(), server_addr.port(), (struct sockaddr_in*) &addr_in)) != 0) {
                throw nplex_exception(uv_strerror(rc));
            }
            break;
        case AF_INET6:
            if ((rc = uv_ip6_addr(server_addr.host().c_str(), server_addr.port(), (struct sockaddr_in6*) &addr_in)) != 0) {
                throw nplex_exception(uv_strerror(rc));
            }
            break;
        case AF_UNSPEC:
        {
            uv_getaddrinfo_t req;
            struct addrinfo hints;

            std::memset(&req, 0, sizeof(req));
            std::memset(&hints, 0, sizeof(hints));

            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            // request address info made synchronously (at this point no other tasks are done)
            if ((rc = uv_getaddrinfo(loop.get(), &req, NULL, server_addr.host().c_str(), NULL, &hints)) != 0) {
                throw nplex_exception(uv_strerror(rc));
            }

            if (req.addrinfo->ai_family != AF_INET && req.addrinfo->ai_family != AF_INET6) {
                uv_freeaddrinfo(req.addrinfo);
                throw nplex_exception(fmt::format("Invalid address family: {}", server_addr.str()));
            }

            memmove(&addr_in, req.addrinfo->ai_addr, req.addrinfo->ai_addrlen);
            uv_freeaddrinfo(req.addrinfo);
            break;
        }
        default:
            throw nplex_exception(fmt::format("Unrecognized address family: {}", server_addr.str()));
    }

    uv_tcp_t *socket = NULL;
    uv_connect_t* connect = NULL;

    if ((socket = (uv_tcp_t *) calloc(1, sizeof(uv_tcp_t))) == NULL)
        throw std::bad_alloc();

    if ((rc = uv_tcp_init(loop.get(), (uv_tcp_t *) socket)) != 0) {
        free(socket);
        throw nplex_exception(uv_strerror(rc));
    }

    if ((connect = (uv_connect_t*) malloc(sizeof(uv_connect_t))) == NULL) {
        free(socket);
        throw std::bad_alloc();
    }

    if ((rc = uv_tcp_connect(connect, socket, (const struct sockaddr*) &addr_in, cb_on_tcp_connect)) != 0) {
        free(socket);
        free(connect);
        throw nplex_exception(uv_strerror(rc));
    }
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

nplex::client_t::state_e nplex::client_t::state() const
{ 
    return m_impl->state;
}

nplex::rev_t nplex::client_t::rev() const
{
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

    m_impl->commands.push(submit_cmd_t{tx, force});

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
