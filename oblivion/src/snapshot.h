#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include "nplex.h"
#include "permissions.h"
#include "transaction.h"

/**
 * @file
 * Snapshot object and support functions.
 */

typedef struct snapshot_t
{
    rev_t rev;                          //!< Snapshot revision.
    format_e format;                    //!< Format (xdr, json).
    compression_e compression;          //!< Compression algorithm (none, lz4).
    buf_t content;                      //!< Snapshot content.
} snapshot_t;

typedef struct snapshot_writer_t snapshot_writer_t;
typedef struct snapshot_reader_t snapshot_reader_t;

/**
 * Resets a snapshot object.
 * Dealloc memory allocated by the snapshot, no the object itself.
 * 
 * @param[in] snapshot Snapshot object.
 */
void snapshot_reset(snapshot_t *snapshot);

/**
 * Creates a snapshot writer.
 * 
 * @param[in] rev Snapshot revision.
 * @param[in] permissions Permissions to apply (ownership is not transferred, can be NULL).
 * @param[in] format Snapshot format.
 * @param[in] compression Compression algorithm.
 * @return Snapshot writer,
 *         NULL on error.
 */
snapshot_writer_t * snapshot_writer_new(rev_t rev, const permissions_t *permissions, format_e format, compression_e compression);

/**
 * Appends a transaction to a snapshot writer.
 * 
 * @param[in] writer Object to update.
 * @param[in] tx Transaction to append (ownership not transferred).
 * @return true = success,
 *         false = error (not enough memory, invalid tx revision, etc).
 */
bool snapshot_writer_append(snapshot_writer_t *writer, const transaction_t *tx);

/**
 * Returns the serialized snapshot.
 * This function is called only once.
 * Data ownership is transferred.
 * 
 * @param[in] writer Snapshot writer.
 * @return Serialized snapshot (rev == 0 means error).
 */
snapshot_t snapshot_writer_get_snapshot(snapshot_writer_t *writer);

/**
 * Deallocates a snapshot writer object.
 * 
 * @param[in] writer Object to dealloc.
 */
void snapshot_writer_free(snapshot_writer_t *writer);

/**
 * Creates a snapshot reader.
 * 
 * @param[in] snapshot Serialized data (ownership not transferred).
 * @param[in] permissions Permissions to apply (ownership is not transferred, can be NULL).
 * @return Snapshot reader,
 *         NULL on error (ex. unrecognized format, corrupted data, etc).
 */
snapshot_reader_t * snapshot_reader_new(const snapshot_t *snapshot, const permissions_t *permissions);

/**
 * Check if there is a tx pending to be read.
 * 
 * @param[in] reader Snapshot reader.
 * @return true = there is a pending tx,
 *         false = otherwhise.
 */
bool snapshot_reader_has_next(snapshot_reader_t *reader);

/**
 * Read next transaction in the snapshot.
 * 
 * @param[in] reader Snapshot reader.
 * @param[out] tx Resulting transaction (empty initially).
 * @return true = success,
 *         false = error (ex. corrupted data, tx rev out-of-order, no more tx's).
 */
bool snapshot_reader_next(snapshot_reader_t *reader, transaction_t *tx);

/**
 * Deallocates a snapshot reader object.
 * 
 * @param[in] reader Object to dealloc.
 */
void snapshot_reader_free(snapshot_reader_t *reader);

#endif
