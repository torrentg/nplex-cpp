#include <math.h>
#include <assert.h>
#include <string.h>
#include "yyjson.h"
#include "base64.h"
#include "utils.h"
#include "json_funcs.h"

#define JSON_KEY_REV "rev"
#define JSON_KEY_USER "user"
#define JSON_KEY_TYPE "type"
#define JSON_KEY_MODE "mode"
#define JSON_KEY_TIMESTAMP "timestamp"
#define JSON_KEY_ENTRIES "entries"
#define JSON_KEY_KEY "key"
#define JSON_KEY_ACTION "action"
#define JSON_KEY_VALUE "value"
#define JSON_KEY_BASE64 "base64"

static const char * mode_to_str(tx_mode_e mode)
{
    switch(mode)
    {
        case TX_MODE_DIRTY: return "dirty";
        case TX_MODE_SERIAL: return "serial";
        default: return "serial";
    }
}

static tx_mode_e str_to_mode(const char *str)
{
    if (!str) return TX_MODE_UNKNOW;
    if (strcmp(str, "dirty") == 0) return TX_MODE_DIRTY; 
    if (strcmp(str, "serial") == 0) return TX_MODE_SERIAL;
    return TX_MODE_UNKNOW;
}

static const char * action_to_str(tx_action_e action)
{
    switch(action)
    {
        case TX_ACTION_UPSERT: return "upsert";
        case TX_ACTION_DELETE: return "delete";
        case TX_ACTION_CHECK: return "check";
        default: return "upsert";
    }
}

static tx_action_e str_to_action(const char *str)
{
    if (!str) return TX_ACTION_UNKNOW;
    if (strcmp(str, "upsert") == 0) return TX_ACTION_UPSERT; 
    if (strcmp(str, "delete") == 0) return TX_ACTION_DELETE;
    if (strcmp(str, "check") == 0) return TX_ACTION_CHECK;
    return TX_ACTION_UNKNOW;
}

static yyjson_mut_val * json_serialize_tx_entry(yyjson_mut_doc *doc, const tx_entry_t *entry)
{
    if (unlikely(!entry->key))
        return NULL;
    if (unlikely(!entry->value.data && entry->value.length > 0))
        return NULL;

    yyjson_mut_val *obj = yyjson_mut_obj(doc);

    if (unlikely(!obj))
        return NULL;

    yyjson_mut_obj_add_str(doc, obj, JSON_KEY_KEY, entry->key);

    if (entry->action != TX_ACTION_UNKNOW)
        yyjson_mut_obj_add_str(doc, obj, JSON_KEY_ACTION, action_to_str(entry->action));

    if (entry->action != TX_ACTION_UPSERT && entry->action != TX_ACTION_UNKNOW)
        return obj;

    if (!entry->value.data)
        yyjson_mut_obj_add_null(doc, obj, JSON_KEY_VALUE);
    else if (is_utf8(entry->value.data, entry->value.length))
        yyjson_mut_obj_add_str(doc, obj, JSON_KEY_VALUE, entry->value.data);
    else
    {
        yyjson_alc *alc = &doc->alc;
        size_t len = ceil(entry->value.length / 3) * 4 + 8;
        char *str = alc->malloc(alc->ctx, len);

        if (!str)
            return NULL;

        bintob64(str, entry->value.data, entry->value.length);

        yyjson_mut_obj_add_true(doc, obj, JSON_KEY_BASE64);
        yyjson_mut_obj_add_str(doc, obj, JSON_KEY_VALUE, str);
    }

    return obj;
}

yyjson_mut_val * json_serialize_transaction(yyjson_mut_doc *doc, const transaction_t *tx)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *entries = NULL;

    if (unlikely(!obj))
        return NULL;

    yyjson_mut_obj_add_uint(doc, obj, JSON_KEY_REV, tx->rev);

    if (tx->user)
        yyjson_mut_obj_add_str(doc, obj, JSON_KEY_USER, tx->user);

    if (tx->type != 0)
        yyjson_mut_obj_add_uint(doc, obj, JSON_KEY_TYPE, tx->type);

    if (tx->mode == TX_MODE_DIRTY)
        yyjson_mut_obj_add_str(doc, obj, JSON_KEY_MODE, mode_to_str(tx->mode));

    if (tx->timestamp != 0)
    {
        char *str = doc->alc.malloc(doc->alc.ctx, 32);

        if (unlikely(!str))
            return NULL;

        millis_to_iso8601(tx->timestamp, str, 32); 
        yyjson_mut_obj_add_str(doc, obj, JSON_KEY_TIMESTAMP, str);
    }

    if (unlikely((entries = yyjson_mut_obj_add_arr(doc, obj, JSON_KEY_ENTRIES)) == NULL))
        return NULL;

    for (uint32_t i = 0; i < tx->num_entries; i++)
    {
        yyjson_mut_val *entry = json_serialize_tx_entry(doc, &tx->entries[i]);

        if (unlikely(!entry))
            return NULL;

        yyjson_mut_arr_add_val(entries, entry);
    }

    return obj;
}

