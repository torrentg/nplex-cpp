#include <cassert>
#include <arpa/inet.h>
#include "cppcrc.h"
#include "client_impl.hpp"

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

using namespace nplex;
using namespace nplex::msgs;
using namespace flatbuffers;

struct sockaddr_storage get_sockaddr(uv_loop_t *loop, const addr_t &addr)
{
    int rc = 0;
    struct sockaddr_storage ret;

    std::memset(&ret, 0, sizeof(ret));

    switch(addr.family())
    {
        case AF_INET:
            if ((rc = uv_ip4_addr(addr.host().c_str(), addr.port(), (struct sockaddr_in*) &ret)) != 0) {
                throw nplex_exception(uv_strerror(rc));
            }
            break;
        case AF_INET6:
            if ((rc = uv_ip6_addr(addr.host().c_str(), addr.port(), (struct sockaddr_in6*) &ret)) != 0) {
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
            if ((rc = uv_getaddrinfo(loop, &req, NULL, addr.host().c_str(), NULL, &hints)) != 0) {
                throw nplex_exception(uv_strerror(rc));
            }

            if (req.addrinfo->ai_family != AF_INET && req.addrinfo->ai_family != AF_INET6) {
                uv_freeaddrinfo(req.addrinfo);
                throw nplex_exception(fmt::format("Invalid address family: {}", addr.str()));
            }

            memmove(&ret, req.addrinfo->ai_addr, req.addrinfo->ai_addrlen);
            uv_freeaddrinfo(req.addrinfo);
            break;
        }
        default:
            throw nplex_exception(fmt::format("Unrecognized address family: {}", addr.str()));
    }

    return ret;
}

