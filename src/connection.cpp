#include <cassert>
#include <arpa/inet.h>
#include <uv.h>
#include <fmt/core.h>
#include "nplex-cpp/exception.hpp"
#include "nplex-cpp/params.hpp"
#include "client_impl.hpp"
#include "connection.hpp"
#include "utils.hpp"

template <typename T>
static auto get_handle(T* obj) -> decltype(reinterpret_cast<std::conditional_t<std::is_const<T>::value, const uv_handle_t*, uv_handle_t*>>(obj)) {
    return reinterpret_cast<std::conditional_t<std::is_const<T>::value, const uv_handle_t*, uv_handle_t*>>(obj);
}

template <typename T>
static auto get_stream(T* obj) -> decltype(reinterpret_cast<std::conditional_t<std::is_const<T>::value, const uv_stream_t*, uv_stream_t*>>(obj)) {
    return reinterpret_cast<std::conditional_t<std::is_const<T>::value, const uv_stream_t*, uv_stream_t*>>(obj);
}

namespace nplex {

/**
 * Class implementing the connection interface.
 * 
 * All connection implementation details are hidden from the user.
 * All members are accessible by static libuv callbacks.
 */
struct connection_impl_t : public connection_t
{
    enum class state_e : std::uint8_t {
        CLOSED,
        CONNECTING,
        CONNECTED
    };

    uv_tcp_t m_tcp = {};                            // Libuv tcp handle (must be first)
    addr_t m_addr;                                  // Remote address (server address)
    char m_input_buffer[UINT16_MAX] = {0};          // Input buffer used by read()
    std::string m_input_msg;                        // Current incoming message
    int m_error = 0;                                // Disconnection cause
    state_e m_state = state_e::CLOSED;              // Connection state

    struct {
        std::size_t max_unack_msgs = 0;             // Max unacknowledged messages
        std::size_t max_unack_bytes = 0;            // Max unacknowledged bytes
        std::size_t max_msg_bytes = 0;              // Max message size (input and output)
    } m_params;

    struct {
        std::size_t unack_msgs = 0;                 // Unacknowledged messages
        std::size_t unack_bytes = 0;                // Unacknowledged bytes
        std::size_t recv_msgs = 0;                  // Total received messages
        std::size_t recv_bytes = 0;                 // Total received bytes
        std::size_t sent_msgs = 0;                  // Total sent messages
        std::size_t sent_bytes = 0;                 // Total sent bytes
    } m_stats;

    connection_impl_t(const addr_t &addr, uv_loop_t *loop, const params_t &params);
    virtual ~connection_impl_t() override;

    virtual const addr_t & addr() const override { return m_addr; }
    virtual bool is_connected() const override { return (m_state == state_e::CONNECTED); }
    virtual bool is_closed() const override { return (m_state == state_e::CLOSED); }
    virtual int error() const override { return m_error; }

    virtual void connect() override;
    virtual void disconnect(int rc = 0) override;
    virtual void send(flatbuffers::DetachedBuffer &&buf) override;

    client_t::impl_t * client() const { return reinterpret_cast<client_t::impl_t *>(m_tcp.loop->data); }
};

} // namespace nplex

// ==========================================================
// Internal (static) functions
// ==========================================================

/**
 * Convert an string address to a sockaddr.
 * 
 * This is a blocking function (eventual DNS call).
 * 
 * @param[in] loop Event loop.
 * @param[in] addr Address to convert.
 * 
 * @return The sockaddr.
 * 
 * @exception nplex_exception If the address is invalid. 
 */
static struct sockaddr_storage get_sockaddr(uv_loop_t *loop, const nplex::addr_t &addr)
{
    using namespace nplex;

    int rc = 0;
    struct sockaddr_storage ret = {};

