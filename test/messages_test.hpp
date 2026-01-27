#pragma once

#include <vector>
#include <memory>
#include <doctest.h>
#include "messages.hpp"

namespace nplex {
namespace tests {

    template <class T>
    std::vector<std::unique_ptr<T>> make_vector_unique_ptr(const std::vector<T>& input)
    {
        std::vector<std::unique_ptr<T>> result;
        result.reserve(input.size());
        for (const auto& item : input) {
            result.push_back(std::make_unique<T>(item));
        }
        return result;
    }

    inline nplex::msgs::UpdateT make_update(std::size_t rev, const char *user, std::uint64_t timestamp, std::uint32_t type, const std::vector<nplex::msgs::KeyValueT> &upserts = {}, const std::vector<std::string> &deletes = {})
    {
        nplex::msgs::UpdateT tx;
        tx.rev = rev;
        tx.user = user;
        tx.timestamp = timestamp;
        tx.type = type;
        tx.upserts = make_vector_unique_ptr(upserts);
        tx.deletes = deletes;
        return tx;
    }

    inline nplex::msgs::SnapshotT make_snapshot(std::size_t rev, const std::vector<nplex::msgs::UpdateT> &updates)
    {
        nplex::msgs::SnapshotT snapshot;
        snapshot.rev = rev;
        snapshot.updates = make_vector_unique_ptr(updates);
        return snapshot;
    }

    inline nplex::msgs::SnapshotResponseT make_snapshot_resp(std::size_t cid, std::size_t crev, std::size_t rev0, bool accepted, const nplex::msgs::SnapshotT &snapshot)
    {
        nplex::msgs::SnapshotResponseT resp;
        resp.cid = cid;
        resp.crev = crev;
        resp.rev0 = rev0;
        resp.accepted = accepted;
        resp.snapshot = std::make_unique<nplex::msgs::SnapshotT>(snapshot);
        return resp;
    }

    inline nplex::msgs::UpdatesPushT make_updates_push(std::size_t cid, std::size_t crev, const nplex::msgs::UpdateT &update)
    {
        std::vector<std::unique_ptr<nplex::msgs::UpdateT>> updates;

        updates.push_back(std::make_unique<nplex::msgs::UpdateT>(update));

        nplex::msgs::UpdatesPushT push;
        push.cid = cid;
        push.crev = crev;
        push.updates = std::move(updates);
        return push;
    }

    inline nplex::msgs::SubmitRequestT make_submit_request(std::size_t cid, std::size_t crev, std::uint32_t type, const std::vector<nplex::msgs::KeyValueT> &upserts, const std::vector<std::string> &deletes, const std::vector<std::string> &ensures)
    {
        nplex::msgs::SubmitRequestT req;
        req.cid = cid;
        req.crev = crev;
        req.type = type;
        req.upserts = make_vector_unique_ptr(upserts);
        req.deletes = deletes;
        req.ensures = ensures;
        return req;
    }

    template<typename T>
    inline nplex::msgs::MessageT make_message(T &&val)
    {
        nplex::msgs::MessageT msg;
        msg.content.Set(std::move(val));
        return msg;
    }

    template <class T>
    flatbuffers::DetachedBuffer serialize(const T &obj)
    {
        flatbuffers::FlatBufferBuilder builder;
        auto offset = T::TableType::Pack(builder, &obj);
        builder.Finish(offset);
        flatbuffers::DetachedBuffer ret = builder.Release();
        flatbuffers::Verifier verifier(ret.data(), ret.size());
        REQUIRE(verifier.VerifyBuffer<typename T::TableType>());
        return ret;
    }

} // namespace tests
} // namespace nplex
