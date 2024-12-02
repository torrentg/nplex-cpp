#include <stdlib.h>
#include <assert.h>
#include "utils.h"
#include "xdr_funcs.h"
#include "json_funcs.h"
#include "transaction.h"

#define JSON_WRITE_OPTS (YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END)
#define JSON_READ_OPTS (YYJSON_READ_STOP_WHEN_DONE)

typedef struct transaction_writer_t
{
    format_e format;
    yyjson_alc *json_allocator;
    yyjson_mut_doc *json_doc;
    buf_t xdr_buf;
} transaction_writer_t;

typedef struct transaction_reader_t
{
    format_e format;
    yyjson_alc *json_allocator;
} transaction_reader_t;

void tx_entry_reset(tx_entry_t *tx_entry)
{
    if (!tx_entry)
        return;

    free(tx_entry->key);
    tx_entry->key = NULL;

    buf_reset(&tx_entry->value);

    *tx_entry = (tx_entry_t){0};
}

void transaction_clear(transaction_t *tx)
{
    if (!tx)
        return;

    free(tx->user);
    tx->user = NULL;

    if (tx->entries.data != NULL)
    {
        for (size_t i = 0; i < tx->entries.length; i++)
            tx_entry_reset(&tx->entries.data[i]);

        tx->entries.length = 0;
    }
    else
    {
        buf_reset((buf_t *) &tx->entries);
    }

    tx->rev = 0;
    tx->timestamp = 0;
    tx->type = 0;
    tx->mode = TX_MODE_UNKNOW;
}

void transaction_reset(transaction_t *tx)
{
    if (!tx)
        return;

    transaction_clear(tx);
    buf_reset((buf_t *) &tx->entries);

    *tx = (transaction_t){0};
}

transaction_writer_t * transaction_writer_new(format_e format)
{
    if (format != FORMAT_XDR && format != FORMAT_JSON)
        return NULL;

    transaction_writer_t *writer = calloc(1, sizeof(transaction_writer_t));
    if (unlikely(!writer))
        goto TRANSACTION_WRITER_ERROR;

    writer->format = format;

    if (format == FORMAT_JSON)
    {
        if ((writer->json_allocator = yyjson_alc_dyn_new()) == NULL)
            goto TRANSACTION_WRITER_ERROR;
    }

    return writer;

TRANSACTION_WRITER_ERROR:
    transaction_writer_free(writer);
    return NULL;
}

void transaction_writer_free(transaction_writer_t *writer)
{
    if (!writer)
        return;

    yyjson_mut_doc_free(writer->json_doc) ;
    writer->json_doc = NULL;

    yyjson_alc_dyn_free(writer->json_allocator);
    writer->json_allocator = NULL;

    buf_reset(&writer->xdr_buf);

    free(writer);
}

static bool transaction_writer_serialize_json(transaction_writer_t *writer, const transaction_t *tx, buf_t *buf)
{
    assert(writer->format == FORMAT_JSON);

    yyjson_mut_doc_free(writer->json_doc);
    writer->json_doc = yyjson_mut_doc_new(writer->json_allocator);

    if (unlikely(!writer->json_doc))
        goto SERIALIZE_JSON_ERROR;

    yyjson_mut_val *root = json_serialize_transaction(writer->json_doc, tx);

    if (unlikely(!root))
        goto SERIALIZE_JSON_ERROR;

    yyjson_mut_doc_set_root(writer->json_doc, root);

    size_t str_len = 0;
    char *str = yyjson_mut_write_opts(writer->json_doc, JSON_WRITE_OPTS, writer->json_allocator, &str_len, NULL);

    if (unlikely(!str))
        goto SERIALIZE_JSON_ERROR;

    buf->data = str;
    buf->length = str_len;
    buf->capacity = str_len;

    return true;

SERIALIZE_JSON_ERROR:
    yyjson_mut_doc_free(writer->json_doc);
    writer->json_doc = NULL;
    return false;
}

