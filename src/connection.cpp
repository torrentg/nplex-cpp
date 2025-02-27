#include <cassert>
#include <arpa/inet.h>
#include "nplex-cpp/exception.hpp"
#include "client_impl.hpp"
#include "connection.hpp"

/**
 * Notes on this compilation unit:
 * 
 * When we use the libuv library we apply C conventions (instead of C++ ones):
 *   - When in Rome, do as the Romans do.
 *   - calloc/free are used instead of new/delete.
 *   - pointers to static functions.
 *   - C-style pointer casting.
 */

// ==========================================================
// Internal (static) functions
// ==========================================================

/**
 * Convert an string address to a sockaddr.
 * 
 * This is a blocking function.
 * 
 * @param loop Event loop.
 * @param addr Address to convert.
 * 
 * @return The sockaddr.
 * 
 * @exception nplex_exception If the address is invalid. 
 */
static struct sockaddr_storage get_sockaddr(uv_loop_t *loop, const nplex::addr_t &addr)
{
    using namespace nplex;

    int rc = 0;
    struct sockaddr_storage ret;

    std::memset(&ret, 0, sizeof(ret));

    switch(addr.family())
    {
        case AF_INET:
            if ((rc = uv_ip4_addr(addr.host().c_str(), addr.port(), (struct sockaddr_in*) &ret)) != 0)
                throw nplex_exception(uv_strerror(rc));
            break;

        case AF_INET6:
            if ((rc = uv_ip6_addr(addr.host().c_str(), addr.port(), (struct sockaddr_in6*) &ret)) != 0)
                throw nplex_exception(uv_strerror(rc));
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
            std::string port = std::to_string(addr.port());
            if ((rc = uv_getaddrinfo(loop, &req, NULL, addr.host().c_str(), port.c_str(), &hints)) != 0)
                throw nplex_exception(uv_strerror(rc));

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

static void cb_tcp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    using namespace nplex;
    UNUSED(suggested_size);

    connection_t *con = (connection_t *) handle;

    buf->base = con->input_buffer;
    buf->len = sizeof(con->input_buffer);
}

static void cb_tcp_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    using namespace nplex;

    connection_t *con = (connection_t *) stream;

    if (nread == UV_EOF || buf->base == NULL) {
        con->disconnect(ERR_CLOSED_BY_PEER);
        return;
    }

    if (nread < 0) {
        con->disconnect((int) nread);
        return;
    }

    con->input_msg.append(buf->base, static_cast<std::size_t>(nread));

    while (con->input_msg.size() >= sizeof(output_msg_t::len))
    {
        const char *ptr = con->input_msg.c_str();
        std::uint32_t len = ntohl(*((const std::uint32_t *) ptr));

        if (len > con->params.max_msg_bytes) {
            con->disconnect(ERR_MSG_SIZE);
            return;
        }

        if (con->input_msg.size() < len)
            break;

        auto msg = parse_network_msg(ptr, len);

        if (!msg) {
            con->disconnect(ERR_MSG_ERROR);
            return;
        }

        con->client()->on_msg_received(con, msg);

        con->input_msg.erase(0, len);
    }
}

static void cb_tcp_connect(uv_connect_t *req, int status)
{
    using namespace nplex;

    connection_t *con = (connection_t *) req->handle;

    free(req);

    if (status < 0) {
        con->disconnect(status);
        return;
    }

    uv_read_start((uv_stream_t *) con, ::cb_tcp_alloc, ::cb_tcp_read);

    con->state = connection_t::state_e::CONNECTED;

    con->client()->on_connection_established(con);
}

static void cb_tcp_close(uv_handle_t *handle)
{
    using namespace nplex;

    connection_t *con = (connection_t *) handle;

    con->state = connection_t::state_e::CLOSED;
    con->client()->on_connection_closed(con);
}

