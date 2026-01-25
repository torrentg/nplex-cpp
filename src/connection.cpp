#include <cassert>
#include <arpa/inet.h>
#include "nplex-cpp/exception.hpp"
#include "errors.hpp"
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
    UNUSED(suggested_size);

    auto obj = reinterpret_cast<nplex::connection_s *>(handle);

    buf->base = obj->m_input_buffer;
    buf->len = sizeof(obj->m_input_buffer);
}

static void cb_tcp_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    using namespace nplex;

    connection_s *obj = (connection_s *) stream;

    if (nread == UV_EOF || buf->base == NULL) {
        obj->disconnect(ERR_CLOSED_BY_PEER);
        return;
    }

    if (nread < 0) {
        obj->disconnect((int) nread);
        return;
    }

    obj->m_input_msg.append(buf->base, static_cast<std::size_t>(nread));
    while (obj->m_input_msg.size() >= sizeof(output_msg_t::len))
    {
        const char *ptr = obj->m_input_msg.c_str();
        std::uint32_t len = ntohl(*((const std::uint32_t *) ptr));

        if (len > obj->m_params.max_msg_bytes) {
            obj->disconnect(ERR_MSG_SIZE);
            return;
        }

        if (obj->m_input_msg.size() < len)
            break;

        auto msg = parse_network_msg(ptr, len);

        if (!msg) {
            obj->disconnect(ERR_MSG_ERROR);
            return;
        }

        obj->client()->on_msg_received(obj->connection(), msg);
        obj->m_input_msg.erase(0, len);
    }
}

static void cb_tcp_connect(uv_connect_t *req, int status)
{
    using namespace nplex;

    connection_s *obj = (connection_s *) req->handle;

    free(req);

    if (status < 0) {
        obj->disconnect(status);
        return;
    }

    uv_read_start((uv_stream_t *) obj, ::cb_tcp_alloc, ::cb_tcp_read);
    obj->m_state = connection_s::state_e::CONNECTED;

    obj->client()->on_connection_established(obj->connection());
}

static void cb_tcp_close(uv_handle_t *handle)
{
    using namespace nplex;

    connection_s *obj = (connection_s *) handle;

    obj->m_state = connection_s::state_e::CLOSED;
    obj->client()->on_connection_closed(obj->connection());
}

static void cb_tcp_write(uv_write_t *req, int status)
{
    using namespace nplex;

    auto msg = std::unique_ptr<output_msg_t>((output_msg_t *) req);
    auto *obj = (connection_s *) req->handle;

    assert(obj->m_stats.unack_msgs > 0);
    assert(obj->m_stats.unack_bytes >= msg->length());

    obj->m_stats.unack_msgs--;
    obj->m_stats.unack_bytes -= msg->length();
    obj->m_stats.sent_msgs++;
    obj->m_stats.sent_bytes += msg->length();

    if (status < 0) {
        obj->disconnect(status);
        return;
    }

    auto *ptr = flatbuffers::GetRoot<nplex::msgs::Message>(msg->content.data());
    assert(ptr);

    obj->client()->on_msg_delivered(obj->connection(), ptr);
}

// ==========================================================
// connection_s methods
// ==========================================================

nplex::connection_s::connection_s(const addr_t &addr, uv_loop_t *loop, const params_t &params) : m_addr(addr)
{
    if (m_addr.port() == 0)
        throw nplex_exception("Invalid address: {}", m_addr.str());

    m_state = state_e::CLOSED;
    m_error = 0;

    m_params.max_msg_bytes = (params.max_msg_bytes == 0 ? UINT32_MAX : params.max_msg_bytes);
    m_params.max_unack_msgs = (params.max_unack_msgs == 0 ? UINT32_MAX : params.max_unack_msgs);
    m_params.max_unack_bytes = (params.max_unack_bytes == 0 ? UINT32_MAX : params.max_unack_bytes);

    m_tcp.loop = loop;
    m_tcp.data = this;
}

nplex::connection_s::~connection_s()
{
    assert(m_state == state_e::CLOSED);
}

bool nplex::connection_s::is_closed() const
{
    return m_state == state_e::CLOSED;
}

void nplex::connection_s::connect()
{
    int rc = 0;

    if (m_state != state_e::CLOSED)
        throw nplex_exception("Trying to connect a non-closed connection");

    m_error = 0;
    m_state = state_e::CONNECTING;
    struct sockaddr_storage addr_in = ::get_sockaddr(m_tcp.loop, m_addr);

    if ((rc = uv_tcp_init(m_tcp.loop, &m_tcp)) != 0)
        throw nplex_exception(uv_strerror(rc));

    m_tcp.data = this;

    uv_connect_t* connect = NULL;
    if ((connect = (uv_connect_t*) malloc(sizeof(uv_connect_t))) == NULL)
        throw std::bad_alloc();

    if ((rc = uv_tcp_connect(connect, &m_tcp, (const struct sockaddr*) &addr_in, ::cb_tcp_connect)) != 0) {
        free(connect);
        throw nplex_exception(uv_strerror(rc));
    }
}

void nplex::connection_s::disconnect(int rc)
{
    if (m_state == state_e::CLOSED)
        return;

    if (!m_error)
        m_error = rc;

    uv_close((uv_handle_t *) &m_tcp, ::cb_tcp_close);
}

void nplex::connection_s::send(flatbuffers::DetachedBuffer &&buf)
{
    auto len = get_msg_length(buf);

    if (m_stats.unack_msgs >= m_params.max_unack_msgs)
        throw nplex_exception("Output message queue is full");

    if (len > m_params.max_msg_bytes)
        throw nplex_exception("Message too large");

    if (m_stats.unack_bytes + len >= m_params.max_unack_bytes)
        throw nplex_exception("Too many output unacked bytes");

    auto *msg = new output_msg_t(std::move(buf));

    assert(len == msg->length());

    uv_write(&msg->req, (uv_stream_t *) &m_tcp, msg->buf.data(), (unsigned int) msg->buf.size(), ::cb_tcp_write);

    m_stats.unack_msgs++;
    m_stats.unack_bytes += static_cast<std::uint32_t>(len);
}

// ==========================================================
// connection_t methods
// ==========================================================
