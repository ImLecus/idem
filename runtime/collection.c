#include "internal.h"

#include <stdlib.h>
#include <string.h>

static IdemSeqObj *idem_as_seq(Value value, const char *message)
{
    if (value.tag != IDEM_TAG_ARRAY && value.tag != IDEM_TAG_LIST &&
        value.tag != IDEM_TAG_TUPLE) {
        idem_panic_str(message);
    }
    return (IdemSeqObj *)value.as.obj;
}

static IdemMapObj *idem_as_map(Value value)
{
    if (value.tag != IDEM_TAG_MAP) {
        idem_panic_str("expected a Map");
    }
    return (IdemMapObj *)value.as.obj;
}

static Value idem_seq_like(IdemTag tag, Value *items, size_t count,
                           const IdemTypeDesc *type)
{
    if (tag == IDEM_TAG_ARRAY) {
        return idem_array(items, count, type);
    }
    return idem_list(items, count, type);
}

Value idem_len(Value value)
{
    switch (value.tag) {
    case IDEM_TAG_STRING:
    case IDEM_TAG_BYTES:
        return idem_int((IdemInt)((IdemStringObj *)value.as.obj)->length);
    case IDEM_TAG_ARRAY:
    case IDEM_TAG_LIST:
    case IDEM_TAG_TUPLE:
        return idem_int((IdemInt)((IdemSeqObj *)value.as.obj)->count);
    case IDEM_TAG_MAP:
        return idem_int((IdemInt)((IdemMapObj *)value.as.obj)->count);
    default:
        idem_panic_str("len expects a collection or string");
        return idem_unit();
    }
}

Value idem_get(Value collection, Value index)
{
    if (collection.tag == IDEM_TAG_ARRAY ||
        collection.tag == IDEM_TAG_LIST) {
        IdemSeqObj *seq;
        IdemInt i;
        seq = idem_as_seq(collection, "expected an Array or List");
        if (index.tag != IDEM_TAG_INT) {
            idem_panic_str("index must be an Int");
        }
        i = index.as.i;
        if (i < 0 || (IdemUInt)i >= (IdemUInt)seq->count) {
            return idem_unit();
        }
        return seq->items[(size_t)i];
    }
    if (collection.tag == IDEM_TAG_MAP) {
        IdemMapObj *map;
        size_t i;
        map = idem_as_map(collection);
        for (i = 0; i < map->count; i++) {
            if (idem_value_equal(map->keys[i], index)) {
                return map->values[i];
            }
        }
        return idem_unit();
    }
    idem_panic_str("get expects an Array, List, or Map");
    return idem_unit();
}