static void cb_tcp_write(uv_write_t *req, int status)
{
    using namespace nplex;

    auto msg = std::unique_ptr<output_msg_t>((output_msg_t *) req);
    auto *con = (connection_t *) req->handle;

    assert(con->stats.unack_msgs > 0);
    assert(con->stats.unack_bytes >= msg->length());

    con->stats.unack_msgs--;
    con->stats.unack_bytes -= msg->length();
    con->stats.sent_msgs++;
    con->stats.sent_bytes += msg->length();

    if (status < 0) {
        con->disconnect(status);
        return;
    }

    auto *ptr = flatbuffers::GetRoot<nplex::msgs::Message>(msg->content.data());
    assert(ptr);

    con->client()->on_msg_delivered(con, ptr);
}

// ==========================================================
// connection_t methods
// ==========================================================

nplex::connection_t::connection_t(const addr_t &addr_, uv_loop_t *loop_, const params_t &params_) : addr(addr_)
{
    if (addr.port() == 0)
        throw nplex_exception("Invalid address: {}", addr.str());

    state = state_e::CLOSED;
    error = 0;

    params.max_msg_bytes = (params_.max_msg_bytes == 0 ? UINT32_MAX : params_.max_msg_bytes);
    params.max_unack_msgs = (params_.max_unack_msgs == 0 ? UINT32_MAX : params_.max_unack_msgs);
    params.max_unack_bytes = (params_.max_unack_bytes == 0 ? UINT32_MAX : params_.max_unack_bytes);

    tcp.loop = loop_;
    tcp.data = this;
}

nplex::connection_t::~connection_t()
{
    assert(state == state_e::CLOSED);
}

void nplex::connection_t::connect()
{
    int rc = 0;

    if (state != state_e::CLOSED)
        throw nplex_exception("Trying to connect a non-closed connection");

    error = 0;
    state = state_e::CONNECTING;

    struct sockaddr_storage addr_in = ::get_sockaddr(tcp.loop, addr);

    if ((rc = uv_tcp_init(tcp.loop, &tcp)) != 0)
        throw nplex_exception(uv_strerror(rc));

    tcp.data = this;

    uv_connect_t* connect = NULL;
    if ((connect = (uv_connect_t*) malloc(sizeof(uv_connect_t))) == NULL)
        throw std::bad_alloc();

    if ((rc = uv_tcp_connect(connect, &tcp, (const struct sockaddr*) &addr_in, ::cb_tcp_connect)) != 0) {
        free(connect);
        throw nplex_exception(uv_strerror(rc));
    }
}

void nplex::connection_t::disconnect(int rc)
{
    if (state == state_e::CLOSED)
        return;

    if (!error)
        error = rc;

    uv_close((uv_handle_t *) &tcp, ::cb_tcp_close);
}

void nplex::connection_t::send(flatbuffers::DetachedBuffer &&buf)
{
    auto len = get_msg_length(buf);

    if (stats.unack_msgs >= params.max_unack_msgs)
        throw nplex_exception("Output message queue is full");

    if (len > params.max_msg_bytes)
        throw nplex_exception("Message too large");

    if (stats.unack_bytes + len >= params.max_unack_bytes)
        throw nplex_exception("Too many output unacked bytes");

    auto *msg = new output_msg_t(std::move(buf));

    assert(len == msg->length());

    uv_write(&msg->req, (uv_stream_t *) &tcp, msg->buf.data(), (unsigned int) msg->buf.size(), ::cb_tcp_write);

    stats.unack_msgs++;
    stats.unack_bytes += static_cast<std::uint32_t>(len);
}

std::string nplex::connection_t::strerror() const
{
    if (error < 0)
        return uv_strerror(error);

    switch (error)
    {
        case ERR_CLOSED_BY_LOCAL: return "closed by local";
        case ERR_CLOSED_BY_PEER: return "closed by peer";
        case ERR_MSG_ERROR: return "invalid message";
        case ERR_MSG_UNEXPECTED: return "unexpected message";
        case ERR_MSG_SIZE: return "message too large";
        case ERR_ALREADY_CONNECTED: return "already connected";
        case ERR_KEEPALIVE: return "keepalive not received";
        case ERR_AUTH: return "unauthorized";
        default: return fmt::format("unknow error -{}-", error);
    }
}
