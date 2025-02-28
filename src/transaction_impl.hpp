#pragma once

#include <set>
#include <map>
#include <tuple>
#include <atomic>
#include "flatbuffers/flatbuffers.h"
#include "nplex-cpp/transaction.hpp"
#include "cache.hpp"

namespace nplex {

/**
 * Internal class implementing the transaction_t interface.
 * Also provides methods to update and serialize its content.
 */
class transaction_impl_t : public transaction_t
{
  public:

    enum class action_e : std::uint8_t {
        READ,                                   //!< Read a key-value.
        UPSERT,                                 //!< Update or insert a key-value.
        DELETE                                  //!< Remove a key-value.
    };

    using entry_t = std::tuple<action_e, value_ptr>;
    using items_t = std::map<key_t, entry_t, gto::cstring_compare>;
    using ensures_t = std::set<std::string>;

  private:

    rev_t m_rev_creation;                       //!< Database revision at tx creation.
    cache_ptr m_cache;                          //!< Database content.
    items_t m_items;                            //!< Transaction items (depends on isolation level).
    ensures_t m_ensures;                        //!< Transaction ensures.
    isolation_e m_isolation_level;              //!< Transaction isolation level.
    std::atomic<std::uint32_t> m_type = 0;      //!< Transaction type (user-defined value).
    std::atomic<state_e> m_state;               //!< Transaction state.
    std::atomic<bool> m_dirty = false;          //!< Current tx conflicts with a commit.
    bool m_read_only = true;                    //!< Read-only flag.

  private:

    void update_serializable(const std::vector<change_t> &changes);
    void update_default(const std::vector<change_t> &changes);

  public:

    transaction_impl_t(cache_ptr cache, isolation_e isolation, bool read_only = false);
    virtual ~transaction_impl_t() override = default;

    virtual isolation_e isolation() const override { return m_isolation_level; }
    virtual bool read_only() const override { return m_read_only; }
    virtual state_e state() const override { return m_state; }
    virtual void state(state_e state) { m_state = state; }
    virtual bool dirty() const override { return m_dirty; }
    virtual void dirty(bool dirty) { m_dirty = dirty; }
    virtual std::uint32_t type() const override { return m_type; }
    virtual void type(std::uint32_t type) override { m_type = type; }
    virtual rev_t rev() const override;
    virtual rev_t rev_creation() const { return m_rev_creation; }

    virtual value_ptr read(const char *key, bool check = false) override;
    virtual bool upsert(const char *key, const std::string_view &data, bool force = false) override;
    virtual bool remove(const key_t &key) override;
    virtual std::size_t remove(const char *pattern) override;
    virtual bool ensure(const char *pattern) override;
    virtual std::size_t for_each(const char *pattern, const callback_t &callback) override;

    void update(const std::vector<change_t> &changes);
    const items_t & items() const { return m_items; }
    const ensures_t & ensures() const { return m_ensures; }
};

using tx_impl_ptr = std::shared_ptr<transaction_impl_t>;

} // namespace nplex
