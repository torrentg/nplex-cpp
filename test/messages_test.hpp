#pragma once

#include <vector>
#include <memory>
#include <type_traits>
#include <doctest.h>
#include "messages.hpp"

namespace nplex {
namespace tests {

    template <class T>
    auto move_to_unique(T&& t) {
        return std::make_unique<std::remove_reference_t<T>>(std::move(t));
    }

    template <class V, class ... Args>
    auto make_vector_unique(Args ... args) {
        std::vector<std::unique_ptr<V>> rv;
        (rv.push_back(move_to_unique(args)), ...);
        return rv;
    }

    template <class T>
    std::vector<std::unique_ptr<T>> make_vector_unique_ptr(const std::vector<T>& input) {
        std::vector<std::unique_ptr<T>> result;
        result.reserve(input.size());
        for (const auto& item : input) {
            result.push_back(std::make_unique<T>(item));
        }
        return result;
    }

    inline nplex::msgs::UpdateT make_update(size_t rev, const char *user, uint64_t timestamp, uint32_t type, std::vector<nplex::msgs::KeyValueT> upserts = {}, std::vector<std::string> deletes = {})
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

    inline nplex::msgs::SnapshotT make_snapshot(size_t rev, std::vector<nplex::msgs::UpdateT> updates)
    {
        nplex::msgs::SnapshotT snapshot;
        snapshot.rev = rev;
        snapshot.updates = make_vector_unique_ptr(updates);
        return snapshot;
    }

    inline nplex::msgs::LoadResponseT make_load_response(size_t cid, size_t crev, bool accepted, const nplex::msgs::SnapshotT &snapshot)
    {
        nplex::msgs::LoadResponseT resp;
        resp.cid = cid;
        resp.crev = crev;
        resp.accepted = accepted;
        resp.snapshot = std::make_unique<nplex::msgs::SnapshotT>(snapshot);
        return resp;
    }

    inline nplex::msgs::UpdatePushT make_update_push(size_t cid, size_t crev, const nplex::msgs::UpdateT &update)
    {
        nplex::msgs::UpdatePushT push;
        push.cid = cid;
        push.crev = crev;
        push.update = std::make_unique<nplex::msgs::UpdateT>(update);
        return push;
    }

    inline nplex::msgs::SubmitRequestT make_submit_request(size_t cid, size_t crev, uint32_t type, std::vector<nplex::msgs::KeyValueT> upserts, std::vector<std::string> deletes, std::vector<nplex::msgs::CheckT> checks)
    {
        nplex::msgs::SubmitRequestT req;
        req.cid = cid;
        req.crev = crev;
        req.type = type;
        req.upserts = make_vector_unique_ptr(upserts);
        req.deletes = deletes;
        req.checks = make_vector_unique_ptr(checks);
        return req;
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

}; // namespace tests
}; // namespace nplex
