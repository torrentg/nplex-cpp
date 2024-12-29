#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nplex.h"

/**
 * @file
 * Utility functions.
 */

/**
 * Converts a NUL-ended ISO-8601 formatted string to a timestamp in milliseconds since the epoch.
 * Mainly based on profile RFC-3339.
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

/**
 * Macros used to create a typed buf_t (ex. char_buf_t).
 */
#define DECL_BUF_T(type)      \
typedef struct {              \
    type *data;               \
    uint32_t length;          \
    uint32_t capacity;        \
}

DECL_BUF_T(char) char_buf_t;

/**
 * Check if buffer content is valid.
 * 
 * @param[in] bu Buffer object to check.
 * @return true = valid buf_t obejct, false = invalid buffer.
 */
bool buf_is_valid(const buf_t *buf);

/**
 * Ensures buffer capacity.
 * It does nothing if current capacity covers length request.
 * Otherwise  grants the requested size increasing and reallocating memory.
 * 
 * @param[in] buf Buffer to update.
 * @param[in] len Number of elements to reserve.
 * @param[in] size Sizeof of each element.
 * @return true = success, false = error (not enough memory).
 */
bool buf_reserve(buf_t *buf, uint32_t len, size_t size);

/**
 * Append data to buffer.
 * Reallocs mem if required.
 * 
 * @param[in] buf Buffer to update.
 * @param[in] ptr Elements to add.
 * @param[in] len Number of elements to add.
 * @param[in] size Sizeof of each element.
 * @return true = success, false = error (not enough memory).
 */
bool buf_append(buf_t *buf, const void *ptr, uint32_t len, size_t size);

/**
 * Resets a buffer.
 * Deallocates memory used by buffer.
 * 
 * @param[in] buf Buffer to reset.
 */
void buf_reset(buf_t *buf);

#endif
