#pragma once

#include <chrono>
#include <cstdint>
#include "nplex-cpp/transaction.hpp"
#include "client_impl.hpp"

namespace nplex {

/**
 * Same as ntohl but receiving a pointer to uint32_t instead of a uint32_t.
 * 
 * @param[in] ptr Pointer to uint32_t in network byte order.
 * 
 * @return Value in host byte order.
 */
std::uint32_t ntohl_ptr(const char *ptr);

/**
 * Convert enum value to string.
 * 
 * @param[in] val Enum value.
 * 
 * @return Enum value as string.
 */
const char * to_str(client_impl::state_e val);
const char * to_str(transaction::state_e val);
const char * to_str(transaction::isolation_e val);

/**
 * Check if a key is valid.
 * 
 * @param[in] key Key to check.
 * 
 * @return true = valid, false = invalid.
 */
inline bool is_valid_key(const std::string_view &key) { return !key.empty(); }
inline bool is_valid_key(const char *key) { return (key && is_valid_key(std::string_view{key})); }
inline bool is_valid_key(const key_t &key) { return is_valid_key(key.view()); }

/**
 * Convert milliseconds since epoch to ISO 8601 format (e.g. "2024-06-30T12:34:56.789Z").
 * 
 * @param ms_since_epoch Milliseconds since epoch.
 * 
 * @return ISO 8601 formatted string.
 */
std::string to_iso8601(std::chrono::milliseconds ms_since_epoch);

} // namespace nplex