    switch (addr.family())
    {
        case AF_INET:
            if ((rc = uv_ip4_addr(addr.host().c_str(), addr.port(), reinterpret_cast<struct sockaddr_in*>(&ret))) != 0)
                throw nplex_exception(uv_strerror(rc));
            break;

        case AF_INET6:
            if ((rc = uv_ip6_addr(addr.host().c_str(), addr.port(), reinterpret_cast<struct sockaddr_in6*>(&ret))) != 0)
                throw nplex_exception(uv_strerror(rc));
            break;

        case AF_UNSPEC:
        {
            uv_getaddrinfo_t req = {};
            struct addrinfo hints = {};

            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            // request address info made synchronously (at this point no other tasks are done)
            std::string port = std::to_string(addr.port());
            if ((rc = uv_getaddrinfo(loop, &req, nullptr, addr.host().c_str(), port.c_str(), &hints)) != 0) {
                if (req.addrinfo) uv_freeaddrinfo(req.addrinfo);
                throw nplex_exception(uv_strerror(rc));
            }

            if (req.addrinfo->ai_family != AF_INET && req.addrinfo->ai_family != AF_INET6) {
                uv_freeaddrinfo(req.addrinfo);
                throw nplex_exception(fmt::format("Invalid address family: {}", addr.str()));
            }

            memcpy(&ret, req.addrinfo->ai_addr, req.addrinfo->ai_addrlen);
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

    auto obj = reinterpret_cast<nplex::connection_impl_t *>(handle->data);

    buf->base = obj->m_input_buffer;
    buf->len = sizeof(obj->m_input_buffer);
}

static void cb_tcp_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    using namespace nplex;

    auto obj = reinterpret_cast<nplex::connection_impl_t *>(stream->data);

    if (nread == 0)
        return;

    if (nread == UV_EOF || buf->base == nullptr) {
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
        const char *ptr = obj->m_input_msg.data();
        std::uint32_t len = ntohl_ptr(ptr);

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

        obj->m_stats.recv_msgs++;
        obj->m_stats.recv_bytes += len;
        obj->client()->on_msg_received(obj, msg);

        obj->m_input_msg.erase(0, len);
    }
}

static void cb_tcp_connect(uv_connect_t *req, int status)
{
    auto obj = reinterpret_cast<nplex::connection_impl_t *>(req->handle->data);

    delete req;

    if (status < 0) {
        obj->disconnect(status);
        return;
    }

    obj->m_state = nplex::connection_impl_t::state_e::CONNECTED;
    uv_read_start(get_stream(&obj->m_tcp), ::cb_tcp_alloc, ::cb_tcp_read);

    obj->client()->on_connection_established(obj);
}

static void cb_tcp_close(uv_handle_t *handle)
{
    auto *obj = reinterpret_cast<nplex::connection_impl_t *>(handle->data);

    obj->m_state = nplex::connection_impl_t::state_e::CLOSED;
    obj->client()->on_connection_closed(obj);
}

static void cb_tcp_write(uv_write_t *req, int status)
{
    using namespace nplex;

    auto msg = std::unique_ptr<output_msg_t>(reinterpret_cast<output_msg_t *>(req));
    auto obj = reinterpret_cast<connection_impl_t *>(req->handle->data);

    assert(msg);
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

    auto ptr = flatbuffers::GetRoot<nplex::msgs::Message>(msg->content.data());
    assert(ptr);

    obj->client()->on_msg_delivered(obj, ptr);
}

// ==========================================================
// connection_impl_t methods
// ==========================================================

nplex::connection_impl_t::connection_impl_t(const addr_t &addr, uv_loop_t *loop, const params_t &params) : m_addr(addr)
{
    if (m_addr.port() == 0)
        throw nplex_exception("Invalid address: {}", m_addr.str());

    m_params.max_msg_bytes = (params.max_msg_bytes == 0 ? UINT32_MAX : params.max_msg_bytes);
    m_params.max_unack_msgs = (params.max_unack_msgs == 0 ? UINT32_MAX : params.max_unack_msgs);
    m_params.max_unack_bytes = (params.max_unack_bytes == 0 ? UINT32_MAX : params.max_unack_bytes);

    m_tcp.loop = loop;
    m_tcp.data = this;
}

nplex::connection_impl_t::~connection_impl_t()
{
    assert(is_closed());
}

void nplex::connection_impl_t::connect()
{
    int rc = 0;

    if (!is_closed())
        throw nplex_exception("Trying to connect a non-closed connection");

    assert(is_closed());

    m_state = state_e::CONNECTING;
    m_input_msg.clear();
    m_stats = {};
    m_error = 0;

    struct sockaddr_storage addr_in = ::get_sockaddr(m_tcp.loop, m_addr);

    if ((rc = uv_tcp_init(m_tcp.loop, &m_tcp)) != 0)
        throw nplex_exception(uv_strerror(rc));

    m_tcp.data = this;

    uv_connect_t* connect = new uv_connect_t();

    if ((rc = uv_tcp_connect(connect, &m_tcp, reinterpret_cast<struct sockaddr*>(&addr_in), ::cb_tcp_connect)) != 0) {
        delete connect;
        throw nplex_exception(uv_strerror(rc));
    }
}

void nplex::connection_impl_t::disconnect(int rc)
{
    if (is_closed())
        return;

    if (!m_error)
        m_error = rc;

    if (!uv_is_closing(get_handle(&m_tcp)))
        uv_close(get_handle(&m_tcp), ::cb_tcp_close);
}

void nplex::connection_impl_t::send(flatbuffers::DetachedBuffer &&buf)
{
    int rc = 0;
    auto len = get_msg_length(buf);

    if (m_state != state_e::CONNECTED)
        throw nplex_exception("Connection is not established");

    if (m_stats.unack_msgs >= m_params.max_unack_msgs)
        throw nplex_exception("Output message queue is full");

    if (len > m_params.max_msg_bytes)
        throw nplex_exception("Message too large");

    if (m_stats.unack_bytes + len >= m_params.max_unack_bytes)
        throw nplex_exception("Too many output unacked bytes");

    auto msg = new output_msg_t(std::move(buf));
    assert(len == msg->length());

    if ((rc = uv_write(&msg->req, get_stream(&m_tcp), msg->buf.data(), static_cast<unsigned int>(msg->buf.size()), ::cb_tcp_write)) != 0) {
        delete msg;
        disconnect(rc);
        return;
    }

    m_stats.unack_msgs++;
    m_stats.unack_bytes += len;
}

// ==========================================================
// connection_t methods
// ==========================================================

std::unique_ptr<nplex::connection_t> nplex::connection_t::create(const addr_t &addr, uv_loop_t *loop, const params_t &params)
{
    return std::make_unique<connection_impl_t>(addr, loop, params);
}
