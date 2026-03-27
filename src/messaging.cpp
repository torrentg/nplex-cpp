#include <cassert>
#include "utf8.h"
#include "cppcrc.h"
#include "nplex-cpp/exception.hpp"
#include "transaction_impl.hpp"
#include "messaging.hpp"
#include "misc.hpp"

#define API_VERSION 10

using namespace nplex::msgs;
using namespace flatbuffers;

// ==========================================================
// nplex functions
// ==========================================================

nplex::output_msg_t::output_msg_t(DetachedBuffer &&msg) : content(std::move(msg))
{
    len = (std::uint32_t)(content.size() + sizeof(len) + sizeof(metadata) + sizeof(checksum));
    len = htonl(len);

    metadata = htonl(0); // not-compressed

    checksum = CRC32::CRC32::calc(reinterpret_cast<const std::uint8_t *>(&len), sizeof(len));
    checksum = CRC32::CRC32::calc(reinterpret_cast<const std::uint8_t *>(&metadata), sizeof(metadata), checksum);
    checksum = CRC32::CRC32::calc(reinterpret_cast<const std::uint8_t *>(content.data()), content.size(), checksum);
    checksum = htonl(checksum);

    buf[0] = uv_buf_init(reinterpret_cast<char *>(&len), sizeof(len));
    buf[1] = uv_buf_init(reinterpret_cast<char *>(&metadata), sizeof(metadata));
    buf[2] = uv_buf_init(reinterpret_cast<char *>(content.data()), static_cast<unsigned int>(content.size()));
    buf[3] = uv_buf_init(reinterpret_cast<char *>(&checksum), sizeof(checksum));
}

const nplex::msgs::Message * nplex::parse_network_msg(const char *ptr, size_t len)
{
    if (len <= 3 * sizeof(std::uint32_t))
        return nullptr;

    if (len != ntohl_ptr(ptr))
        return nullptr;

    std::uint32_t metadata = ntohl_ptr(ptr + sizeof(std::uint32_t));
    UNUSED(metadata);

    std::uint32_t checksum = ntohl_ptr(ptr + len - sizeof(std::uint32_t));

    if (checksum != CRC32::CRC32::calc(reinterpret_cast<const std::uint8_t *>(ptr), len - sizeof(std::uint32_t)))
        return nullptr;

    ptr += 2 * sizeof(std::uint32_t);
    len -= 3 * sizeof(std::uint32_t);

    auto verifier = flatbuffers::Verifier(reinterpret_cast<const std::uint8_t *>(ptr), len);

    if (!verifier.VerifyBuffer<Message>(nullptr))
        return nullptr;

    return flatbuffers::GetRoot<Message>(ptr);
}

flatbuffers::DetachedBuffer nplex::create_ping_msg(std::size_t cid, const std::string &payload)
{
    FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::PING_REQUEST, 
        CreatePingRequest(builder, 
            cid, 
            builder.CreateString(payload)
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer nplex::create_login_msg(std::size_t cid, const std::string &user, const std::string &password)
{
    FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::LOGIN_REQUEST, 
        CreateLoginRequest(builder, 
            cid, 
            API_VERSION,
            builder.CreateString(user), 
            builder.CreateString(password)
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer nplex::create_snapshot_msg(std::size_t cid, rev_t rev)
{
    FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::SNAPSHOT_REQUEST,
        CreateSnapshotRequest(builder, 
            cid, 
            rev
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer nplex::create_updates_msg(std::size_t cid, rev_t rev)
{
    FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::UPDATES_REQUEST,
        CreateUpdatesRequest(builder, 
            cid, 
            rev
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer nplex::create_submit_msg(std::size_t cid, rev_t crev, bool force, const tx_impl_ptr &tx)
{
    FlatBufferBuilder builder;
    std::vector<Offset<KeyValue>> upserts;
    std::vector<Offset<String>> deletes;
    std::vector<Offset<String>> ensures;

    for (const auto &item : tx->items())
    {
        switch (std::get<transaction_impl::action_e>(item.second))
        {
            case transaction_impl::action_e::DELETE:
                deletes.push_back(builder.CreateString(item.first));
                break;

            case transaction_impl::action_e::UPSERT:
                upserts.push_back(
                    CreateKeyValue(
                        builder, 
                        builder.CreateString(item.first), 
                        builder.CreateVector(
                            (std::uint8_t *) std::get<value_ptr>(item.second)->data().c_str(), 
                            std::get<value_ptr>(item.second)->data().size()
                        )
                    )
                );
                break;

            default:
                break;
        }
    }

    for (const auto &item : tx->ensures())
    {
        ensures.push_back(builder.CreateString(item));
    }

    auto msg = CreateMessage(builder, 
        MsgContent::SUBMIT_REQUEST, 
        CreateSubmitRequest(builder, 
            cid,
            (tx->isolation() == transaction::isolation_e::SERIALIZABLE ? tx->rev_creation() : crev),
            tx->type(),
            builder.CreateVector(upserts),
            builder.CreateVector(deletes),
            builder.CreateVector(ensures),
            force
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}
