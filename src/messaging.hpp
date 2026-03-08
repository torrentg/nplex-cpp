#pragma once

#include <array>
#include <string>
#include <memory>
#include <uv.h>
#include "nplex-cpp/types.hpp"
#include "messages.hpp"

/**
 * Collection of classes and functions used by client_impl and connection.
 */

namespace nplex {

// Forward declarations
class transaction_impl;
using tx_impl_ptr = std::shared_ptr<transaction_impl>;

struct output_msg_t
{
    uv_write_t req;
    std::array<uv_buf_t, 4> buf;
    flatbuffers::DetachedBuffer content;
    std::uint32_t metadata;     // 0=none, 1=lz4 (big-endian)
    std::uint32_t checksum;     // CRC32 of len + metadata + content (big-endian)
    std::uint32_t len;          // Total message length (including len, metadata, content, checksum) (big-endian)

    output_msg_t(flatbuffers::DetachedBuffer &&msg);
    std::uint32_t length() const { return ntohl(len); }
};

inline std::size_t get_msg_length(const flatbuffers::DetachedBuffer &buf) noexcept {
    return buf.size() + 3 * sizeof(std::uint32_t);
}

flatbuffers::DetachedBuffer create_ping_msg(std::size_t cid, const std::string &payload);
flatbuffers::DetachedBuffer create_login_msg(std::size_t cid, const std::string &user, const std::string &password);
flatbuffers::DetachedBuffer create_snapshot_msg(std::size_t cid, rev_t rev);
flatbuffers::DetachedBuffer create_updates_msg(std::size_t cid, rev_t rev);
flatbuffers::DetachedBuffer create_submit_msg(std::size_t cid, rev_t crev, bool force, const tx_impl_ptr &tx);

const nplex::msgs::Message * parse_network_msg(const char *ptr, size_t len);

} // namespace nplex
