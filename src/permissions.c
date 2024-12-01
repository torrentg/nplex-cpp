#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include "match.h"
#include "permissions.h"

#define INITIAL_LENGTH 8
#define MAX_PATTERN_LENGTH 256
#define FACTOR 2

static crud_t crud_and(crud_t lhs, crud_t rhs)
{
    uint8_t ret = (*((uint8_t *) &lhs) & *((uint8_t *) &rhs));
    return *((crud_t *) &ret);
    // return (crud_t){
    //     .create = lhs.create & rhs.create,
    //     .read   = lhs.read   & rhs.read,
    //     .update = lhs.update & rhs.update,
    //     .delete = lhs.delete & rhs.delete,
    // };
}

static crud_t crud_or(crud_t lhs, crud_t rhs)
{
    uint8_t ret = (*((uint8_t *) &lhs) | *((uint8_t *) &rhs));
    return *((crud_t *) &ret);
    // return (crud_t){
    //     .create = lhs.create | rhs.create,
    //     .read   = lhs.read   | rhs.read,
    //     .update = lhs.update | rhs.update,
    //     .delete = lhs.delete | rhs.delete
    // };
}

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

bool permissions_append(permissions_t *perms, const char *pattern, crud_t crud, bool op_and)
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
    perms->data[perms->length].op_and = op_and;
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
            perms->data[i].crud = CRUD(0,0,0,0);
        }

        free(perms->data);
    }

    *perms = (permissions_t){0};
}

crud_t permissions_check(const permissions_t *perms, const char *path)
{
    crud_t crud = CRUD(0,0,0,0);
    uint16_t num_matches = 0;

    if (!is_valid_permissions(perms) || !path) {
        assert(false);
        return crud;
    }

    for (size_t i = 0; i < perms->length; i++)
    {
        if (glob_match(path, perms->data[i].pattern))
        {
            if (num_matches == 0 || !perms->data[i].op_and)
                crud = crud_or(crud, perms->data[i].crud);
            else
                crud = crud_and(crud, perms->data[i].crud);

            num_matches++;
        }
    }

    return crud;
}
