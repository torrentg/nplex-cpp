#ifndef XDR_FUNCS_H
#define XDR_FUNCS_H

#include <rpc/xdr.h>
#include "transaction.h"

/**
 * @file
 * XDR encoding/decoding related functions.
 */

/**
 * Serialize/deserialize a transaction.
 * 
 * @param[in] xdrs The XDR handle.
 * @param[in,out] objp Transaction.
 * @return true = success, false = error.
 */
bool_t xdr_transaction(XDR *xdrs, transaction_t *objp);

#endif
