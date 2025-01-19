#include "cppcrc.h"
#include "nplex-cpp/exception.hpp"
#include "client_impl.hpp"
#include "client_internals.hpp"

/**
 * Notes on this compilation unit:
 * 
 * When we use the libuv library we apply C conventions (instead of C++ ones):
 *   - When in Rome, do as the Romans do.
 *   - calloc/free are used instead of new/delete.
 *   - pointers to static functions.
 *   - C-style pointer casting.
 */

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

struct sockaddr_storage nplex::get_sockaddr(uv_loop_t *loop, const addr_t &addr)
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

flatbuffers::DetachedBuffer nplex::create_login_msg(std::size_t cid, const std::string &user, const std::string &password)
{
    using namespace msgs;

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

flatbuffers::DetachedBuffer nplex::create_load_msg(std::size_t cid, msgs::LoadMode mode, rev_t rev)
{
    using namespace msgs;

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

void nplex::cb_process_async(uv_async_t *handle)
{
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) handle->data;
    impl->process_commands();
}

void nplex::cb_close_handle(uv_handle_t *handle, void *arg)
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

void nplex::cb_tcp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    UNUSED(suggested_size);
    uv_tcp_t *con = (uv_tcp_t *) handle;
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) con->loop->data;

    buf->base = impl->input_buffer;
    buf->len = sizeof(impl->input_buffer);
}

void nplex::cb_tcp_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    UNUSED(stream);
    UNUSED(nread);
    UNUSED(buf);

    uv_tcp_t *con = (uv_tcp_t *) stream;
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) con->loop->data;
    char *ptr = buf->base;
    size_t len = buf->len;

    if (nread < 0 || buf->base == NULL) {
        impl->error = fmt::format("Tcp read error: {}", uv_strerror(nread));
        impl->close();
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

void nplex::cb_tcp_write(uv_write_t *req, int status)
{
    output_msg_t *msg = (output_msg_t *) req;
    uv_tcp_t *con = (uv_tcp_t *) req->handle;
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) con->loop->data;

    // assert(XXX);
    // // TODO: pop message
    // output_queue_release(con, tcp_msg);

    if (status < 0) {
        impl->error = fmt::format("Failed to write to {}: {}", impl->server_addr.str(), uv_strerror(status));
        impl->disconnect();
    }
}

void nplex::cb_tcp_connect(uv_connect_t *req, int status)
{
    uv_tcp_t *con = (uv_tcp_t *) req->handle;
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) con->loop->data;

    free(req);

    if (status < 0) {
        std::string msg = fmt::format("Failed to connect to {}: {}", impl->server_addr.str(), uv_strerror(status));
        impl->close();
        return;
    }

    uv_read_start((uv_stream_t *) con, cb_tcp_alloc, cb_tcp_read);

    impl->do_login();
}
