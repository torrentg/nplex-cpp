#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include "match.h"
#include "permissions.h"

#define INITIAL_LENGTH 8
#define MAX_PATTERN_LENGTH 256
#define FACTOR 2

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
    if (!buf_is_valid((buf_t *) perms) || !is_valid_pattern(pattern))
        return false;

    for (size_t i = 0; i < perms->length; i++)
    {
        assert(perms->data[i].pattern);

        if (strcmp(perms->data[i].pattern, pattern) == 0)
            return true;
    }

    return false;
}

static void permission_reset(permission_t *perm)
{
    if (!perm)
        return;

    free(perm->pattern);

    *perm = (permission_t){0};
}

bool permissions_append(permissions_t *perms, const char *pattern, crud_t crud)
{
    if (!buf_is_valid((buf_t *) perms) || !is_valid_pattern(pattern))
        return false;

    permission_t perm = {
        .crud = crud,
        .pattern = strdup(pattern)
    };

    if (!perm.pattern || !buf_append((buf_t *) perms, &perm, 1, sizeof(permission_t))) {
        permission_reset(&perm);
        return false;
    }

    return true;
}

void permissions_free(permissions_t *perms)
{
    if (!perms)
        return;

    if (perms->data) {
        for (size_t i = 0; i < perms->length; i++)
            permission_reset(&perms->data[i]);
    }

    buf_reset((buf_t *) perms);
}

crud_t permissions_check(const permissions_t *perms, const char *path)
{
    if (!buf_is_valid((buf_t *) perms) || !path) {
        assert(false);
        return (crud_t){0};
    }

    for (size_t i = 0; i < perms->length; i++)
        if (glob_match(path, perms->data[i].pattern))
            return perms->data[i].crud;

    return (crud_t){0};
}
