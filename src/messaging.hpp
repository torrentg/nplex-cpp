#pragma once

#include <array>
#include <string>
#include <uv.h>
#include "nplex-cpp/types.hpp"
#include "messages.hpp"

namespace nplex {

// Forward declarations
class transaction_impl;
using tx_impl_ptr = std::shared_ptr<transaction_impl>;

/**
 * Network message format:
 * 
 *  +--------------+--------------+-------------------+--------------+
 *  | length       | metadata     | content           | checksum     |
 *  +--------------+--------------+-------------------+--------------+
 *  | 4 bytes      | 4 bytes      | N bytes           | 4 bytes      |
 *  | big-endian   | big-endian   | flatbuffer        | big-endian   |
 *  +--------------+--------------+-------------------+--------------+
 *                                |<-- length - 12 -->|
 * 
 * - length: Total message size in bytes (including all 4 fields)
 * - metadata: Message properties (currently unused)
 * - content: FlatBuffer serialized data (content_length = msg_length - 12)
 * - checksum: CRC32 of [length + metadata + content]
 */
struct output_msg_t
{
    uv_write_t req;
    std::array<uv_buf_t, 4> buf;
    flatbuffers::DetachedBuffer content;
    std::uint32_t metadata;     // Message metadata (ex. compression alg) -currently unused- (big-endian)
    std::uint32_t checksum;     // CRC32 of len + metadata + content (big-endian)
    std::uint32_t len;          // Total message length (including len, metadata, content, checksum) (big-endian)

    output_msg_t(flatbuffers::DetachedBuffer &&msg);
    std::uint32_t length() const { return ntohl(len); }

    static inline std::size_t length(const flatbuffers::DetachedBuffer &buf) noexcept {
        return buf.size() + 3 * sizeof(std::uint32_t);
    }
};

/**
 * Parse a network message.
 * 
 * Checks the message integrity and returns the flatbuffer message.
 * 
 * @param[in] ptr Pointer to received data.
 * @param[in] len Length of received data.
 * 
 * @return The message or 
 *         nullptr on error.
 */
const msgs::Message * parse_network_msg(const char *ptr, size_t len);

/**
 * Helper functions to create messages.
 */
flatbuffers::DetachedBuffer create_ping_msg(std::size_t cid, const std::string &payload);
flatbuffers::DetachedBuffer create_login_msg(std::size_t cid, const std::string &user, const std::string &password);
flatbuffers::DetachedBuffer create_snapshot_msg(std::size_t cid, rev_t rev);
flatbuffers::DetachedBuffer create_updates_msg(std::size_t cid, rev_t rev);
flatbuffers::DetachedBuffer create_submit_msg(std::size_t cid, rev_t crev, bool force, const tx_impl_ptr &tx);
flatbuffers::DetachedBuffer create_sessions_msg(std::size_t cid, bool stream);

} // namespace nplex
