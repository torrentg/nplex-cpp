#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Converts a NUL-ended ISO-8601 formatted string to a timestamp in milliseconds since the epoch.
 * 
 * This function handles ISO-8601 strings with milliseconds and optional timezone offsets.
 * Any additional leading or trailing characters will be considered an error.
 * 
 * @see https://en.wikipedia.org/wiki/ISO_8601
 * 
 * @param[in] iso8601 An ISO-8601 string (ex. "2024-11-26T11:13:34.552Z" or "2024-11-26T11:13:34.552+02:00")
 * @return The number of milliseconds since the epoch in UTC, 0 on error.
 */
uint64_t iso8601_to_millis(const char *iso8601);

/**
 * Converts a timestamps in milliseconds from the epoch to an ISO-8601 formatted string.
 * 
 * @see https://en.wikipedia.org/wiki/ISO_8601
 * 
 * @param[in] millis The number of milliseconds since the epoch (January 1, 1970, 00:00:00 UTC).
 * @param[out] buffer A buffer to store the resultant ISO-8601 formatted string.
 * @param[in] len Buffer length (in bytes).
 * @return true = success, false = error (insufficient buffer size, millis too large, etc).
 */
bool millis_to_iso8601(uint64_t millis, char *buffer, size_t len);

/**
 * Check if the given string is a valid utf-8 string.
 * 
 * @param[in] str String to check (NUL-ended required on text string).
 * @param[in] len String length (without ending NUL).
 * @return true = valid utf-8 string
 *         false = otherwise.
 */
bool is_utf8(const char *str, size_t len);

#endif
