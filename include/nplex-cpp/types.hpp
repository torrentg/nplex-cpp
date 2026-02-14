#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <string>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <system_error>
#include "cstring.hpp"

#define KEY_DELIMITER '/'

namespace nplex {

//! Database revision number.
using rev_t = std::size_t;

//! Database key type (formatted like a path, ex: /path/to/file).
using key_t = gto::cstring;

//! Database timestamps (milliseconds from epoch time).
using millis_t = std::chrono::milliseconds;

//! Transaction metadata.
struct meta_t
{
    rev_t rev;                      //!< Revision at transaction creation.
    gto::cstring user;              //!< Transaction creator.
    millis_t timestamp;             //!< Timestamp at transaction creation.
    std::uint32_t type;             //!< Transaction type (user-defined).
    std::uint32_t nrefs;            //!< Number of references in the store (internal use).
};

using meta_ptr = std::shared_ptr<meta_t>;

//! Database value.
class value_t
{
    static const gto::cstring EMPTY;
    friend struct store_t;

  private:
    gto::cstring m_data;
    meta_ptr m_meta;

  public:
    value_t(const gto::cstring &data, std::shared_ptr<meta_t> meta) : m_data{data}, m_meta{meta} {}

    // Metadata accessors
    rev_t rev() const { return (m_meta ? m_meta->rev : 0); }
    const gto::cstring & user() const { return (m_meta ? m_meta->user : EMPTY); }
    millis_t timestamp() const { return (m_meta ? m_meta->timestamp : millis_t{0}); }
    std::uint32_t type() const { return (m_meta ? m_meta->type : 0); }

    // Data accessors
    const gto::cstring & data() const { return m_data; }
    std::string_view as_string() const { return m_data.view(); }
    bool as_bool() const { return (m_data == "true" || m_data == "1"); }
    millis_t as_millis() const { return std::chrono::milliseconds{as_number<std::int64_t>()}; }
    template<typename T> 
    T as_number() const {
        T value;
        auto result = std::from_chars(m_data.data(), m_data.data() + m_data.size(), value);
        if (result.ec != std::errc())
            throw std::invalid_argument("Invalid conversion to the requested type");
        return value;
    }
};

using value_ptr = std::shared_ptr<value_t>;

// Database change.
struct change_t
{
    enum class action_e : std::uint8_t {
        CREATE,
        UPDATE,
        DELETE
    };

    action_e action;
    key_t key;
    value_ptr value;
    value_ptr old_value;  // CREATE = empty, UPDATE = previous value, DELETE = same as value
};

// Key support functions.
inline bool is_valid_key(const std::string_view &key) { return !key.empty(); }
inline bool is_valid_key(const char *key) { return (key && is_valid_key(std::string_view{key})); }
inline bool is_valid_key(const key_t &key) { return is_valid_key(key.view()); }

} // namespace nplex
