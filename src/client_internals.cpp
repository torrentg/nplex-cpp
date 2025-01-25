#include "cppcrc.h"
#include "nplex-cpp/exception.hpp"
#include "transaction_impl.hpp"
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

flatbuffers::DetachedBuffer nplex::create_login_msg(std::size_t cid, const std::string &user, const std::string &password)
{
    using namespace msgs;
    using namespace flatbuffers;

    FlatBufferBuilder builder;

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
    using namespace flatbuffers;

    FlatBufferBuilder builder;

    auto msg = CreateMessage(builder, 
        MsgContent::LOAD_REQUEST,
        CreateLoadRequest(builder, 
            cid, 
            mode,
            rev
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer nplex::create_submit_msg(std::size_t cid, rev_t crev, bool force, const tx_impl_ptr &tx)
{
    using namespace msgs;
    using namespace flatbuffers;

    FlatBufferBuilder builder;
    std::vector<Offset<KeyValue>> upserts_v;
    std::vector<Offset<String>> deletes_v;
    std::vector<Offset<Acl>> ensures_v;

    for (const auto &item : tx->items())
    {
        switch (std::get<transaction_impl_t::action_e>(item.second))
        {
            case transaction_impl_t::action_e::DELETE:
                deletes_v.push_back(builder.CreateString(item.first));
                break;

            case transaction_impl_t::action_e::UPSERT:
                upserts_v.push_back(
                    CreateKeyValue(
                        builder, 
                        builder.CreateString(item.first), 
                        builder.CreateVector(
                            (uint8_t *) std::get<value_ptr>(item.second)->data().c_str(), 
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
        ensures_v.push_back(
            CreateAcl(
                builder, 
                builder.CreateString(item.first), 
                item.second
            )
        );
    }

    auto msg = CreateMessage(builder, 
        MsgContent::SUBMIT_REQUEST, 
        CreateSubmitRequest(builder, 
            cid,
            (tx->isolation() == transaction_t::isolation_e::SERIALIZABLE ? tx->rev_creation() : crev),
            tx->type(),
            builder.CreateVector(upserts_v),
            builder.CreateVector(deletes_v),
            builder.CreateVector(ensures_v),
            force
        ).Union()
    );

    builder.Finish(msg);
    return builder.Release();
}

const nplex::msgs::Message * nplex::parse_network_msg(const char *ptr, size_t len)
{
    using namespace nplex;
    using namespace nplex::msgs;
    using namespace flatbuffers;

    if (len <= 3 * sizeof(std::uint32_t))
        return nullptr;

    if (len != ntohl(*((const uint32_t *) ptr)))
        return nullptr;

    std::uint32_t metadata = ntohl(*((const uint32_t *) (ptr + sizeof(std::uint32_t))));
    // TODO: uncompress if (metadata & LZ4)
    UNUSED(metadata);

    std::uint32_t checksum = ntohl(*((const std::uint32_t *) (ptr + len - sizeof(std::uint32_t))));

    if (checksum != CRC32::CRC32::calc(reinterpret_cast<const uint8_t *>(ptr), len - sizeof(std::uint32_t)))
        return nullptr;

    ptr += 2 * sizeof(std::uint32_t);
    len -= 3 * sizeof(std::uint32_t);

    auto verifier = flatbuffers::Verifier((const uint8_t *) ptr, len);
    verifier.VerifyBuffer<nplex::msgs::Message>(nullptr);

    return flatbuffers::GetRoot<nplex::msgs::Message>(ptr);
}
