#ifndef NPLEX_H
#define NPLEX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file
 * Generic nplex types.
 */

#if __has_builtin(__builtin_expect)
    #define likely(expr) __builtin_expect(!!(expr), 1)
    #define unlikely(expr) __builtin_expect(!!(expr), 0)
#else
    #define likely(expr) (expr)
    #define unlikely(expr) (expr)
#endif

#define UNUSED(x) (void)(x)

// @see https://en.wikipedia.org/wiki/Comparison_of_data-serialization_formats
typedef enum
{ 
    FORMAT_XDR,                         //!< XDR is a standard data serialization format.
    FORMAT_JSON                         //!< Don't use it, only for debug purposes.
} format_e;

typedef enum
{
    COMPRESSION_NONE,
    COMPRESSION_LZ4
} compression_e;

typedef uint32_t rev_t;

typedef struct string_t {
    char *data;                         //!< String data (NUL-terminated).
    uint32_t len;                       //!< String length (NUL not considered).
    uint32_t refcounter;                //!< Number of references.
} string_t;

typedef struct buf_t {
    char *data;
    uint32_t length;
    uint32_t reserved;
} buf_t;

#endif
