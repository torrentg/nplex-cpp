#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "hashmap.h"
#include "rbtree.h"
#include "data.h"

// gcc -I../deps -Wall -pedantic -o coco data.c ../deps/hashmap.c

/**
 * Compare two rbnode_t::key.
 * 
 * @param[in] lhs Rbtree key (revision).
 * @param[in] rhs Rbtree key (revision).
 * @return zero: lhs == rhs, negative: lhs < rhs, positive: lhs > rhs 
 */
static int rbtree_entry_cmp(const void *lhs, const void *rhs)
{
    return (size_t) lhs - (size_t) rhs;
}

/**
 * Dealloc an rbtree_entry_t.
 * 
 * @param[in] node Entry to dealloc.
 * @param[in] udata User data (unused).
 */
static void rbtree_entry_free(rbnode_t *node, void *udata)
{
    UNUSED(udata);
    free(node);
}

/**
 * Hash a key (see hashmap.h).
 * 
 * @param[in] item Hashmap entry (pointer to hashmap_entry_t).
 * @param[in] seed0 Seed.
 * @param[in] seed1 Seed.
 * @return Key hash.
 */
static uint64_t hashmap_entry_hash(const void *item, uint64_t seed0, uint64_t seed1)
{
    UNUSED(seed0);
    UNUSED(seed1);
    assert(item);

    char *key = ((const hashmap_entry_t *) item)->key;

    assert(key);

    return hashmap_sip(key, strlen(key), seed0, seed1);
}

/**
 * Compare two keys (see hashmap.h).
 * 
 * @param[in] lhs Hashmap entry.
 * @param[in] rhs Hashmap entry.
 * @return zero: lhs.key == rhs.key, negative: lhs.key < rhs.key, positive: lhs.key > rhs.key 
 */
static int hashmap_entry_cmp(const void *lhs, const void *rhs, void *udata)
{
    UNUSED(udata);
    assert(lhs);
    assert(rhs);

    char *key1 = ((hashmap_entry_t *) lhs)->key;
    char *key2 = ((hashmap_entry_t *) rhs)->key;

    assert(key1);
    assert(key2);

    return strcmp(key1, key2);
}
