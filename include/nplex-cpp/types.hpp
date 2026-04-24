#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <charconv>
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
    std::uint32_t tx_type;          //!< Transaction type (user-defined).
    std::uint32_t nrefs;            //!< Number of references in the store (internal use).
};

using meta_ptr = std::shared_ptr<meta_t>;
using const_meta_ptr = std::shared_ptr<const meta_t>;

/**
 * Database value.
 * 
 * Includes the value data and its metadata.
 * 
 * The syntactic-sugar methods are not used internally; they are provided
 * for convenience. Feel free to adapt or extend them as needed.
 * 
 * This class is immutable.
 */
class value_t
{
  public:  // methods

    // Constructor.
    value_t(const gto::cstring &data, std::shared_ptr<meta_t> meta) : m_data{data}, m_meta{meta} {}

    // Metadata accessors
    rev_t rev() const { return (m_meta ? m_meta->rev : 0); }
    const gto::cstring & user() const { return (m_meta ? m_meta->user : EMPTY); }
    millis_t timestamp() const { return (m_meta ? m_meta->timestamp : millis_t{0}); }
    std::uint32_t tx_type() const { return (m_meta ? m_meta->tx_type : 0); }

    // Data accessor
    const gto::cstring & data() const { return m_data; }

    // Some syntactic sugar
    std::string_view as_string() const { return m_data.view(); }
    bool as_bool() const { return (m_data == "true" || m_data == "1"); }
    millis_t as_millis() const { return millis_t{as_number<std::int64_t>()}; }
    template<typename T> 
    T as_number() const {
        T value{};
        const char* begin = m_data.data();
        const char* end = begin + m_data.size();
        auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc() || result.ptr != end)
            throw std::invalid_argument("Invalid numeric conversion");
        return value;
    }
    template<typename T>
    T as_number_or(const T &default_value) const {
        try {
            return as_number<T>();
        }
        catch (const std::exception &) {
            return default_value;
        }
    }

  private:  // types

    static const gto::cstring EMPTY;
    friend struct store_t;

  private:  // members

    gto::cstring m_data;
    meta_ptr m_meta;
};

using value_ptr = std::shared_ptr<value_t>;

/**
 * Struct used to report changes in the database content after an update.
 */
struct change_t
{
    enum class action_e : std::uint8_t {
        CREATE,
        UPDATE,
        DELETE
    };

    action_e action;
    key_t key;
    value_ptr new_value;    // DELETE = nullptr
    value_ptr old_value;    // CREATE = nullptr
};

/**
 * Struct used to report session information when a client connects 
 * or disconnects from the server.
 */
struct session_t
{
    enum class code_e : std::uint8_t {
        CONNECTED,
        CLOSED_BY_SERVER,
        CLOSED_BY_USER,
        COMM_ERROR,
        CON_LOST,
        EXCD_LIMITS
    };

    std::string user;       //!< User name
    std::string address;    //!< Peer address (IP)
    code_e code;            //!< Exit code (0 means connected)
    millis_t since;         //!< Connection start (UTC millis since epoch)
    millis_t until;         //!< Disconnection time (0 if still active)
};

} // namespace nplex