flatbuffers::DetachedBuffer create_login_request(std::size_t cid, const std::string &user, const std::string &password)
{
    flatbuffers::FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::LOGIN_REQUEST, 
        CreateLoginRequest(builder, 
            cid, 
            builder.CreateString(user), 
            builder.CreateString(password)
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer create_load_request(std::size_t cid, LoadMode mode, rev_t rev)
{
    flatbuffers::FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::LOGIN_REQUEST, 
        CreateLoadRequest(builder, 
            cid, 
            mode,
            rev
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

} // unnamed namespace

// =================================================================================================
// libuv functions
// =================================================================================================

static void cb_process_async(uv_async_t *handle)
{
    nplex::client_impl_t *impl = (nplex::client_impl_t *) handle->data;
    nplex::command_t cmd;

    while (impl->commands.try_pop(cmd))
    {
        std::visit([impl, &cmd](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, nplex::connect_cmd_t>)
                impl->connect(get<T>(cmd));
            else if constexpr (std::is_same_v<T, nplex::load_cmd_t>)
                impl->load(get<T>(cmd));
            else if constexpr (std::is_same_v<T, nplex::submit_cmd_t>)
                impl->submit(get<T>(cmd));
            else if constexpr (std::is_same_v<T, nplex::close_cmd_t>)
                impl->close(get<T>(cmd));
            else if constexpr (std::is_same_v<T, nplex::ping_cmd_t>)
                impl->ping(get<T>(cmd));
            else
                static_assert(false, "non-exhaustive visitor!");
        }, cmd);
    }
}

static void cb_close_handle(uv_handle_t *handle, void *arg)
{
    UNUSED(arg);

    if (uv_is_closing(handle))
        return;

    switch(handle->type)
    {
        case UV_TCP:
        case UV_ASYNC:
            uv_close(handle, NULL);
            break;
        default:
            uv_close(handle, (uv_close_cb) free);
    }
}

static void cb_on_tcp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    UNUSED(suggested_size);
    uv_tcp_t *con = (uv_tcp_t *) handle;
    nplex::client_impl_t *impl = (nplex::client_impl_t *) con->loop->data;

    buf->base = impl->input_buffer;
    buf->len = sizeof(impl->input_buffer);
}

static void cb_on_tcp_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    UNUSED(stream);
    UNUSED(nread);
    UNUSED(buf);

    uv_tcp_t *con = (uv_tcp_t *) stream;
    nplex::client_impl_t *impl = (nplex::client_impl_t *) con->loop->data;
    char *ptr = buf->base;
    size_t len = buf->len;

    if (nread < 0 || buf->base == NULL) {
        impl->error = fmt::format("Tcp read error: {}", uv_strerror(nread));
        impl->close(nplex::close_cmd_t{});
        return;
    }

    // if (nread == UV_EOF) {
    //     rft_close_connection(con);
    //     return;
    // }

    // if (con->input_msg_data == NULL) {
    //     con->input_msg_data = malloc(MIN_MSG_SIZE);
    //     con->input_msg_reserved = MIN_MSG_SIZE;
    // }

    // while (len > 0)
    // {
    //     assert(con->input_msg_data && con->input_msg_reserved);
    //     assert(con->input_msg_pos <= con->input_msg_reserved);
    //     assert(con->input_msg_len <= con->input_msg_pos);
    //     assert(con->input_msg_len <= con->input_msg_reserved);

    //     // msg data is preceded by its length (4-bytes in network format).
    //     // msg data can be split into multiple on_tcp_read() calls.
    //     // msg length can be split between two consecutive on_tcp_read() calls.

    //     if (con->input_msg_len == 0)
    //     {
    //         assert(con->input_msg_pos < sizeof(uint32_t));

    //         size_t aux = MIN(sizeof(uint32_t) - con->input_msg_pos, len);
    //         memcpy(con->input_msg_data + con->input_msg_pos, ptr, aux);
    //         con->input_msg_pos += aux;
    //         ptr += aux;
    //         len -= aux;

    //         if (con->input_msg_pos < sizeof(uint32_t))
    //             return;

    //         uint32_t num = 0;
    //         memcpy(&num, con->input_msg_data, sizeof(uint32_t));

    //         con->input_msg_len = ntohl(num);
    //         con->input_msg_pos = 0;

    //         if (con->input_msg_len == 0) {
    //             LOG(RAFT_LOG_WARNING, "Received empty message, remote=%s", con->address);
    //             rft_close_connection(con);
    //             return;
    //         }

    //         if (con->input_msg_len > params->max_msg_size) {
    //             LOG(RAFT_LOG_WARNING, "Received message exceeding max size (%d), remote=%s", 
    //                     (int) params->max_msg_size, con->address);
    //             rft_close_connection(con);
    //             con->input_msg_len = 0;
    //             return;
    //         }

    //         if (con->input_msg_len > con->input_msg_reserved) {
    //             free(con->input_msg_data);
    //             con->input_msg_data = malloc(con->input_msg_len);
    //             con->input_msg_reserved = con->input_msg_len;
    //         }
    //     }

    //     assert(con->input_msg_pos < con->input_msg_len);

    //     size_t aux = MIN(con->input_msg_len - con->input_msg_pos, len);
    //     memcpy(con->input_msg_data + con->input_msg_pos, ptr, aux);
    //     con->input_msg_pos += aux;
    //     ptr += aux;
    //     len -= aux;

    //     assert(con->input_msg_pos <= con->input_msg_len);

    //     if (con->input_msg_pos == con->input_msg_len)
    //     {
    //         process_input_message(con);
    //         con->input_msg_len = 0;
    //         con->input_msg_pos = 0;
    //     }
    // }
}

static void cb_on_tcp_write(uv_write_t *req, int status)
{
    output_msg_t *msg = (output_msg_t *) req;
    uv_tcp_t *con = (uv_tcp_t *) req->handle;
    nplex::client_impl_t *impl = (nplex::client_impl_t *) con->loop->data;

    // assert(XXX);
    // // TODO: pop message
    // output_queue_release(con, tcp_msg);

    if (status < 0) {
        impl->error = fmt::format("Failed to write to {}: {}", impl->server_addr.str(), uv_strerror(status));
        impl->disconnect();
    }
}

