#include <stdlib.h>
#include <assert.h>
#include "yyjson.h"
#include "snapshot.h"

// gcc -Wall -pedantic -c snapshot.c

#define INITIAL_MEMORY_SIZE (256*1024)
#define GROWTH_FACTOR 2

typedef struct snapshot_writer_t 
{
    snapshot_t serialized_snapshot;
    yyjson_alc *json_allocator;
    yyjson_mut_doc *json_doc;
    yyjson_mut_val *root;
} snapshot_writer_t;


static bool serialize_transaction_json(const transaction_t *tx, buf_t *ret)
{
    assert(tx);
    assert(ret);

}

snapshot_writer_t * snapshot_writer_new(rev_t rev, format_e format, compression_e compression)
{
    if (rev == 0)
        return NULL;
    
    if (format != FORMAT_XDR && format != FORMAT_JSON)
        return NULL;
    
    if (compression != COMPRESSION_NONE && compression != COMPRESSION_LZ4)
        return NULL;

    snapshot_writer_t *writer = calloc(1, sizeof(snapshot_writer_t));
    if (unlikely(!writer))
        goto SNAPSHOT_WRITER_ERROR;

    writer->serialized_snapshot.rev = rev;
    writer->serialized_snapshot.format = format;
    writer->serialized_snapshot.compression = compression;
    writer->serialized_snapshot.reserved = INITIAL_MEMORY_SIZE;
    writer->serialized_snapshot.data = (char *) malloc(INITIAL_MEMORY_SIZE);
    writer->serialized_snapshot.length = 0;

    if (unlikely(!writer->serialized_snapshot.data))
        goto SNAPSHOT_WRITER_ERROR;

    if (format == FORMAT_JSON)
    {
        if ((writer->json_allocator = yyjson_alc_dyn_new()) == NULL)
            goto SNAPSHOT_WRITER_ERROR;

        if ((writer->json_doc = yyjson_mut_doc_new(writer->json_allocator)) == NULL)
            goto SNAPSHOT_WRITER_ERROR;
    }

    return writer;

SNAPSHOT_WRITER_ERROR:
    snapshot_writer_free(writer);
    return NULL;
}

void snapshot_writer_free(snapshot_writer_t *writer)
{
    if (!writer)
        return;
    
    yyjson_alc_dyn_free(writer->json_allocator);
    yyjson_mut_doc_free(writer->json_doc) ;
    writer->json_allocator = NULL;
    free(writer);
}