Value idem_set(Value collection, Value index, Value value)
{
    if (collection.tag == IDEM_TAG_ARRAY) {
        IdemSeqObj *seq;
        Value *items;
        IdemInt i;

        seq = idem_as_seq(collection, "expected an Array");
        if (index.tag != IDEM_TAG_INT) {
            idem_panic_str("index must be an Int");
        }
        i = index.as.i;
        if (i < 0 || (IdemUInt)i >= (IdemUInt)seq->count) {
            idem_panic_str("array index out of bounds");
        }
        items = (Value *)malloc(sizeof(Value) * seq->count);
        if (items == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(items, seq->items, seq->count);
        items[(size_t)i] = value;
        {
            Value result;
            result = idem_array(items, seq->count, seq->type);
            free(items);
            return result;
        }
    }

    if (collection.tag == IDEM_TAG_MAP) {
        IdemMapObj *map;
        Value *keys;
        Value *values;
        size_t i;
        size_t used;
        int found;

        map = idem_as_map(collection);
        keys = (Value *)malloc(sizeof(Value) * (map->count + 1));
        values = (Value *)malloc(sizeof(Value) * (map->count + 1));
        if (keys == NULL || values == NULL) {
            idem_panic_oom();
        }
        used = 0;
        found = 0;
        for (i = 0; i < map->count; i++) {
            keys[used] = map->keys[i];
            values[used] = map->values[i];
            if (!found && idem_value_equal(map->keys[i], index)) {
                values[used] = value;
                found = 1;
            }
            used += 1;
        }
        if (!found) {
            keys[used] = index;
            values[used] = value;
            used += 1;
        }
        {
            Value result;
            result = idem_map_lit(keys, values, used, map->type);
            free(keys);
            free(values);
            return result;
        }
    }

    idem_panic_str("set expects an Array or Map");
    return idem_unit();
}

Value idem_remove(Value map_value, Value key)
{
    IdemMapObj *map;
    Value *keys;
    Value *values;
    size_t i;
    size_t used;

    map = idem_as_map(map_value);
    keys = (Value *)malloc(sizeof(Value) * (map->count + 1));
    values = (Value *)malloc(sizeof(Value) * (map->count + 1));
    if (keys == NULL || values == NULL) {
        idem_panic_oom();
    }
    used = 0;
    for (i = 0; i < map->count; i++) {
        if (!idem_value_equal(map->keys[i], key)) {
            keys[used] = map->keys[i];
            values[used] = map->values[i];
            used += 1;
        }
    }
    {
        Value result;
        result = idem_map_lit(keys, values, used, map->type);
        free(keys);
        free(values);
        return result;
    }
}

Value idem_push(Value list, Value value)
{
    IdemSeqObj *seq;
    Value *items;
    Value result;

    if (list.tag != IDEM_TAG_LIST) {
        idem_panic_str("push expects a List");
    }
    seq = idem_as_seq(list, "expected a List");
    items = (Value *)malloc(sizeof(Value) * (seq->count + 1));
    if (items == NULL) {
        idem_panic_oom();
    }
    idem_copy_values(items, seq->items, seq->count);
    items[seq->count] = value;
    result = idem_list(items, seq->count + 1, seq->type);
    free(items);
    return result;
}

Value idem_concat(Value a, Value b)
{
    if (a.tag == IDEM_TAG_STRING || a.tag == IDEM_TAG_BYTES) {
        IdemStringObj *x;
        IdemStringObj *y;
        char *data;
        Value result;
        size_t total;

        if (a.tag != b.tag) {
            idem_panic_str("concat expects matching types");
        }
        x = (IdemStringObj *)a.as.obj;
        y = (IdemStringObj *)b.as.obj;
        total = x->length + y->length;
        data = (char *)malloc(total + 1);
        if (data == NULL) {
            idem_panic_oom();
        }
        memcpy(data, x->data, x->length);
        memcpy(data + x->length, y->data, y->length);
        data[total] = '\0';
        result = a.tag == IDEM_TAG_STRING
                     ? idem_string(data, total)
                     : idem_bytes(data, total);
        free(data);
        return result;
    }

    if (a.tag == IDEM_TAG_ARRAY || a.tag == IDEM_TAG_LIST) {
        IdemSeqObj *x;
        IdemSeqObj *y;
        Value *items;
        Value result;

        if (a.tag != b.tag) {
            idem_panic_str("concat expects matching types");
        }
        x = idem_as_seq(a, "expected a collection");
        y = idem_as_seq(b, "expected a collection");
        items = (Value *)malloc(sizeof(Value) * (x->count + y->count));
        if (items == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(items, x->items, x->count);
        idem_copy_values(items + x->count, y->items, y->count);
        result = idem_seq_like(a.tag, items, x->count + y->count, x->type);
        free(items);
        return result;
    }

    idem_panic_str("concat expects String, Bytes, Array, or List");
    return idem_unit();
}

Value idem_map(Value collection, Value function,
               const IdemTypeDesc *result_type)
{
    IdemSeqObj *seq;
    Value *items;
    Value tag_value;
    size_t i;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("map expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected a collection");
    tag_value = collection;

    if (seq->count == 0) {
        return idem_seq_like(tag_value.tag, NULL, 0, result_type);
    }

    items = (Value *)malloc(sizeof(Value) * seq->count);
    if (items == NULL) {
        idem_panic_oom();
    }
    for (i = 0; i < seq->count; i++) {
        items[i] = idem_unit();
    }
    idem_gc_push_frame(items, seq->count);

    for (i = 0; i < seq->count; i++) {
        Value args[1];
        args[0] = seq->items[i];
        items[i] = idem_value_call(function, args, 1);
    }

    result = idem_seq_like(tag_value.tag, items, seq->count, result_type);
    idem_gc_pop_frame();
    free(items);
    return result;
}

Value idem_filter(Value collection, Value function)
{
    IdemSeqObj *seq;
    Value *items;
    size_t i;
    size_t used;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("filter expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected a collection");

    if (seq->count == 0) {
        return idem_seq_like(collection.tag, NULL, 0, seq->type);
    }

    items = (Value *)malloc(sizeof(Value) * seq->count);
    if (items == NULL) {
        idem_panic_oom();
    }
    for (i = 0; i < seq->count; i++) {
        items[i] = idem_unit();
    }
    idem_gc_push_frame(items, seq->count);

    used = 0;
    for (i = 0; i < seq->count; i++) {
        Value args[1];
        Value keep;
        args[0] = seq->items[i];
        keep = idem_value_call(function, args, 1);
        if (idem_truth(keep)) {
            items[used] = seq->items[i];
            used += 1;
        }
    }

    result = idem_seq_like(collection.tag, items, used, seq->type);
    idem_gc_pop_frame();
    free(items);
    return result;
}

Value idem_fold(Value collection, Value initial, Value function)
{
    IdemSeqObj *seq;
    Value roots[1];
    size_t i;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("fold expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected a collection");

    roots[0] = initial;
    idem_gc_push_frame(roots, 1);

    for (i = 0; i < seq->count; i++) {
        Value args[2];
        args[0] = roots[0];
        args[1] = seq->items[i];
        roots[0] = idem_value_call(function, args, 2);
    }

    result = roots[0];
    idem_gc_pop_frame();
    return result;
}

Value idem_sum_int(Value collection)
{
    IdemSeqObj *seq;
    Value total;
    size_t i;

    seq = idem_as_seq(collection, "sum expects an Array or List");
    total = idem_int(0);
    for (i = 0; i < seq->count; i++) {
        total = idem_int_add(total, seq->items[i]);
    }
    return total;
}

Value idem_sum_float(Value collection)
{
    IdemSeqObj *seq;
    double total;
    size_t i;

    seq = idem_as_seq(collection, "sum expects an Array or List");
    total = 0.0;
    for (i = 0; i < seq->count; i++) {
        total += seq->items[i].as.f;
    }
    return idem_float(total);
}

Value idem_keys(Value map_value, const IdemTypeDesc *result_type)
{
    IdemMapObj *map;
    Value result;

    map = idem_as_map(map_value);
    result = idem_array(map->keys, map->count, result_type);
    return result;
}

Value idem_values(Value map_value, const IdemTypeDesc *result_type)
{
    IdemMapObj *map;
    Value result;

    map = idem_as_map(map_value);
    result = idem_array(map->values, map->count, result_type);
    return result;
}
