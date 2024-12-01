#include <string.h>
#include "xdr_funcs.h"

/*
 * Each type has a function named xdr_<type_name> used to encode/decode.
 * 
 * @see https://docs.oracle.com/cd/E23824_01/html/821-1671/xdrnts-1.html
 */

static bool_t
xdr_tx_action (XDR *xdrs, tx_action_e *objp)
{
    if (!xdr_enum (xdrs, (enum_t *) objp))
        return FALSE;

    return TRUE;
}

static bool_t
xdr_tx_mode (XDR *xdrs, tx_mode_e *objp)
{
    if (!xdr_enum (xdrs, (enum_t *) objp))
        return FALSE;

    return TRUE;
}

static bool_t
xdr_buf (XDR *xdrs, buf_t *objp)
{
    if (!xdrs || !objp)
        return FALSE;

    unsigned int len = (xdrs->x_op == XDR_ENCODE ? (unsigned int) objp->length : 0);

    if (!xdr_bytes(xdrs, (char **)&objp->data, &len, UINT32_MAX))
        return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        objp->capacity = (uint32_t) len;
        objp->length = (uint32_t) len;
    }

    return TRUE;
}

static bool_t
xdr_str (XDR *xdrs, char **objp)
{
    unsigned int len = 0;

    if (xdrs->x_op == XDR_ENCODE)
        len = (*objp ? (unsigned int) strlen(*objp) + 1 : 0);

    if (!xdr_bytes(xdrs, objp, &len, UINT32_MAX))
        return FALSE;
    return TRUE;
}

static bool_t
xdr_tx_entry (XDR *xdrs, tx_entry_t *objp)
{
    if (!xdrs || !objp)
        return FALSE;
    if (xdrs->x_op == XDR_ENCODE && !objp->key)
        return FALSE;

    if (!xdr_str (xdrs, &objp->key))
        return FALSE;
    if (!xdr_buf (xdrs, &objp->value))
        return FALSE;
    if (!xdr_tx_action (xdrs, &objp->action))
        return FALSE;
    return TRUE;
}

bool_t xdr_transaction (XDR *xdrs, transaction_t *objp)
{
    if (!xdrs || !objp)
        return FALSE;

    if (!xdr_u_int32_t (xdrs, &objp->rev))
        return FALSE;
    if (!xdr_str (xdrs, &objp->user))
        return FALSE;
    if (!xdr_u_int64_t (xdrs, &objp->timestamp))
        return FALSE;
    if (!xdr_u_int16_t (xdrs, &objp->type))
        return FALSE;
    if (!xdr_tx_mode (xdrs, &objp->mode))
        return FALSE;

    unsigned int num_entries = (unsigned int) objp->num_entries;

    if (!xdr_array(xdrs, (char **) &objp->entries, &num_entries, UINT32_MAX, sizeof(tx_entry_t), (xdrproc_t) xdr_tx_entry))
        return FALSE;

    if (xdrs->x_op == XDR_DECODE)
        objp->num_entries = (uint32_t) num_entries;

    return TRUE;
}
