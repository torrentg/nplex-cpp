#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include "match.h"
#include "permissions.h"

#define INITIAL_LENGTH 8
#define MAX_PATTERN_LENGTH 256
#define FACTOR 2

static bool is_valid_permissions(const permissions_t *perms)
{
    if (!perms)
        return false;

    if (perms->length > perms->capacity)
        return false;

    if (perms->length > 0 && !perms->data)
        return false;

    return true;
}

bool is_valid_pattern(const char *pattern)
{
    if (!pattern)
        return false;

    size_t len = strlen(pattern);
    
    if (len == 0 || len >= MAX_PATTERN_LENGTH)
        return false;

    for (size_t i = 0; i < len; i++)
        if (!isprint(pattern[i]))
            return false;

    return true;
}

bool permissions_contains(const permissions_t *perms, const char *pattern)
{
    if (!is_valid_permissions(perms) || !is_valid_pattern(pattern))
        return false;

    for (size_t i = 0; i < perms->length; i++)
    {
        assert(perms->data[i].pattern);

        if (strcmp(perms->data[i].pattern, pattern) == 0)
            return true;
    }

    return false;
}

bool permissions_append(permissions_t *perms, const char *pattern, crud_t crud)
{
    if (!is_valid_permissions(perms) || !is_valid_pattern(pattern))
        return false;

    if (perms->length == perms->capacity)
    {
        uint16_t capacity = (perms->capacity == 0 ? INITIAL_LENGTH : (uint16_t)(FACTOR * perms->capacity));
        permission_t *data = reallocarray(perms->data, capacity, sizeof(permission_t));
        if (!data)
            return false;

        perms->data = data;
        perms->capacity = capacity;
    }

    assert(perms->capacity > perms->length);

    perms->data[perms->length].pattern = strdup(pattern);
    if (!perms->data[perms->length].pattern)
        return false;

    perms->data[perms->length].crud = crud;
    perms->length++;

    return true;
}

void permissions_free(permissions_t *perms)
{
    if (!perms)
        return;

    if (perms->data)
    {
        for (size_t i = 0; i < perms->length; i++) {
            free(perms->data[i].pattern);
            perms->data[i].pattern = NULL;
            perms->data[i].crud = (crud_t){0};
        }

        free(perms->data);
    }

    *perms = (permissions_t){0};
}

crud_t permissions_check(const permissions_t *perms, const char *path)
{
    if (!is_valid_permissions(perms) || !path) {
        assert(false);
        return (crud_t){0};
    }

    for (size_t i = 0; i < perms->length; i++)
        if (glob_match(path, perms->data[i].pattern))
            return perms->data[i].crud;

    return (crud_t){0};
}
