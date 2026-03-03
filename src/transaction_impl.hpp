#pragma once

#include <set>
#include <map>
#include <tuple>
#include <mutex>
#include <atomic>
#include <future>
#include "flatbuffers/flatbuffers.h"
#include "nplex-cpp/transaction.hpp"
#include "messages.hpp"
#include "store.hpp"

namespace nplex {

// Forward declaration
class client_impl;
using client_impl_ptr = std::shared_ptr<client_impl>;

/**
 * Internal class implementing the transaction interface.
 * Also provides methods to update and serialize its content.
 */
class transaction_impl : public transaction, public std::enable_shared_from_this<transaction_impl>
{
  public:  // types

    enum class action_e : std::uint8_t {
        READ,                                   //!< Read a key-value.
        UPSERT,                                 //!< Update or insert a key-value.
        DELETE                                  //!< Remove a key-value.
    };

    using entry_t = std::tuple<action_e, value_ptr>;
    using items_t = std::map<key_t, entry_t, gto::cstring_compare>;
    using ensures_t = std::set<std::string>;

  public:  // methods

    transaction_impl(client_impl_ptr client, store_ptr store, isolation_e isolation, bool read_only = false);
    virtual ~transaction_impl() override;

    // const methods
    virtual isolation_e isolation() const override { return m_isolation_level; }
    virtual bool is_read_only() const override { return m_read_only; }
    virtual bool is_dirty() const override { return m_dirty; }
    virtual state_e state() const override { return m_state; }
    virtual std::uint32_t type() const override { return m_type; }
    virtual void type(std::uint32_t type) override { m_type = type; }
    virtual rev_t rev() const override;
    virtual rev_t rev_creation() const { return m_rev_creation; }

    // non-const methods
    virtual value_ptr read(const char *key, bool check = false) override;
    virtual bool upsert(const char *key, const std::string_view &data, bool force = false) override;
    virtual bool remove(const key_t &key) override;
    virtual std::size_t remove(const char *pattern) override;
    virtual bool ensure(const char *pattern) override;
    virtual std::size_t for_each(const char *pattern, const callback_t &callback) override;
    virtual std::future<submit_e> submit(bool force = false) override;
    virtual void discard() override;

    // used by messaging to serialize
    const items_t & items() const { return m_items; }
    const ensures_t & ensures() const { return m_ensures; }

    // used by client to update and submit/commit
    void update(const std::vector<change_t> &changes);
    void set_submit_result(std::exception_ptr eptr);        //!< Reports an error during submission.
    void set_submit_result(msgs::SubmitCode code);          //!< Reports the submit response result code.
    void confirm_commit(rev_t rev);                         //!< Confirms that the commit was completed at rev.

  protected:  // static members

    static std::atomic<std::uint64_t> seq_id;   //<! Transaction id generator (process-wide).

  protected:  // members

    const std::uint64_t m_id;                   //!< Transaction unique id (process-wide).
    std::mutex m_mutex;                         //!< Mutex to protect m_commands, m_async, m_cv, m_promise.
    std::weak_ptr<client_impl> m_client;        //!< Weak reference to the client.
    std::promise<submit_e> m_promise;           //!< Promise to set the submit result.
    store_ptr m_store;                          //!< Database content.
    rev_t m_rev_creation;                       //!< Database revision at tx creation.
    items_t m_items;                            //!< Transaction items (depends on isolation level).
    ensures_t m_ensures;                        //!< Transaction ensures.
    isolation_e m_isolation_level;              //!< Transaction isolation level.
    std::atomic<std::uint32_t> m_type = 0;      //!< Transaction type (user-defined value).
    std::atomic<state_e> m_state;               //!< Transaction state.
    std::atomic<bool> m_dirty = false;          //!< Current tx conflicts with a commit.
    bool m_read_only = true;                    //!< Read-only flag.

  protected:  // methods

    void set_state(state_e state);
    void update_serializable(const std::vector<change_t> &changes);
    void update_default(const std::vector<change_t> &changes);
};

using tx_impl_ptr = std::shared_ptr<transaction_impl>;

} // namespace nplex
