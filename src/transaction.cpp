#include "nplex-cpp/transaction.hpp"
#include "transaction_impl.hpp"

nplex::transaction_t::isolation_e nplex::transaction_t::isolation() const { return get_impl(this)->m_isolation_level; }
bool nplex::transaction_t::read_only() const { return get_impl(this)->m_read_only; }
nplex::transaction_t::state_e nplex::transaction_t::state() const { return get_impl(this)->m_state; }
void nplex::transaction_t::state(state_e state) { get_impl(this)->m_state = state; }
bool nplex::transaction_t::dirty() const { return get_impl(this)->m_dirty; }
void nplex::transaction_t::dirty(bool dirty) { get_impl(this)->m_dirty = dirty; }
std::uint32_t nplex::transaction_t::type() const { return get_impl(this)->m_type; }
void nplex::transaction_t::type(std::uint32_t type) { get_impl(this)->m_type = type; }
nplex::value_ptr nplex::transaction_t::read(const char *key, bool check) { return get_impl(this)->read(key, check); }
bool nplex::transaction_t::upsert(const char *key, const std::string_view &data, bool force) { return get_impl(this)->upsert(key, data, force); }
bool nplex::transaction_t::remove(const key_t &key) { return get_impl(this)->remove(key); }
std::size_t nplex::transaction_t::remove(const char *pattern) { return get_impl(this)->remove(pattern); }
bool nplex::transaction_t::ensure(const char *pattern, std::uint8_t actions) { return get_impl(this)->ensure(pattern, actions); }
std::size_t nplex::transaction_t::for_each(const char *pattern, const callback_t &callback) { return get_impl(this)->for_each(pattern, callback); }
void nplex::transaction_t::update(const std::vector<change_t> &changes) { get_impl(this)->update(changes); }
