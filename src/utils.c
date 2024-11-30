#define _XOPEN_SOURCE
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "simdutf8check.h"
#include "utils.h"

#define BUF_GROWTH_FACTOR       2

uint64_t iso8601_to_millis(const char *iso8601)
{
    struct tm tm_time = {0};
    int milliseconds = 0;
    int tz_offset_seconds = 0;
    char *str = NULL;

    if (!iso8601 || !isdigit(*iso8601))
        return 0;

    // Parse the date-time part exluding milliseconds
    if ((str = strptime(iso8601, "%Y-%m-%dT%H:%M:%S", &tm_time)) == NULL)
        return 0;

    // Parse milliseconds
    if (*str == '.')
    {
        char *tmp = NULL;

        str++;
        if (!isdigit(*str)) return 0;

        errno = 0;
        milliseconds = strtol(str, &tmp, 10);

        if (str == tmp || tmp > str + 3 || errno != 0 || milliseconds < 0 || milliseconds > 999)
            return 0;

        str = tmp;
    }

    // Parse timezone offset
    if (*str == 'Z' || *str == 'z')
    {
        if (*(++str) != '\0')
            return 0;
    }
    else if (*str == '+' || *str == '-')
    {
        char timezone[6] = {0};
        size_t len = 0;
        int sign = (*str == '-' ? -1 : +1);
        int hours = 0;
        int minutes = 0;
        int rc = 0;

        str++;
        strncpy(timezone, str, 5);
        len = strlen(timezone);

        if (strlen(str) != len)
            return 0;

        rc = sscanf(timezone, "%2d:%2d", &hours, &minutes);

        switch (rc)
        {
            case 1: // hh
                if (len != 2)
                    return 0;
                break;
            case 2: // hh:mm
                if (len != 5)
                    return 0;
                break;
            default:
                return 0;
        }

        if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
            return 0;

        tz_offset_seconds = sign * (hours * 3600 + minutes * 60);

        str += strlen(timezone);
    }
    else
    {
        if (*str != '\0')
            return 0;
    }

    // convert to UTC epoch time
    time_t seconds_since_epoch = timegm(&tm_time);
    if (seconds_since_epoch == -1)
        return 0;

    // Check for invalid time
    if (seconds_since_epoch < tz_offset_seconds)
        return 0;

    // Combine seconds and milliseconds
    return (uint64_t) (seconds_since_epoch - tz_offset_seconds) * 1000 + milliseconds;
}

bool millis_to_iso8601(uint64_t millis, char *buffer, size_t len)
{
    if (buffer == NULL || len < 25)
        return false;

    time_t seconds = millis / 1000;
    int milliseconds = millis % 1000;

    const struct tm *tm_time = gmtime(&seconds);
    if (tm_time == NULL)
        return false;

    // Format an ISO-8601 string with 'Z' to denote UTC time
    snprintf(buffer, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm_time->tm_year + 1900,
        tm_time->tm_mon + 1,
        tm_time->tm_mday,
        tm_time->tm_hour,
        tm_time->tm_min,
        tm_time->tm_sec,
        milliseconds
    );

    return true;
}

bool is_utf8(const char *str, size_t len)
{
    if (!str || strlen(str) != len)
        return false;

    return validate_utf8_fast(str, len);
}

static bool buf_is_valid(const buf_t *buf)
{
    return (buf && buf->length <= buf->reserved && (buf->reserved == 0 || buf->data));
}

bool buf_reserve(buf_t *buf, uint32_t size)
{
    if (!buf_is_valid(buf))
        return false;

    if (buf->reserved >= size)
        return true;

    if (buf->reserved == 0)
    {
        free(buf->data);

        if ((buf->data = malloc(size)) == NULL)
            return false;

        buf->reserved = size;
        return true;
    }

    size_t reserved = buf->reserved;

    while (reserved < size)
    {
        reserved *= BUF_GROWTH_FACTOR;

        if (reserved > UINT32_MAX)
            reserved = size;
    }

    char *ptr = realloc(buf->data, reserved);

    if (unlikely(!ptr))
        return false;

    buf->data = ptr;
    buf->reserved = (uint32_t) reserved;

    return true;
}

void buf_reset(buf_t *buf)
{
    if (!buf)
        return;
    
    if (buf->data)
        free(buf->data);
    
    *buf = (buf_t){0};
}

bool buf_append(buf_t *buf, const char *str, uint32_t len)
{
    if (!buf_is_valid(buf))
        return false;

    if (len == 0)
        return true;

    if (!str || buf->length + len <= buf->length) // length overflow
        return false;

    if (!buf_reserve(buf, buf->length + len))
        return false;

    memcpy(buf->data + buf->length, str, len);
    buf->length += len;

    return true;
}
