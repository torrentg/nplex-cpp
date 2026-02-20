#pragma once

#include <cstdint>
#include "nplex-cpp/transaction.hpp"

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
 * Convert isolation level to string.
 * 
 * @param[in] isolation Isolation level.
 * 
 * @return Isolation level as string.
 */
const char * to_str(nplex::transaction::isolation_e isolation);

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

}
