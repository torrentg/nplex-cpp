#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include "nplex-cpp/transaction.hpp"

// Nplex error codes (0 = OK, 1-999 reserved to libuv errors)
#define ERR_CLOSED_BY_LOCAL     1000
#define ERR_CLOSED_BY_PEER      1001
#define ERR_MSG_ERROR           1002
#define ERR_MSG_UNEXPECTED      1003
#define ERR_QUEUE_EXCEEDED      1004
#define ERR_ALREADY_CONNECTED   1005
#define ERR_CON_LOST            1006
#define ERR_AUTH                1007
#define ERR_LOAD                1008
#define ERR_SIGNAL              1009

#define UNUSED(x) (void)(x)

namespace nplex {

/**
 * Get a human-readable string for a given error code.
 * 
 * @param[in] error Error code (see ERR_xxx values).
 * 
 * @return Human-readable string for the error code.
 */
const char * strerror(int error);

/**
 * Convert enum value to string.
 * 
 * @param[in] val Enum value.
 * 
 * @return Enum value as string.
 */
const char * to_str(transaction::state_e val);
const char * to_str(transaction::isolation_e val);

/**
 * Same as ntohl but receiving a pointer to uint32_t instead of a uint32_t.
 * 
 * @param[in] ptr Pointer to uint32_t in network byte order.
 * 
 * @return Value in host byte order.
 */
std::uint32_t ntohl_ptr(const char *ptr);

/**
 * Check if a key is valid.
 * 
 * A valid key:
 * - must not be empty, 
 * - must not start with a space,
 * - must not end with a space,
 * - must not contain "//",
 * - must not contain '\0' in the middle, 
 * - must not contain control characters,
 * - and must be valid UTF-8.
 * 
 * @param[in] key Key to check.
 * 
 * @return true or false.
 */
bool is_valid_key(const std::string_view &key);
inline bool is_valid_key(const char *key) { return (key && is_valid_key(std::string_view{key})); }
inline bool is_valid_key(const key_t &key) { return is_valid_key(key.view()); }

/**
 * Convert milliseconds since epoch to ISO 8601 format (e.g. "2024-06-30T12:34:56.789Z").
 * 
 * @param[in] ms_since_epoch Milliseconds since epoch.
 * 
 * @return ISO 8601 formatted string.
 */
std::string to_iso8601(std::chrono::milliseconds ms_since_epoch);

/**
 * Serialize an ACL mode to a string.
 * 
 * The return string is in the format "crud" where each character is 
 * 'c', 'r', 'u' or 'd' if the corresponding permission is present, 
 * or '-' if not.
 * 
 * Example: crud 0b1101 (create, read and delete permissions) will be serialized as "cr-d".
 * 
 * @param[in] crud ACL mode.
 * 
 * @return std::string Serialized ACL mode.
 */
std::string crud_to_string(std::uint8_t crud);

/**
 * Parse a string to an ACL mode.
 * 
 * The input string must be in the format "crud" where each character is 
 * 'c', 'r', 'u' or 'd' if the corresponding permission is present, 
 * or '-' if not.
 * 
 * Example: string "cr-d" will be parsed as mode 0b1101 (create, read and delete permissions).
 * 
 * @param[in] str String to parse.
 * 
 * @return std::uint8_t Parsed ACL mode.
 * 
 * @exception std::invalid_argument If the input string is not in the correct format.
 */
std::uint8_t parse_crud(const std::string_view &str);

} // namespace nplex
