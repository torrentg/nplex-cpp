#ifndef JSON_FUNCS_H
#define JSON_FUNCS_H

#include "yyjson.h"
#include "transaction.h"

/**
 * @file
 * JSON encoding/decoding related functions.
 */

/**
 * Serialize a transaction.
 * 
 * {
 *     "rev": 73924,
 *     "user": "jdoe",
 *     "type": 15,
 *     "mode": "serial",
 *     "timestamp": "2024-11-26T10:36:57.937Z",
 *     "entries" = [
 *          { "key": "energy/as27/status", "action": "upsert", "base64": false, "value": "enabled"},
 *          { "key": "alarms/2647/level", "action": "delete" },
 *          { "key": "alarms/2647/msg", "action": "delete"},
 *          { "key": "alarms/2647/read", "action": "delete"},
 *          { "key": "alarms/2647/category", "action": "delete"}
 *     ]
 * }
 * 
 * @param[in] doc JSON doc to update.
 * @param[in] tx Transaction to serialize.
 * @return JSON object or NULL on error.
 */
yyjson_mut_val * json_serialize_transaction(yyjson_mut_doc *doc, const transaction_t *tx);

/**
 * Convert a JSON object to transaction.
 * 
 * @param[in] obj JSON object.
 * @param[out] tx Transaction.
 * @return true = success, false = error.
 */
bool json_deserialize_transaction(yyjson_val *obj, transaction_t *tx);

#endif