static bool transaction_writer_serialize_xdr(transaction_writer_t *writer, const transaction_t *tx, buf_t *buf)
{
    assert(writer->format == FORMAT_XDR);

    XDR xdrs = {0};
    unsigned int len = xdr_sizeof((xdrproc_t) xdr_transaction, (void *) tx);
    bool ret = false;

    writer->xdr_buf.length = 0;

    if (!buf_reserve(&writer->xdr_buf, len, sizeof(char)))
        return false;

    xdrmem_create(&xdrs, writer->xdr_buf.data, len, XDR_ENCODE);

    if (xdr_transaction(&xdrs, (void *) tx)) {
        writer->xdr_buf.length = len;
        *buf = writer->xdr_buf;
        ret = true;
    }

    xdr_destroy(&xdrs);

    return ret;
}

bool transaction_writer_serialize(transaction_writer_t *writer, const transaction_t *tx, buf_t *ret)
{
    if (!writer || !tx || !ret || !buf_is_valid((buf_t *) &tx->entries))
        return false;

    *ret = (buf_t){0};

    switch (writer->format)
    {
        case FORMAT_JSON: return transaction_writer_serialize_json(writer, tx, ret);
        case FORMAT_XDR: return transaction_writer_serialize_xdr(writer, tx, ret);
        default: return false;
    }
}

transaction_reader_t * transaction_reader_new(format_e format)
{
    if (format != FORMAT_XDR && format != FORMAT_JSON)
        return NULL;

    transaction_reader_t *reader = calloc(1, sizeof(transaction_reader_t));
    if (unlikely(!reader))
        goto TRANSACTION_READER_ERROR;

    reader->format = format;

    if (format == FORMAT_JSON)
    {
        if ((reader->json_allocator = yyjson_alc_dyn_new()) == NULL)
            goto TRANSACTION_READER_ERROR;
    }

    return reader;

TRANSACTION_READER_ERROR:
    transaction_reader_free(reader);
    return NULL;
}

void transaction_reader_free(transaction_reader_t *reader)
{
    if (!reader)
        return;

    yyjson_alc_dyn_free(reader->json_allocator);
    reader->json_allocator = NULL;

    free(reader);
}

static bool transaction_reader_deserialize_json(transaction_reader_t *reader, const buf_t *buf, transaction_t *tx)
{
    assert(reader->format == FORMAT_JSON);

    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;

    if ((doc = yyjson_read_opts(buf->data, buf->length, JSON_READ_OPTS, reader->json_allocator, NULL)) == NULL)
        goto DESERIALIZE_JSON_ERROR;

    root = yyjson_doc_get_root(doc);

    if (!json_deserialize_transaction(root, tx))
        goto DESERIALIZE_JSON_ERROR;

    yyjson_doc_free(doc);
    return true;

DESERIALIZE_JSON_ERROR:
    yyjson_doc_free(doc);
    return false;
}

static bool transaction_reader_deserialize_xdr(transaction_reader_t *reader, const buf_t *buf, transaction_t *tx)
{
    UNUSED(reader);
    assert(reader->format == FORMAT_XDR);

    XDR xdrs = {0};
    bool ret = true;

    xdrmem_create(&xdrs, buf->data, (unsigned int) buf->length, XDR_DECODE);

    if (xdr_transaction(&xdrs, tx) == FALSE) {
        xdr_free((xdrproc_t) xdr_transaction, tx);
        *tx = (transaction_t){0};
        ret = false;
    }

    xdr_destroy(&xdrs);
    return ret;
}

bool transaction_reader_deserialize(transaction_reader_t *reader, const buf_t *buf, transaction_t *tx)
{
    if (!reader || !buf || !tx || !buf->data)
        return false;

    transaction_clear(tx);

    switch (reader->format)
    {
        case FORMAT_JSON: return transaction_reader_deserialize_json(reader, buf, tx);
        case FORMAT_XDR: return transaction_reader_deserialize_xdr(reader, buf, tx);
        default: return false;
    }
}
