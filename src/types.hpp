#pragma once

#include <chrono>
#include <memory>
#include <cstddef>
#include <cstdint>
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
};

//! Database value.
class value_t
{
  private:
    gto::cstring m_data;
    std::shared_ptr<meta_t> m_meta;

  public:
    value_t(const gto::cstring &data, std::shared_ptr<meta_t> meta) : m_data{data}, m_meta{meta} {}

    // Metadata accessors
    rev_t rev() const { return (m_meta ? m_meta->rev : 0); }
    const gto::cstring & user() const { return (m_meta ? m_meta->user : gto::cstring{}); }
    millis_t timestamp() const { return (m_meta ? m_meta->timestamp : millis_t{0}); }
    uint32_t type() const { return (m_meta ? m_meta->type : 0); }

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

// Key support functions.
bool is_valid_key(const key_t &key);
std::string_view key_part(const key_t &key, std::size_t index, char delimiter = KEY_DELIMITER);
std::string_view key_prefix(const key_t &key, std::size_t index, char delimiter = KEY_DELIMITER);
std::string_view key_suffix(const key_t &key, std::size_t index, char delimiter = KEY_DELIMITER);
key_t operator+(const key_t &lhs, const char *rhs);

struct key_cmp_less_t
{
    using is_transparent = std::true_type;

    // key_t vs key_t
    bool operator()(const key_t &lhs, const key_t &rhs) const { return (lhs < rhs); }

    // key_t vs char *
    bool operator()(const key_t &lhs, const char *rhs) const { return (lhs < rhs); }
    bool operator()(const char *lhs, const key_t &rhs) const { return (lhs < rhs); }

    // key_t vs std::string
    bool operator()(const key_t &lhs, const std::string &rhs) const { return (lhs.view() < rhs); }
    bool operator()(const std::string &lhs, const key_t &rhs) const { return (lhs < rhs.view()); }
};

}; // namespace nplex
