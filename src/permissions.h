#ifndef PERMISSIONS_H
#define PERMISSIONS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @file
 * Permissions object and support functions.
 */

typedef struct crud_t
{
    uint8_t create:1;               //<! Allowed to create
    uint8_t read  :1;               //<! Allowed to read
    uint8_t update:1;               //<! Allowed to update
    uint8_t delete:1;               //<! Allowed to delete
} crud_t;

typedef struct permission_t
{
    bool op_and;                    //<! Logical operator to apply when there is a previous match (true=and, false=or).
    crud_t crud;                    //<! Pattern permissions.
    char *pattern;                  //<! Glob pattern (ex. system/**/password).
} permission_t;

typedef struct permissions_t
{
    permission_t *data;             //<! Permissions list.
    uint16_t length;                //<! Current length.
    uint16_t capacity;              //<! Reserved memory.
} permissions_t;

#define CRUD(c, r, u, d) (crud_t){.create = c, .read = r, .update = u, .delete = d}

/**
 * Check if a string is a valid pattern.
 *   - Not NULL
 *   - 0 < length < 256
 *   - printable chars
 * 
 * @param[in] pattern Pattern to check.
 * @return true if valid pattern.
 * @return false otherwise.
 */
bool is_valid_pattern(const char *pattern);

/**
 * Check if a pattern belongs to a permissions list.
 * 
 * @param[in] perms Permissions list.
 * @param[in] pattern Pattern to check.
 * @return true if pattern belongs to list.
 * @return false otherwise or error.
 */
bool permissions_contains(const permissions_t *perms, const char *pattern);

/**
 * Append a new permission to the end of the list
 * Pattern is not checked for uniqueness.
 * Takes care of list resize if required.
 * 
 * @param[in] perms Permissions list.
 * @param[in] pattern Pattern to add.
 * @param[in] crud Pattern crud.
 * @param[in] op_and Logical operator (true=and, false=or).
 * @return true on success.
 * @return false otherwise or error.
 */
bool permissions_append(permissions_t *perms, const char *pattern, crud_t crud, bool op_and);

/**
 * Dealloc a list of permissions.
 * Dealloc the content, not the object itself.
 * 
 * @param[in] perms Permissions to dealloc.
 */
void permissions_free(permissions_t *perms);

/**
 * Get path permissions.
 * 
 * At startup, path has no permissions (----).
 * Checks sequentially all patterns.
 * On a pattern match:
 *   - First match -> assigns pattern permissions
 *   - Subsequent matchs -> permissions combined using logical operator
 * 
 * @param[in] perms Permissions list.
 * @param[in] path Path to check.
 * @return Path crud.
 */
crud_t permissions_check(const permissions_t *perms, const char *path);

#endif