static bool json_deserialize_tx_entry(yyjson_val *obj, tx_entry_t *tx_entry)
{
    assert(obj);
    assert(tx_entry);

    yyjson_val *value = NULL;
    bool value_is_base64 = false;

    tx_entry_reset(tx_entry);

    if (!yyjson_is_obj(obj))
        return NULL;

    if ((value = yyjson_obj_get(obj, JSON_KEY_KEY)) == NULL || !yyjson_is_str(value))
        goto JSON_DESERIALIZE_TX_ENTRY_ERROR;

    tx_entry->key = strdup(yyjson_get_str(value));

    if ((value = yyjson_obj_get(obj, JSON_KEY_ACTION)) != NULL)
    {
        if (!yyjson_is_str(value))
            goto JSON_DESERIALIZE_TX_ENTRY_ERROR;

        tx_entry->action = str_to_action(yyjson_get_str(value));
    }

    if ((value = yyjson_obj_get(obj, JSON_KEY_BASE64)) != NULL)
    {
        if (!yyjson_is_bool(value))
            goto JSON_DESERIALIZE_TX_ENTRY_ERROR;

        value_is_base64 = yyjson_get_bool(value);
    }

    if ((value = yyjson_obj_get(obj, JSON_KEY_VALUE)) != NULL && !yyjson_is_null(value))
    {
        if (!yyjson_is_str(value))
            goto JSON_DESERIALIZE_TX_ENTRY_ERROR;

        char *ptr = (char *) yyjson_get_str(value);
        size_t len = strlen(ptr);

        if (value_is_base64)
        {
            char *end = b64decode(ptr);

            if (!end)
                goto JSON_DESERIALIZE_TX_ENTRY_ERROR;

            len = end - ptr;
        }

        tx_entry->value.data = strndup(ptr, len);
        tx_entry->value.capacity = len + 1;
        tx_entry->value.length = len;
    }

    return true;

JSON_DESERIALIZE_TX_ENTRY_ERROR:
    tx_entry_reset(tx_entry);
    return false;
}

bool json_deserialize_transaction(yyjson_val *obj, transaction_t *tx)
{
    assert(obj);
    assert(tx);

    yyjson_val *value = NULL;
    yyjson_val *entries = NULL;

    transaction_reset(tx);

    if (!yyjson_is_obj(obj))
        return NULL;

    if ((value = yyjson_obj_get(obj, JSON_KEY_REV)) == NULL || !yyjson_is_uint(value))
        goto JSON_DESERIALIZE_TRANSACTION_ERROR;

    tx->rev = (rev_t) yyjson_get_uint(value);

    if ((value = yyjson_obj_get(obj, JSON_KEY_USER)) != NULL)
    {
        if (!yyjson_is_str(value))
            goto JSON_DESERIALIZE_TRANSACTION_ERROR;

        tx->user = strdup(yyjson_get_str(value));
    }

    if ((value = yyjson_obj_get(obj, JSON_KEY_TYPE)) != NULL)
    {
        if (!yyjson_is_uint(value))
            goto JSON_DESERIALIZE_TRANSACTION_ERROR;

        tx->type = (uint16_t) yyjson_get_uint(value);
    }

    if ((value = yyjson_obj_get(obj, JSON_KEY_MODE)) != NULL)
    {
        if (!yyjson_is_str(value))
            goto JSON_DESERIALIZE_TRANSACTION_ERROR;

        tx->mode = str_to_mode(yyjson_get_str(value));
    }

    if ((value = yyjson_obj_get(obj, JSON_KEY_TIMESTAMP)) != NULL)
    {
        if (!yyjson_is_str(value))
            goto JSON_DESERIALIZE_TRANSACTION_ERROR;

        tx->timestamp = iso8601_to_millis(yyjson_get_str(value));
    }

    if ((entries = yyjson_obj_get(obj, JSON_KEY_ENTRIES)) == NULL || !yyjson_is_arr(entries))
        goto JSON_DESERIALIZE_TRANSACTION_ERROR;

    uint32_t num_entries = yyjson_arr_size(entries);

    if ((tx->entries = calloc(num_entries, sizeof(tx_entry_t))) == NULL)
        goto JSON_DESERIALIZE_TRANSACTION_ERROR;

    for (uint32_t i = 0; i < num_entries; i++)
    {
        yyjson_val *entry = yyjson_arr_get(entries, i);

        if (!entry || !yyjson_is_obj(entry))
            goto JSON_DESERIALIZE_TRANSACTION_ERROR;

        if (!json_deserialize_tx_entry(entry, &tx->entries[i]))
            goto JSON_DESERIALIZE_TRANSACTION_ERROR;

        tx->num_entries++;
    }

    return true;

JSON_DESERIALIZE_TRANSACTION_ERROR:
    transaction_reset(tx);
    return false;
}