static void cb_on_tcp_connect(uv_connect_t *req, int status)
{
    uv_tcp_t *con = (uv_tcp_t *) req->handle;
    nplex::client_impl_t *impl = (nplex::client_impl_t *) con->loop->data;

    free(req);

    if (status < 0) {
        std::string msg = fmt::format("Failed to connect to {}: {}", impl->server_addr.str(), uv_strerror(status));
        impl->close(nplex::close_cmd_t{});
        return;
    }

    uv_read_start((uv_stream_t *) con, cb_on_tcp_alloc, cb_on_tcp_read);

    impl->state = nplex::client_t::state_e::LOGGING_IN;

    impl->send(
        create_login_request(
            impl->correlation++, 
            impl->params.user, 
            impl->params.password
        )
    );
}

// =================================================================================================
// client_impl_t methods
// =================================================================================================

nplex::output_msg_t::output_msg_t(flatbuffers::DetachedBuffer &&content_)
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

nplex::client_impl_t::client_impl_t(client_t &parent_, const params_t &params_) : 
    parent(parent_),
    params(params_), 
    input_buffer{0},
    commands(params.max_num_queued_commands),
    state{client_t::state_e::INITIALIZING}
{
    if (params.servers.empty())
        throw nplex_exception("Invalid params: no servers");

    cache = std::make_shared<cache_t>();
    async = std::make_unique<uv_async_t>();
    loop = std::make_unique<uv_loop_t>();
    con = std::make_unique<uv_tcp_t>();
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
    catch (...) {
        error = "Unknown error in the event loop";
    }

    guard.unlock();
    uv_walk(loop.get(), cb_close_handle, NULL);
    while (uv_run(loop.get(), UV_RUN_NOWAIT));
    uv_loop_close(loop.get());

    parent.on_close();

    return;
}

void nplex::client_impl_t::connect(const nplex::connect_cmd_t &cmd)
{
    // TODO: Check if the client is already connected
    // TODO: manage on-fly messages (clear + reject tx)

    state = client_t::state_e::CONNECTING;

    int rc = 0;
    uv_connect_t* connect = NULL;
    struct sockaddr_storage addr_in = get_sockaddr(loop.get(), cmd.server);

    if ((rc = uv_tcp_init(loop.get(), con.get())) != 0)
        throw nplex_exception(uv_strerror(rc));

    if ((connect = (uv_connect_t*) malloc(sizeof(uv_connect_t))) == NULL)
        throw std::bad_alloc();

    if ((rc = uv_tcp_connect(connect, con.get(), (const struct sockaddr*) &addr_in, cb_on_tcp_connect)) != 0) {
        free(connect);
        throw nplex_exception(uv_strerror(rc));
    }
}

void nplex::client_impl_t::disconnect()
{
    switch (state)
    {
        case client_t::state_e::LOGGING_IN:
        case client_t::state_e::SYNCHRONIZING:
        case client_t::state_e::SYNCED:
            state = client_t::state_e::DISCONNECTING;
            uv_close((uv_handle_t *) con.get(), NULL);
            break;
        default:
            break;
    }
}

void nplex::client_impl_t::close(const close_cmd_t &cmd)
{
    UNUSED(cmd);

    switch (state)
    {
        case client_t::state_e::INITIALIZING:
            state = client_t::state_e::CLOSED;
            return;

        case client_t::state_e::CLOSING:
        case client_t::state_e::CLOSED:
            return;

        default:
            state = client_t::state_e::CLOSING;
            commands.clear();
            disconnect();
            break;
    }

    uv_stop(loop.get());
}

void nplex::client_impl_t::send(flatbuffers::DetachedBuffer &&buf)
{
    output_msg_t msg(std::move(buf));

    // TODO: add msg to msg_queue
    // rft_tcp_msg_t *msg = con->output_queue_front;

    uv_write(&msg.req, (uv_stream_t *) con.get(), msg.buf.data(), msg.buf.size(), cb_on_tcp_write);
}

void nplex::client_impl_t::load(const nplex::load_cmd_t &cmd)
{
    send(
        create_load_request(
            correlation++, 
            cmd.load_mode, 
            cmd.rev
        )
    );
}
