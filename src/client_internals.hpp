#pragma once

#include <array>
#include <string>
#include <memory>
#include <variant>
#include <arpa/inet.h>
#include <flatbuffers/flatbuffers.h>
#include <uv.h>
#include "nplex-cpp/types.hpp"
#include "transaction_impl.hpp"
#include "messages.hpp"
#include "addr.hpp"

#define UNUSED(x) (void)(x)

/**
 * Collection of classes and functions used by client_t::impl_t.
 */

namespace nplex {

struct shared_ptr_less_t
{
    using is_transparent = std::true_type;

    template<typename T1, typename T2>
    bool operator()(const std::shared_ptr<T1> &lhs, const std::shared_ptr<T2> &rhs) const { 
        return (static_cast<void*>(lhs.get()) < static_cast<void *>(rhs.get()));
    }
};

struct output_msg_t
{
    uv_write_t req;
    std::array<uv_buf_t, 4> buf;
    flatbuffers::DetachedBuffer content;
    std::uint32_t metadata;     // 0=none, 1=lz4 (big-endian)
    std::uint32_t checksum;     // CRC32 of len + metadata + content (big-endian)
    std::uint32_t len;          // Total message length (including len, metadata, content, checksum) (big-endian)

    output_msg_t(flatbuffers::DetachedBuffer &&content_);
    std::uint32_t length() const { return ntohl(len); }
};

struct submit_cmd_t {
    tx_impl_ptr tx;
    bool force;
};

struct close_cmd_t {};

struct ping_cmd_t {
    std::string payload;
};

using command_t = std::variant<submit_cmd_t, close_cmd_t, ping_cmd_t>;

struct sockaddr_storage get_sockaddr(uv_loop_t *loop, const addr_t &addr);
flatbuffers::DetachedBuffer create_login_msg(std::size_t cid, const std::string &user, const std::string &password);
flatbuffers::DetachedBuffer create_load_msg(std::size_t cid, msgs::LoadMode mode, rev_t rev);
flatbuffers::DetachedBuffer create_submit_msg(std::size_t cid, rev_t crev, bool force, const tx_impl_ptr &tx);
const nplex::msgs::Message * parse_network_msg(const char *ptr, size_t len);

void cb_process_async(uv_async_t *handle);
void cb_close_handle(uv_handle_t *handle, void *arg);
void cb_tcp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
void cb_tcp_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void cb_tcp_write(uv_write_t *req, int status);
void cb_tcp_connect(uv_connect_t *req, int status);

} // namespace nplex
