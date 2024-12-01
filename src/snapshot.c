#include <stdlib.h>
#include <assert.h>
#include "yyjson.h"
#include "utils.h"
#include "snapshot.h"

// gcc -Wall -pedantic -c snapshot.c

#define INITIAL_MEMORY_SIZE (256*1024)
#define GROWTH_FACTOR 2

typedef struct snapshot_writer_t 
{
    snapshot_t snapshot;
    transaction_writer_t *tx_writer;
    const permissions_t *permissions;
} snapshot_writer_t;

void snapshot_reset(snapshot_t *snapshot)
{
    if (!snapshot)
        return;

    buf_reset(&snapshot->content);

    *snapshot = (snapshot_t){0};
}

snapshot_writer_t * snapshot_writer_new(rev_t rev, const permissions_t *permissions, format_e format, compression_e compression)
{
    if (rev == 0)
        return NULL;
    
    if (compression != COMPRESSION_NONE && compression != COMPRESSION_LZ4)
        return NULL;

    snapshot_writer_t *writer = calloc(1, sizeof(snapshot_writer_t));
    if (unlikely(!writer))
        goto SNAPSHOT_WRITER_ERROR;

    writer->snapshot.rev = rev;
    writer->snapshot.format = format;
    writer->snapshot.compression = compression;

    if (unlikely(!buf_reserve(&writer->snapshot.content, INITIAL_MEMORY_SIZE)))
        goto SNAPSHOT_WRITER_ERROR;

    writer->tx_writer = transaction_writer_new(format);

    if (unlikely(!writer->tx_writer))
        goto SNAPSHOT_WRITER_ERROR;

    writer->permissions = permissions;

    return writer;

SNAPSHOT_WRITER_ERROR:
    snapshot_writer_free(writer);
    return NULL;
}

void snapshot_writer_free(snapshot_writer_t *writer)
{
    if (!writer)
        return;

    snapshot_reset(&writer->snapshot);
    transaction_writer_free(writer->tx_writer);

    free(writer);
}

bool snapshot_writer_append(snapshot_writer_t *writer, const transaction_t *tx)
{
    if (!writer || !tx)
        return false;

    buf_t buf = {0};
    
    if (!transaction_writer_serialize(writer->tx_writer, tx, &buf))
        return false;

    if (!buf_append(&writer->snapshot.content, buf.data, buf.length))
        return false;

    // TODO: call LZ4 if required

    return true;
}
