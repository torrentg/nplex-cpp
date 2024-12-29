#ifndef DATA_H
#define DATA_H

#include <stdint.h>
#include <stddef.h>
#include "nplex.h"
#include "permissions.h"

typedef struct hashmap_entry_t
{
    char *key;                          // key
    char *value;                        // value
    size_t rev;                         // revision
    struct hashmap_entry_t *prev;       // previous entry on same tx
    struct hashmap_entry_t *next;       // next entry on same tx
} hashmap_entry_t;

typedef struct rbtree_entry_t
{
    rbnode_t node;                      // rbtree data
    hashmap_entry_t *ptr;               // pointer to first entry
} rbtree_entry_t;

typedef struct nplex_data_t
{
    rev_t rev;
    struct hashmap *map_keys;           // map of entries (key = key, value = pointer to entry)
    struct rbtree_t map_revs;           // map of revisions (key = rev, value = ptr to first entry)
} nplex_data_t;

typedef struct data_t data_t;

data_t * data_new(void);
void data_free(data_t *data);
bool data_get_snapshot(data_t *data, buf_t *buf, permissions_t *perms);
int data_set_snapshot(data_t *data, buf_t *buf, permissions_t *perms);
int data_commit_tx(data_t *data, tx_t *tx, permissions_t *perms);

#endif
