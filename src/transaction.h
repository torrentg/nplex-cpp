#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "nplex.h"

/**
 * @file
 * Transaction object and support functions.
 * 
 * Transactions are used in two contexts:
 *   - case1: User trying to modify data content
 *   - case2: Notification to client on new data update
 *   - case3: Snapshot entry
 */

typedef enum
{ 
    TX_ACTION_UNKNOW,                   //!< Unrecognized action.
    TX_ACTION_UPSERT,                   //!< Insert or update key-value.
    TX_ACTION_DELETE,                   //!< Delete a key from data.
    TX_ACTION_CHECK                     //!< Check that key rev match data content.
} tx_action_e;

typedef enum
{ 
    TX_MODE_UNKNOW,                     //!< Unrecognized mode.
    TX_MODE_SERIAL,                     //!< Checks that modified data has exactly same key revs before update/delete.
    TX_MODE_DIRTY,                      //!< Data update overrides data integrity check.
} tx_mode_e;

typedef struct tx_entry_t
{
    char *key;                          //!< Entry key (mandatory field, max length = 255).
    buf_t value;                        //!< Entry value (mandatory in the upsert case).
    tx_action_e action;                 //!< Entry action (upsert | delete | check).
} tx_entry_t;

typedef struct transaction_t
{
    rev_t rev;                          //!< Revision (case1: user data rev, case2+case3: transaction rev).
    char *user;                         //!< Creator (case1: unused, case1+case3: mandatory, max length = 255).
    uint64_t timestamp;                 //!< Creation time (case1: unused, case1+case3: mandatory).
    tx_entry_t *entries;                 //!< Entries (order matters).
    uint32_t num_entries;               //!< Number of entries.
    uint16_t type;                      //!< Type (user-defined, optional, 0 is the default value).
    tx_mode_e mode;                     //!< Transaction mode (optional, serial is the default value).
} transaction_t;

typedef struct transaction_writer_t transaction_writer_t;
typedef struct transaction_reader_t transaction_reader_t;

/**
 * Dealloc memory owned by a transaction entry.
 * Does not dealloc the object itself.
 * 
 * @param[in] tx_entry Transaction to reset.
 */
void tx_entry_reset(tx_entry_t *tx_entry);

/**
 * Dealloc memory owned by a transaction.
 * Does not dealloc the object itself.
 * 
 * @param[in] tx Transaction to reset.
 */
void transaction_reset(transaction_t *tx);

/**
 * Creates a transaction writer.
 * 
 * @param[in] format Transaction format.
 * @return Transaction writer,
 *         NULL on error.
 */
transaction_writer_t * transaction_writer_new(format_e format);

/**
 * Serialize a transaction.
 * 
 * @param[in] writer Writer object.
 * @param[in] tx Transaction to serialize (ownership not transferred).
 * @param[out] buf Serialized transaction (previous content is discarded, data ownership is not transferred).
 * @return true = success,
 *         false = error (not enough memory, invalid tx revision, etc).
 */
bool transaction_writer_serialize(transaction_writer_t *writer, const transaction_t *tx, buf_t *buf);

/**
 * Deallocates a transaction writer object.
 * 
 * @param[in] writer Object to dealloc.
 */
void transaction_writer_free(transaction_writer_t *writer);

/**
 * Creates a transaction reader.
 * 
 * @param[in] format Transaction format.
 * @return Transaction reader,
 *         NULL on error.
 */
transaction_reader_t * transaction_reader_new(format_e format);

/**
 * Deserialize a transaction.
 * 
 * @param[in] reader Reader object.
 * @param[in] buf Serialized transaction (ownership not transferred).
 * @param[out] tx Deserialized transaction (ownership transferred).
 * @return true = success,
 *         false = error (not enough memory, corrupted data, etc).
 */
bool transaction_reader_deserialize(transaction_reader_t *reader, const buf_t *buf, transaction_t *tx);

/**
 * Deallocates a transaction reader object.
 * 
 * @param[in] reader Object to dealloc.
 */
void transaction_reader_free(transaction_reader_t *reader);

#endif
