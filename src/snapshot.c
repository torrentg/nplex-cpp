#include <stdlib.h>
#include <assert.h>
#include "yyjson.h"
#include "utils.h"
#include "snapshot.h"

// gcc -Wall -pedantic -c snapshot.c

#define WRITER_CONTENT_INITIAL_SIZE (256*1024)
#define WRITER_ENTRIES_INITIAL_SIZE 32

typedef struct snapshot_writer_t 
{
    rev_t last_rev_added;
    snapshot_t snapshot;
    transaction_writer_t *tx_writer;
    const permissions_t *permissions;
    tx_entries_t entries;                   // to avoid multiple entries::data allocation
} snapshot_writer_t;

typedef struct snapshot_reader_t 
{
    const snapshot_t *snapshot;
    const permissions_t *permissions;
    transaction_reader_t *tx_reader;
    transaction_t tx;                       // next transaction
} snapshot_reader_t;

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

    writer->last_rev_added = 0;
    writer->permissions = permissions;
    writer->snapshot.rev = rev;
    writer->snapshot.format = format;
    writer->snapshot.compression = compression;

    if (unlikely(!buf_reserve(&writer->snapshot.content, WRITER_CONTENT_INITIAL_SIZE, sizeof(char))))
        goto SNAPSHOT_WRITER_ERROR;

    if (unlikely(!buf_reserve((buf_t *) &writer->entries, WRITER_ENTRIES_INITIAL_SIZE, sizeof(tx_entry_t))))
        goto SNAPSHOT_WRITER_ERROR;

    writer->tx_writer = transaction_writer_new(format);
    if (unlikely(!writer->tx_writer))
        goto SNAPSHOT_WRITER_ERROR;

    return writer;

SNAPSHOT_WRITER_ERROR:
    snapshot_writer_free(writer);
    return NULL;
}

void snapshot_writer_free(snapshot_writer_t *writer)
{
    if (!writer)
        return;

    writer->entries.length = 0;
    buf_reset((buf_t *) &writer->entries);
    snapshot_reset(&writer->snapshot);
    transaction_writer_free(writer->tx_writer);

    free(writer);
}

bool snapshot_writer_append(snapshot_writer_t *writer, const transaction_t *tx)
{
    if (!writer || !tx)
        return false;

    if (tx->rev <= writer->last_rev_added || tx->rev > writer->snapshot.rev)
        return false;

    writer->last_rev_added = tx->rev;

    buf_t buf = {0};
    transaction_t aux = *tx;

    // filtering entries according to permissions
    if (writer->permissions != NULL)
    {
        const permissions_t *perms = writer->permissions;
        tx_entries_t *entries = &writer->entries;

        entries->length = 0;

        for (uint32_t i = 0; i < tx->entries.length; i++)
        {
            if (permissions_check(perms, tx->entries.data[i].key).read)
                buf_append((buf_t *) entries, &tx->entries.data[i], 1, sizeof(tx_entry_t));
        }

        aux.entries = *entries;
    }

    if (aux.entries.length == 0)
        return true;

    if (!transaction_writer_serialize(writer->tx_writer, &aux, &buf))
        return false;

    if (!buf_append(&writer->snapshot.content, buf.data, buf.length, sizeof(char)))
        return false;

    // TODO: call LZ4 if required

    return true;
}

snapshot_t snapshot_writer_get_snapshot(snapshot_writer_t *writer)
{
    snapshot_t snapshot = writer->snapshot;

    writer->snapshot = (snapshot_t){0};
    writer->last_rev_added = UINT32_MAX;

    return snapshot;
}

/**
 * Read next non-empty transaction according to permissions.
 * Result is left in reader::tx.
 * 
 * @param[in] reader Snapshot reader.
 * @return true = success, false = error.
 */
static bool snapshot_reader_next_tx(snapshot_reader_t *reader)
{
    rev_t prev_rev = reader->tx.rev;

    UNUSED(reader);
    // TODO
    //bool transaction_reader_deserialize(reader->tx_reader, const buf_t *buf, &reader->tx);
    return false;
}

snapshot_reader_t * snapshot_reader_new(const snapshot_t *snapshot, const permissions_t *permissions)
{
    if (!snapshot)
        return NULL;
    
    if (snapshot->compression != COMPRESSION_NONE && snapshot->compression != COMPRESSION_LZ4)
        return NULL;

    snapshot_reader_t *reader = calloc(1, sizeof(snapshot_reader_t));
    if (unlikely(!reader))
        goto SNAPSHOT_READER_ERROR;

    reader->permissions = permissions;
    reader->snapshot = snapshot;

    reader->tx_reader = transaction_reader_new(snapshot->format);
    if (unlikely(!reader->tx_reader))
        goto SNAPSHOT_READER_ERROR;

    if (!snapshot_reader_next_tx(reader))
        goto SNAPSHOT_READER_ERROR;

    return reader;

SNAPSHOT_READER_ERROR:
    snapshot_reader_free(reader);
    return NULL;
}

void snapshot_reader_free(snapshot_reader_t *reader)
{
    if (!reader)
        return;

    transaction_reset(&reader->tx);
    transaction_reader_free(reader->tx_reader);

    free(reader);
}

bool snapshot_reader_has_next(snapshot_reader_t *reader)
{
    // TODO
    UNUSED(reader);
    return false;
}

bool snapshot_reader_next(snapshot_reader_t *reader, transaction_t *tx)
{
    // TODO
    UNUSED(reader);
    UNUSED(tx);
    return false;
}
