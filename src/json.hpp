#pragma once

#include <string>
#include <vector>
#include "nplex-cpp/types.hpp"

namespace nplex {

/**
 * JSON functions (for debugging/info purposes).
 * 
 * We don't use the flatbuffers json features because we need:
 *   - ad-hoc binary data printing
 */

/**
 * Prints a database key-value pair in JSON format.
 *
 * @param[in] key Key identifier.
 * @param[in] value Value and metadata.
 *
 * @return A string containing the JSON representation of the key-value pair.
 */
std::string data_to_json(const key_t &key, const value_t &value);

/**
 * Prints a current session in JSON format.
 *
 * @param[in] session Session event information.
 *
 * @return A string containing the JSON representation of the session event.
 */
std::string session_to_json(const session_t &session);

/**
 * Prints a data event in JSON format.
 *
 * @param[in] meta Event metadata associated with the change set.
 * @param[in] changes List of applied changes.
 *
 * @return A string containing the JSON representation of the change set.
 */
std::string event_data_to_json(const const_meta_ptr &meta, const std::vector<change_t> &changes);

/**
 * Prints a session event in JSON format.
 *
 * @param[in] session Session event information.
 *
 * @return A string containing the JSON representation of the session event.
 */
std::string event_session_to_json(const session_t &session);

} // namespace nplex
